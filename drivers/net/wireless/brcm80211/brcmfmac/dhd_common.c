/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/netdevice.h>
#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include "dhd.h"
#include "dhd_bus.h"
#include "dhd_dbg.h"
#include "fwil.h"
#include "fwil_types.h"
#include "tracepoint.h"

#define BRCMF_DEFAULT_BCN_TIMEOUT	3
#define BRCMF_DEFAULT_SCAN_CHANNEL_TIME	40
#define BRCMF_DEFAULT_SCAN_UNASSOC_TIME	40

/* boost value for RSSI_DELTA in preferred join selection */
#define BRCMF_JOIN_PREF_RSSI_BOOST	8


bool brcmf_c_prec_enq(struct device *dev, struct pktq *q,
		      struct sk_buff *pkt, int prec)
{
	struct sk_buff *p;
	int eprec = -1;		/* precedence to evict from */
	bool discard_oldest;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	/* Fast case, precedence queue is not full and we are also not
	 * exceeding total queue length
	 */
	if (!pktq_pfull(q, prec) && !pktq_full(q)) {
		brcmu_pktq_penq(q, prec, pkt);
		return true;
	}

	/* Determine precedence from which to evict packet, if any */
	if (pktq_pfull(q, prec))
		eprec = prec;
	else if (pktq_full(q)) {
		p = brcmu_pktq_peek_tail(q, &eprec);
		if (eprec > prec)
			return false;
	}

	/* Evict if needed */
	if (eprec >= 0) {
		/* Detect queueing to unconfigured precedence */
		discard_oldest = ac_bitmap_tst(drvr->wme_dp, eprec);
		if (eprec == prec && !discard_oldest)
			return false;	/* refuse newer (incoming) packet */
		/* Evict packet according to discard policy */
		p = discard_oldest ? brcmu_pktq_pdeq(q, eprec) :
			brcmu_pktq_pdeq_tail(q, eprec);
		if (p == NULL)
			brcmf_err("brcmu_pktq_penq() failed, oldest %d\n",
				  discard_oldest);

		brcmu_pkt_buf_free_skb(p);
	}

	/* Enqueue */
	p = brcmu_pktq_penq(q, prec, pkt);
	if (p == NULL)
		brcmf_err("brcmu_pktq_penq() failed\n");

	return p != NULL;
}

int brcmf_c_preinit_dcmds(struct brcmf_if *ifp)
{
	s8 eventmask[BRCMF_EVENTING_MASK_LEN];
	u8 buf[BRCMF_DCMD_SMLEN];
	struct brcmf_join_pref_params join_pref_params[2];
	char *ptr;
	s32 err;

	/* retreive mac address */
	err = brcmf_fil_iovar_data_get(ifp, "cur_etheraddr", ifp->mac_addr,
				       sizeof(ifp->mac_addr));
	if (err < 0) {
		brcmf_err("Retreiving cur_etheraddr failed, %d\n",
			  err);
		goto done;
	}
	memcpy(ifp->drvr->mac, ifp->mac_addr, sizeof(ifp->drvr->mac));

	/* query for 'ver' to get version info from firmware */
	memset(buf, 0, sizeof(buf));
	strcpy(buf, "ver");
	err = brcmf_fil_iovar_data_get(ifp, "ver", buf, sizeof(buf));
	if (err < 0) {
		brcmf_err("Retreiving version information failed, %d\n",
			  err);
		goto done;
	}
	ptr = (char *)buf;
	strsep(&ptr, "\n");

	/* Print fw version info */
	brcmf_err("Firmware version = %s\n", buf);

	/* locate firmware version number for ethtool */
	ptr = strrchr(buf, ' ') + 1;
	strlcpy(ifp->drvr->fwver, ptr, sizeof(ifp->drvr->fwver));

	/* set mpc */
	err = brcmf_fil_iovar_int_set(ifp, "mpc", 1);
	if (err) {
		brcmf_err("failed setting mpc\n");
		goto done;
	}

	/*
	 * Setup timeout if Beacons are lost and roam is off to report
	 * link down
	 */
	err = brcmf_fil_iovar_int_set(ifp, "bcn_timeout",
				      BRCMF_DEFAULT_BCN_TIMEOUT);
	if (err) {
		brcmf_err("bcn_timeout error (%d)\n", err);
		goto done;
	}

	/* Enable/Disable build-in roaming to allowed ext supplicant to take
	 * of romaing
	 */
	err = brcmf_fil_iovar_int_set(ifp, "roam_off", 1);
	if (err) {
		brcmf_err("roam_off error (%d)\n", err);
		goto done;
	}

	/* Setup join_pref to select target by RSSI(with boost on 5GHz) */
	join_pref_params[0].type = BRCMF_JOIN_PREF_RSSI_DELTA;
	join_pref_params[0].len = 2;
	join_pref_params[0].rssi_gain = BRCMF_JOIN_PREF_RSSI_BOOST;
	join_pref_params[0].band = WLC_BAND_5G;
	join_pref_params[1].type = BRCMF_JOIN_PREF_RSSI;
	join_pref_params[1].len = 2;
	join_pref_params[1].rssi_gain = 0;
	join_pref_params[1].band = 0;
	err = brcmf_fil_iovar_data_set(ifp, "join_pref", join_pref_params,
				       sizeof(join_pref_params));
	if (err)
		brcmf_err("Set join_pref error (%d)\n", err);

	/* Setup event_msgs, enable E_IF */
	err = brcmf_fil_iovar_data_get(ifp, "event_msgs", eventmask,
				       BRCMF_EVENTING_MASK_LEN);
	if (err) {
		brcmf_err("Get event_msgs error (%d)\n", err);
		goto done;
	}
	setbit(eventmask, BRCMF_E_IF);
	err = brcmf_fil_iovar_data_set(ifp, "event_msgs", eventmask,
				       BRCMF_EVENTING_MASK_LEN);
	if (err) {
		brcmf_err("Set event_msgs error (%d)\n", err);
		goto done;
	}

	/* Setup default scan channel time */
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_CHANNEL_TIME,
				    BRCMF_DEFAULT_SCAN_CHANNEL_TIME);
	if (err) {
		brcmf_err("BRCMF_C_SET_SCAN_CHANNEL_TIME error (%d)\n",
			  err);
		goto done;
	}

	/* Setup default scan unassoc time */
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_UNASSOC_TIME,
				    BRCMF_DEFAULT_SCAN_UNASSOC_TIME);
	if (err) {
		brcmf_err("BRCMF_C_SET_SCAN_UNASSOC_TIME error (%d)\n",
			  err);
		goto done;
	}

	/* do bus specific preinit here */
	err = brcmf_bus_preinit(ifp->drvr->bus_if);
done:
	return err;
}

#if defined(CONFIG_BRCM_TRACING) || defined(CONFIG_BRCMDBG)
void __brcmf_dbg(u32 level, const char *func, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	if (brcmf_msg_level & level)
		pr_debug("%s %pV", func, &vaf);
	trace_brcmf_dbg(level, func, &vaf);
	va_end(args);
}
#endif
