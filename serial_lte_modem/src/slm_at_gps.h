/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef SLM_AT_GPS_
#define SLM_AT_GPS_

/**@file slm_at_gps.h
 *
 * @brief Vendor-specific AT command for GPS service.
 * @{
 */

#include <nrf_socket.h>
#include <zephyr/types.h>
#include "slm_at_host.h"

//////////////////////////////////////////////////
struct gps_client {
	int sock; /* Socket descriptor. */
	u16_t mask; /* NMEA mask */
	bool running; /* GPS running status */
	bool has_fix; /* At least one fix is got */
	at_cmd_handler_t callback;
};
//////////////////////////////////////////////////

/**
 * @brief GPS AT command parser.
 *
 * @param at_cmd  AT command string.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int slm_at_gps_parse(const char *at_cmd);

/**
 * @brief Initialize GPS AT command parser.
 *
 * @param callback Callback function to send AT response
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int slm_at_gps_init(at_cmd_handler_t callback);

/**
 * @brief Uninitialize GPS AT command parser.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int slm_at_gps_uninit(void);

void wait_for_gps_fix(void);

nrf_gnss_data_frame_t get_gps_nmea(void);

/** @} */

#endif /* SLM_AT_GPS_ */
