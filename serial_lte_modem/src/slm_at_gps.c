/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */
#include <logging/log.h>

#include <zephyr.h>
#include <stdio.h>
#include <nrf_socket.h>
#include <net/socket.h>
#include "slm_at_gps.h"

#define AT_XSYSTEMMODE "AT\%XSYSTEMMODE=0,1,1,0"
#define AT_CFUN        "AT+CFUN=1"

#ifdef CONFIG_BOARD_NRF9160_PCA10090NS
#define AT_MAGPIO      "AT\%XMAGPIO=1,0,0,1,1,1574,1577"
#define AT_COEX0       "AT\%XCOEX0=1,1,1570,1580"
#endif

static const char     gps_at_commands[][31]  = {
				//AT_XSYSTEMMODE,
#ifdef CONFIG_BOARD_NRF9160_PCA10090NS
				AT_MAGPIO,
				AT_COEX0
#endif
				//AT_CFUN
			};


LOG_MODULE_REGISTER(gps, CONFIG_SLM_LOG_LEVEL);

#define IS_FIX(_flag)       (_flag & NRF_GNSS_PVT_FLAG_FIX_VALID_BIT)
#define IS_UNHEALTHY(_flag) (_flag & NRF_GNSS_SV_FLAG_UNHEALTHY)

#define INVALID_SOCKET	-1

/**@brief List of supported AT commands. */
enum slm_gps_mode {
	GPS_MODE_STANDALONE,
	GPS_MODE_PSM,
	GPS_MODE_EDRX,
	GPS_MODE_AGPS
};

/**@brief List of supported AT commands. */
enum slm_gps_at_cmd_type {
	AT_GPSRUN,
	AT_GPS_MAX
};

/** forward declaration of cmd handlers **/
static int handle_at_gpsrun(const char *at_cmd, size_t param_offset);

/**@brief SLM AT Command list type. */
static slm_at_cmd_list_t m_at_list[AT_GPS_MAX] = {
	{AT_GPSRUN, "AT#XGPSRUN", "at#xgpsrun", handle_at_gpsrun},
};

static struct gps_client {
	int sock; /* Socket descriptor. */
	u16_t mask; /* NMEA mask */
	bool running; /* GPS running status */
	bool has_fix; /* At least one fix is got */
	at_cmd_handler_t callback;
} client;

static struct current_gps_data_struct {
	double latitude;
	double longitude;
	float altitude;
	float hdop;
	nrf_gnss_datetime_t datetime;
} current_gps_data;

nrf_gnss_data_frame_t 	 gps_data;
static char buf[64];

#define THREAD_STACK_SIZE	KB(1)
#define THREAD_PRIORITY		K_LOWEST_APPLICATION_THREAD_PRIO

static struct k_thread gps_thread;
static k_tid_t gps_thread_id;
static K_THREAD_STACK_DEFINE(gps_thread_stack, THREAD_STACK_SIZE);

/* global variable defined in different files */
extern struct at_param_list m_param_list;

static void gps_satellite_stats(void)
{
	static u8_t last_tracked;
	u8_t tracked = 0;
	u8_t in_fix = 0;
	u8_t unhealthy = 0;

	if (gps_data.data_id != NRF_GNSS_PVT_DATA_ID || client.has_fix) {
		return;
	}

	for (int i = 0; i < NRF_GNSS_MAX_SATELLITES; ++i) {
		if ((gps_data.pvt.sv[i].sv > 0) &&
		    (gps_data.pvt.sv[i].sv < 33)) {
			LOG_DBG("GPS tracking: %d", gps_data.pvt.sv[i].sv);
			tracked++;
			if (IS_FIX(gps_data.pvt.sv[i].flags)) {
				in_fix++;
			}
			if (IS_UNHEALTHY(gps_data.pvt.sv[i].flags)) {
				unhealthy++;
			}
		}
	}

	if (last_tracked != tracked) {
		sprintf(buf, "#XGPSS: tracking %d using %d unhealthy %d\r\n",
			tracked, in_fix, unhealthy);
		client.callback(buf);
		last_tracked = tracked;
	}
}

static void print_pvt_data(nrf_gnss_data_frame_t *pvt_data)
{
	printf("Longitude:  %f\n", pvt_data->pvt.longitude);
	printf("Latitude:   %f\n", pvt_data->pvt.latitude);
	printf("Altitude:   %f\n", pvt_data->pvt.altitude);
	printf("Speed:      %f\n", pvt_data->pvt.speed);
	printf("Heading:    %f\n", pvt_data->pvt.heading);
	printk("Date:       %02u-%02u-%02u\n", pvt_data->pvt.datetime.day,
					       pvt_data->pvt.datetime.month,
					       pvt_data->pvt.datetime.year);
	printk("Time (UTC): %02u:%02u:%02u\n", pvt_data->pvt.datetime.hour,
					       pvt_data->pvt.datetime.minute,
					      pvt_data->pvt.datetime.seconds);
}

