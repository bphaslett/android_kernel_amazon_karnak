/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: wcmd.h
 *
 * Purpose: Handles the management command interface functions
 *
 * Author: Lyndon Chen
 *
 * Date: May 8, 2002
 *
 */

#ifndef __WCMD_H__
#define __WCMD_H__

#include "ttype.h"
#include "80211hdr.h"
#include "80211mgr.h"

#define AUTHENTICATE_TIMEOUT   1000
#define ASSOCIATE_TIMEOUT      1000

typedef enum tagCMD_CODE {
	WLAN_CMD_BSSID_SCAN,
	WLAN_CMD_SSID,
	WLAN_CMD_DISASSOCIATE,
	WLAN_CMD_DEAUTH,
	WLAN_CMD_RX_PSPOLL,
	WLAN_CMD_RADIO,
	WLAN_CMD_CHANGE_BBSENSITIVITY,
	WLAN_CMD_SETPOWER,
	WLAN_CMD_TBTT_WAKEUP,
	WLAN_CMD_BECON_SEND,
	WLAN_CMD_CHANGE_ANTENNA,
	WLAN_CMD_REMOVE_ALLKEY,
	WLAN_CMD_MAC_DISPOWERSAVING,
	WLAN_CMD_11H_CHSW,
	WLAN_CMD_RUN_AP
} CMD_CODE, *PCMD_CODE;

#define CMD_Q_SIZE              32

typedef enum tagCMD_STATUS {
	CMD_STATUS_SUCCESS = 0,
	CMD_STATUS_FAILURE,
	CMD_STATUS_RESOURCES,
	CMD_STATUS_TIMEOUT,
	CMD_STATUS_PENDING
} CMD_STATUS, *PCMD_STATUS;

typedef struct tagCMD_ITEM {
	CMD_CODE eCmd;
	unsigned char abyCmdDesireSSID[WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1];
	bool bNeedRadioOFF;
	unsigned short wDeAuthenReason;
	bool bRadioCmd;
	bool bForceSCAN;
} CMD_ITEM, *PCMD_ITEM;

typedef enum tagCMD_STATE {
	WLAN_CMD_SCAN_START,
	WLAN_CMD_SCAN_END,
	WLAN_CMD_DISASSOCIATE_START,
	WLAN_CMD_SSID_START,
	WLAN_AUTHENTICATE_WAIT,
	WLAN_ASSOCIATE_WAIT,
	WLAN_DISASSOCIATE_WAIT,
	WLAN_CMD_TX_PSPACKET_START,
	WLAN_CMD_AP_MODE_START,
	WLAN_CMD_RADIO_START,
	WLAN_CMD_CHECK_BBSENSITIVITY_CHANGE,
	WLAN_CMD_IDLE
} CMD_STATE, *PCMD_STATE;

#endif //__WCMD_H__
