/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */
#include <logging/log.h>

#include <zephyr.h>
#include <stdio.h>
//#include <nrf_socket.h>
#include <net/socket.h>
#include "slm_at_gps.h"

///////////////////////////////////////////////////////////////////////////
#define AT_XSYSTEMMODE "AT\%XSYSTEMMODE=0,1,1,0"  // enable NB1 and GNSS
#define AT_CFUN0       "AT+CFUN=0" // off
#define AT_CFUN1       "AT+CFUN=1" // on
#define AT_CPSMS1	   "AT+CPSMS=1,\"\",\"\",\"10101010\",\"00000001\""

#ifdef CONFIG_BOARD_NRF9160_PCA10090NS
#define AT_MAGPIO      "AT\%XMAGPIO=1,0,0,1,1,1574,1577"
#define AT_COEX0       "AT\%XCOEX0=1,1,1570,1580"
#endif

static const char     gps_at_commands[][40]  = {
				//AT_XSYSTEMMODE, // already enabled both in TCP/IP
#ifdef CONFIG_BOARD_NRF9160_PCA10090NS
				AT_MAGPIO,
				AT_COEX0,
#endif
				AT_CFUN1,
				AT_CPSMS1 /////////////////////////////////
			};
///////////////////////////////////////////////////////////////////////////

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
static int handle_at_gpsrun(enum at_cmd_type cmd_type);

/**@brief SLM AT Command list type. */
static slm_at_cmd_list_t m_gps_at_list[AT_GPS_MAX] = {
	{AT_GPSRUN, "AT#XGPSRUN", handle_at_gpsrun},
};

//////////////////////////////////////
struct gps_client gps_client_inst; 
nrf_gnss_data_frame_t gps_data; // was static
struct current_loc current_location;
uint8_t notified = 0;
//////////////////////////////////////////

static char buf[64];

#define THREAD_STACK_SIZE	KB(1)
#define THREAD_PRIORITY		K_LOWEST_APPLICATION_THREAD_PRIO

static struct k_thread gps_thread;
static k_tid_t gps_thread_id;
static K_THREAD_STACK_DEFINE(gps_thread_stack, THREAD_STACK_SIZE);
static u64_t ttft_start;



/* global variable defined in different files */
extern struct at_param_list m_param_list;

static void gps_satellite_stats(void)
{
	static u8_t last_tracked;
	u8_t tracked = 0;
	u8_t in_fix = 0;
	u8_t unhealthy = 0;

	if (gps_data.data_id != NRF_GNSS_PVT_DATA_ID || gps_client_inst.has_fix) {
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
		gps_client_inst.callback(buf);
		last_tracked = tracked;
	}
}

static void gps_pvt_notify(void)
{
	///////////////////////////////////////////////////
	LOG_INF("NOTIFY!");
	notified = 1;
	current_location.lat = gps_data.pvt.latitude;
	current_location.lon = gps_data.pvt.longitude;
	current_location.alt = gps_data.pvt.altitude;
	current_location.hdop = gps_data.pvt.hdop;
	current_location.datetime = gps_data.pvt.datetime;
	///////////////////////////////////////////////////

	// sprintf(buf, "#XGPSP: long %f lat %f\r\n",
	// 	gps_data.pvt.longitude,
	// 	gps_data.pvt.latitude);

	gps_client_inst.callback(buf);
	sprintf(buf, "#XGPSP: %04u-%02u-%02u %02u:%02u:%02u\r\n",
		gps_data.pvt.datetime.year,
		gps_data.pvt.datetime.month,
		gps_data.pvt.datetime.day,
		gps_data.pvt.datetime.hour,
		gps_data.pvt.datetime.minute,
		gps_data.pvt.datetime.seconds);
	gps_client_inst.callback(buf);
}