static void gps_pvt_notify(void)
{
	sprintf(buf, "#XGPSP: long %f lat %f\r\n",
		gps_data.pvt.longitude,
		gps_data.pvt.latitude);
	client.callback(buf);
	sprintf(buf, "#XGPSP: %04u-%02u-%02u %02u:%02u:%02u\r\n",
		gps_data.pvt.datetime.year,
		gps_data.pvt.datetime.month,
		gps_data.pvt.datetime.day,
		gps_data.pvt.datetime.hour,
		gps_data.pvt.datetime.minute,
		gps_data.pvt.datetime.seconds);
	client.callback(buf);
	//print_pvt_data(&gps_data.pvt);

	// Update current GPS data
	LOG_INF("Updating current GPS data");
	current_gps_data.latitude = gps_data.pvt.latitude;
	current_gps_data.longitude = gps_data.pvt.longitude;
	current_gps_data.altitude = gps_data.pvt.altitude;
	current_gps_data.hdop = gps_data.pvt.hdop;
	current_gps_data.datetime = gps_data.pvt.datetime;
}

static void gps_thread_fn(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		if (nrf_recv(client.sock, &gps_data, sizeof(gps_data), 0)
			<= 0) {
			LOG_ERR("GPS nrf_recv(): %d", -errno);
			sprintf(buf, "#XGPSRUN: %d\r\n", -errno);
			client.callback(buf);
			nrf_close(client.sock);
			client.running = false;
			break;
		}
		gps_satellite_stats();
		switch (gps_data.data_id) {
		case NRF_GNSS_PVT_DATA_ID:
			if (IS_FIX(gps_data.pvt.flags)) {
				LOG_INF("GPS FIX PVT DATA");
				gps_pvt_notify();
				if (!client.has_fix) {
					client.has_fix = true;
				}
			}
			break;
		case NRF_GNSS_NMEA_DATA_ID:
			if (client.has_fix) {
				LOG_INF("GPS FIX client.has_fix");	
				client.callback("#XGPSN: ");
				client.callback(gps_data.nmea);
			}
			break;
		default:
			break;
		}
	}
}

static int do_gps_start(void)
{
	LOG_DBG("do_gps_start");
	int ret = -EINVAL;

	nrf_gnss_fix_retry_t    fix_retry    = 0; /* unlimited retry period */
	nrf_gnss_fix_interval_t fix_interval = 1; /* 1s delay between fixes */
	nrf_gnss_delete_mask_t  delete_mask  = 0;
	nrf_gnss_nmea_mask_t    nmea_mask = (nrf_gnss_nmea_mask_t)client.mask;
	
	client.sock = nrf_socket(NRF_AF_LOCAL, NRF_SOCK_DGRAM, NRF_PROTO_GNSS);
	if (client.sock < 0) {
		LOG_ERR("Could not init socket (err: %d)", -errno);
		goto error;
	}
	ret = nrf_setsockopt(client.sock, NRF_SOL_GNSS, NRF_SO_GNSS_FIX_RETRY,
			&fix_retry, sizeof(fix_retry));
	if (ret != 0) {
		LOG_ERR("Failed to set fix retry value (err: %d)", -errno);
		goto error;
	}
	ret = nrf_setsockopt(client.sock, NRF_SOL_GNSS,
		NRF_SO_GNSS_FIX_INTERVAL, &fix_interval, sizeof(fix_interval));
	if (ret != 0) {
		LOG_ERR("Failed to set fix interval value (err: %d)", -errno);
		goto error;
	}
	ret = nrf_setsockopt(client.sock, NRF_SOL_GNSS, NRF_SO_GNSS_NMEA_MASK,
			&nmea_mask, sizeof(nmea_mask));
	if (ret != 0) {
		LOG_ERR("Failed to set nmea mask (err: %d)", -errno);
		goto error;
	}
	ret = nrf_setsockopt(client.sock, NRF_SOL_GNSS, NRF_SO_GNSS_START,
			&delete_mask, sizeof(delete_mask));
	if (ret != 0) {
		LOG_ERR("Failed to start GPS (err: %d)", -errno);
		goto error;
	}

	/* Start GPS listening thread */
	if (gps_thread_id != NULL) {
		k_thread_resume(gps_thread_id);
	} else {
		gps_thread_id = k_thread_create(&gps_thread, gps_thread_stack,
				K_THREAD_STACK_SIZEOF(gps_thread_stack),
				gps_thread_fn, NULL, NULL, NULL,
				THREAD_PRIORITY, 0, K_NO_WAIT);
	}

	client.running = true;
	LOG_DBG("GPS started");

	sprintf(buf, "#XGPSRUN: 1,%d\r\n", client.mask);
	client.callback(buf);
	return 0;

error:
	LOG_ERR("GPS start failed: %d", ret);
	sprintf(buf, "#XGPSRUN: %d\r\n", ret);
	client.callback(buf);
	client.running = false;

	return -errno;
}