static void gps_thread_fn(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		if (nrf_recv(gps_client_inst.sock, &gps_data, sizeof(gps_data), 0)
			<= 0) {
			LOG_ERR("GPS nrf_recv(): %d", -errno);
			sprintf(buf, "#XGPSRUN: %d\r\n", -errno);
			gps_client_inst.callback(buf);
			nrf_close(gps_client_inst.sock);
			gps_client_inst.running = false;
			break;
		}
		gps_satellite_stats();
		switch (gps_data.data_id) {
		case NRF_GNSS_PVT_DATA_ID:
			//LOG_INF("PVT");
			if (IS_FIX(gps_data.pvt.flags)) {
				//LOG_INF("PVT HAS FIX");
				gps_pvt_notify();
				if (!gps_client_inst.has_fix) {
					u64_t now = k_uptime_get();
					sprintf(buf, "#	: TTFF %d sec\r\n",
						(int)(now - ttft_start)/1000);
					gps_client_inst.callback(buf);
					gps_client_inst.has_fix = true;
				}
			}
			break;
		case NRF_GNSS_NMEA_DATA_ID:
			//LOG_INF("NMEA");
			if (gps_client_inst.has_fix) {
				//LOG_INF("NMEA HAS FIX");
				//gps_client_inst.callback("#XGPSN: ");
				//gps_client_inst.callback(gps_data.nmea);
			}
			break;
		default:
			break;
		}
	}
}

static int do_gps_start(void)
{
	int ret = -EINVAL;

	nrf_gnss_fix_retry_t    fix_retry    = 0; /* unlimited retry period */
	nrf_gnss_fix_interval_t fix_interval = 1; /* 1s delay between fixes */
	nrf_gnss_delete_mask_t  delete_mask  = 0;
	nrf_gnss_nmea_mask_t    nmea_mask = (nrf_gnss_nmea_mask_t)gps_client_inst.mask;
	//nrf_gnss_power_save_mode_t power_mode = 1; ////////////////////////////////////

	gps_client_inst.sock = nrf_socket(NRF_AF_LOCAL, NRF_SOCK_DGRAM, NRF_PROTO_GNSS);
	if (gps_client_inst.sock < 0) {
		LOG_ERR("Could not init socket (err: %d)", -errno);
		goto error;
	}
	ret = nrf_setsockopt(gps_client_inst.sock, NRF_SOL_GNSS, NRF_SO_GNSS_FIX_RETRY,
			&fix_retry, sizeof(fix_retry));
	if (ret != 0) {
		LOG_ERR("Failed to set fix retry value (err: %d)", -errno);
		goto error;
	}
	ret = nrf_setsockopt(gps_client_inst.sock, NRF_SOL_GNSS,
		NRF_SO_GNSS_FIX_INTERVAL, &fix_interval, sizeof(fix_interval));
	if (ret != 0) {
		LOG_ERR("Failed to set fix interval value (err: %d)", -errno);
		goto error;
	}


	//////////////////////////////////////////////////////////
	// ret = nrf_setsockopt(gps_client_inst.sock, NRF_SOL_GNSS, NRF_SO_GNSS_POWER_SAVE_MODE,
	// 		&power_mode, sizeof(power_mode));
    // if (ret != 0) {
    //     LOG_ERR("Failed to set GPS power mode policy (err: %d)", -errno);
	// 	goto error;
	// }
	//////////////////////////////////////////////////////////


	ret = nrf_setsockopt(gps_client_inst.sock, NRF_SOL_GNSS, NRF_SO_GNSS_NMEA_MASK,
			&nmea_mask, sizeof(nmea_mask));
	if (ret != 0) {
		LOG_ERR("Failed to set nmea mask (err: %d)", -errno);
		goto error;
	}
	ret = nrf_setsockopt(gps_client_inst.sock, NRF_SOL_GNSS, NRF_SO_GNSS_START,
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

	gps_client_inst.running = true;
	LOG_DBG("GPS started");

	sprintf(buf, "#XGPSRUN: 1,%d\r\n", gps_client_inst.mask);
	gps_client_inst.callback(buf);
	ttft_start = k_uptime_get();
	return 0;

error:
	LOG_ERR("GPS start failed: %d", ret);
	sprintf(buf, "#XGPSRUN: %d\r\n", ret);
	gps_client_inst.callback(buf);
	gps_client_inst.running = false;

	return -errno;
}

static int do_gps_stop(void)
{
	int ret = 0;
	nrf_gnss_delete_mask_t	delete_mask  = 0;

	if (gps_client_inst.sock != INVALID_SOCKET) {
		ret = nrf_setsockopt(gps_client_inst.sock, NRF_SOL_GNSS,
			NRF_SO_GNSS_STOP, &delete_mask, sizeof(delete_mask));
		if (ret != 0) {
			LOG_ERR("Failed to stop GPS (err: %d)", -errno);
			ret = -errno;
		} else {
			k_thread_suspend(gps_thread_id);
			nrf_close(gps_client_inst.sock);
			gps_client_inst.running = false;
			gps_client_inst.callback("#XGPSRUN: 0\r\n");
			LOG_DBG("GPS stopped");
		}

	}

	return ret;
}

/**@brief handle AT#XPSRUN commands
 *  AT#XGPSRUN=<op>[,<mask>]
 *  AT#XGPSRUN?
 *  AT#XGPSRUN=? TEST command not supported
 */
static int handle_at_gpsrun(enum at_cmd_type cmd_type)
{
	int err = -EINVAL;
	u16_t op;

	switch (cmd_type) {
	case AT_CMD_TYPE_SET_COMMAND:
		if (at_params_valid_count_get(&m_param_list) < 2) {
			return -EINVAL;
		}
		err = at_params_short_get(&m_param_list, 1, &op);
		if (err < 0) {
			return err;
		}
		if (op == 1) {
			if (at_params_valid_count_get(&m_param_list) > 2) {
				err = at_params_short_get(&m_param_list, 2,
							&gps_client_inst.mask);
				if (err < 0) {
					return err;
				}
			}
			if (gps_client_inst.running) {
				LOG_WRN("GPS is running");
			} else {
				err = do_gps_start();
			}
		} else if (op == 0) {
			if (!gps_client_inst.running) {
				LOG_WRN("GPS is not running");
			} else {
				err = do_gps_stop();
			}
		} break;

	case AT_CMD_TYPE_READ_COMMAND:
		if (gps_client_inst.running) {
			sprintf(buf, "#XGPSRUN: 1,%d\r\n", gps_client_inst.mask);
		} else {
			sprintf(buf, "#XGPSRUN: 0\r\n");
		}
		gps_client_inst.callback(buf);
		err = 0;
		break;

	default:
		break;
	}

	return err;
}

/**@brief API to handle GPS AT commands
 */
int slm_at_gps_parse(const char *at_cmd)
{
	int ret = -ENOTSUP;
	enum at_cmd_type type;

	for (int i = 0; i < AT_GPS_MAX; i++) {
		u8_t cmd_len = strlen(m_gps_at_list[i].string);

		if (slm_at_cmd_cmp(at_cmd, m_gps_at_list[i].string, cmd_len)) {
			ret = at_parser_params_from_str(at_cmd, NULL,
						&m_param_list);
			if (ret < 0) {
				LOG_ERR("Failed to parse AT command %d", ret);
				return -EINVAL;
			}
			type = at_parser_cmd_type_get(at_cmd);
			ret = m_gps_at_list[i].handler(type);
			break;
		}
	}

	return ret;
}

/////////////////////////////////////////////////////////////////////////////
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
///////////////////////////////////////////////////////////////////////////

/**@brief API to initialize GPS AT commands handler
 */
int slm_at_gps_init(at_cmd_handler_t callback)
{
	if (callback == NULL) {
		LOG_ERR("No callback");
		return -EINVAL;
	}
	gps_client_inst.sock = INVALID_SOCKET;
	// gps_client_inst.mask =  NRF_GNSS_NMEA_GSV_MASK |
	// 	       NRF_GNSS_NMEA_GSA_MASK |
	// 	       NRF_GNSS_NMEA_GLL_MASK |
	// 	       NRF_GNSS_NMEA_GGA_MASK |
	// 	       NRF_GNSS_NMEA_RMC_MASK;
	gps_client_inst.mask = NRF_GNSS_NMEA_GGA_MASK; ///////////////////////
	gps_client_inst.running = false;
	gps_client_inst.has_fix = false;
	gps_client_inst.callback = callback;
	gps_thread_id = NULL;

	//////////////////////////////////////////////////////////////////////
	enable_gps();
	do_gps_start();
	//////////////////////////////////////////////////////////////////////

	return 0;
}

/**@brief API to uninitialize GPS AT commands handler
 */
int slm_at_gps_uninit(void)
{
	if (gps_thread_id != NULL) {
		do_gps_stop();
		k_thread_abort(gps_thread_id);
		gps_thread_id = NULL;
	}

	return 0;
}

void wait_for_gps_fix(void)
{
	while(!gps_client_inst.has_fix)
	{
		LOG_INF("waiting for gpsfix");
		k_sleep(K_SECONDS(1));
	}
}

nrf_gnss_data_frame_t* get_gps_data(void)
{
	return &gps_data;
}