static int do_gps_stop(void)
{
	int ret = 0;
	nrf_gnss_delete_mask_t	delete_mask  = 0;

	if (client.sock != INVALID_SOCKET) {
		ret = nrf_setsockopt(client.sock, NRF_SOL_GNSS,
			NRF_SO_GNSS_STOP, &delete_mask, sizeof(delete_mask));
		if (ret != 0) {
			LOG_ERR("Failed to stop GPS (err: %d)", -errno);
			ret = -errno;
		} else {
			k_thread_suspend(gps_thread_id);
			nrf_close(client.sock);
			client.running = false;
			client.callback("#XGPSRUN: 0\r\n");
			LOG_DBG("GPS stopped");
		}

	}

	return ret;
}

/**@brief handle AT#XGPSRUN commands
 *  AT#XGPSRUN=<op>[,<mask>]
 *  AT#XGPSRUN?
 *  AT#XGPSRUN=? TEST command not supported
 */
static int handle_at_gpsrun(const char *at_cmd, size_t param_offset)
{
	LOG_INF("handle_at_gpsrun");
	int err = -EINVAL;
	char *at_param = (char *)at_cmd + param_offset;
	u16_t op;

	if (*(at_param) == '=') {
		at_param++;
		if (*(at_param) == '?') {
			return err;
		}
		err = at_parser_params_from_str(at_cmd, NULL, &m_param_list);
		if (err < 0) {
			return err;
		};
		if (at_params_valid_count_get(&m_param_list) < 2) {
			return -EINVAL;
		}
		err = at_params_short_get(&m_param_list, 1, &op);
		if (err < 0) {
			return err;
		};
		if (op == 1) {
			if (at_params_valid_count_get(&m_param_list) > 2) {
				err = at_params_short_get(&m_param_list, 2,
							&client.mask);
				if (err < 0) {
					return err;
				};
			}
			if (client.running) {
				LOG_WRN("GPS is running");
			} else {
				LOG_INF("running do_gps_start from at handler");
				err = do_gps_start();
			}
		} else if (op == 0) {
			if (!client.running) {
				LOG_WRN("GPS is not running");
			} else {
				err = do_gps_stop();
			}
		}
	} else if (*(at_param) == '?') {
		if (client.running) {
			sprintf(buf, "#XGPSRUN: 1,%d\r\n", client.mask);
		} else {
			sprintf(buf, "#XGPSRUN: 0\r\n");
		}
		client.callback(buf);
		err = 0;
	}

	return err;
}

/**@brief API to handle GPS AT commands
 */
int slm_at_gps_parse(const u8_t *param, u8_t length)
{
	int ret = -ENOTSUP;

	ARG_UNUSED(length);

	for (int i = 0; i < AT_GPS_MAX; i++) {
		u8_t cmd_len = strlen(m_at_list[i].string_upper);

		if (strncmp(param, m_at_list[i].string_upper,
			cmd_len) == 0) {
			ret = m_at_list[i].handler(param, cmd_len);
			break;
		} else if (strncmp(param, m_at_list[i].string_lower,
			cmd_len) == 0) {
			ret = m_at_list[i].handler(param, cmd_len);
			break;
		}
	}

	return ret;
}

static int enable_gps(void)
{
	LOG_INF("Initializing GPS Module");

	int  at_sock;
	int  bytes_sent;
	int  bytes_received;
	char buf[2];

	at_sock = socket(AF_LTE, 0, NPROTO_AT);
	if (at_sock < 0) {
		return -1;
	}

	for (int i = 0; i < ARRAY_SIZE(gps_at_commands); i++) {
		LOG_INF("%s",gps_at_commands[i]);
		bytes_sent = send(at_sock, gps_at_commands[i],
				  strlen(gps_at_commands[i]), 0);

		if (bytes_sent < 0) {
			close(at_sock);
			return -1;
		}

		do {
			bytes_received = recv(at_sock, buf, 2, 0);
		} while (bytes_received == 0);

		if (memcmp(buf, "OK", 2) != 0) {
			close(at_sock);
			return -1;
		}
		//at_cmd_write(gps_at_commands[i], NULL, 0, NULL);
		k_sleep(K_SECONDS(1));
	
	}
	
	close(at_sock);
	LOG_INF("GPS Enabled");

	return 0;
}

/**@brief API to initialize GPS AT commands handler
 */
int slm_at_gps_init(at_cmd_handler_t callback)
{
	if (callback == NULL) {
		LOG_ERR("No callback");
		return -EINVAL;
	}
	client.sock = INVALID_SOCKET;
	client.mask =  NRF_GNSS_NMEA_GSV_MASK |
		       NRF_GNSS_NMEA_GSA_MASK |
		       NRF_GNSS_NMEA_GLL_MASK |
		       NRF_GNSS_NMEA_GGA_MASK |
		       NRF_GNSS_NMEA_RMC_MASK;
	client.running = false;
	client.has_fix = false;
	client.callback = callback;
	gps_thread_id = NULL;
	enable_gps();
	do_gps_start();
	return 0;
}
