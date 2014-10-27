/*
 * Copyright (C) 2007-2012 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Written by:
 * Pavel Smolenskiy <pavel.smolenskiy@gmail.com>
 * Maxim Gorbachyov <maxim.gorbachev@siemens.com>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/crc-ccitt.h>

#include <net/mac802154.h>
#include <net/ieee802154_netdev.h>
#include <net/rtnetlink.h>
#include <linux/nl802154.h>

#include "ieee802154_i.h"

static int mac802154_process_data(struct net_device *dev, struct sk_buff *skb)
{
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->protocol = htons(ETH_P_IEEE802154);

	return netif_receive_skb(skb);
}

static int
mac802154_subif_frame(struct ieee802154_sub_if_data *sdata, struct sk_buff *skb,
		      const struct ieee802154_hdr *hdr)
{
	__le16 span, sshort;
	int rc;

	pr_debug("getting packet via slave interface %s\n", sdata->dev->name);

	spin_lock_bh(&sdata->mib_lock);

	span = sdata->pan_id;
	sshort = sdata->short_addr;

	switch (mac_cb(skb)->dest.mode) {
	case IEEE802154_ADDR_NONE:
		if (mac_cb(skb)->dest.mode != IEEE802154_ADDR_NONE)
			/* FIXME: check if we are PAN coordinator */
			skb->pkt_type = PACKET_OTHERHOST;
		else
			/* ACK comes with both addresses empty */
			skb->pkt_type = PACKET_HOST;
		break;
	case IEEE802154_ADDR_LONG:
		if (mac_cb(skb)->dest.pan_id != span &&
		    mac_cb(skb)->dest.pan_id != cpu_to_le16(IEEE802154_PANID_BROADCAST))
			skb->pkt_type = PACKET_OTHERHOST;
		else if (mac_cb(skb)->dest.extended_addr == sdata->extended_addr)
			skb->pkt_type = PACKET_HOST;
		else
			skb->pkt_type = PACKET_OTHERHOST;
		break;
	case IEEE802154_ADDR_SHORT:
		if (mac_cb(skb)->dest.pan_id != span &&
		    mac_cb(skb)->dest.pan_id != cpu_to_le16(IEEE802154_PANID_BROADCAST))
			skb->pkt_type = PACKET_OTHERHOST;
		else if (mac_cb(skb)->dest.short_addr == sshort)
			skb->pkt_type = PACKET_HOST;
		else if (mac_cb(skb)->dest.short_addr ==
			  cpu_to_le16(IEEE802154_ADDR_BROADCAST))
			skb->pkt_type = PACKET_BROADCAST;
		else
			skb->pkt_type = PACKET_OTHERHOST;
		break;
	default:
		spin_unlock_bh(&sdata->mib_lock);
		pr_debug("invalid dest mode\n");
		kfree_skb(skb);
		return NET_RX_DROP;
	}

	spin_unlock_bh(&sdata->mib_lock);

	skb->dev = sdata->dev;

	rc = mac802154_llsec_decrypt(&sdata->sec, skb);
	if (rc) {
		pr_debug("decryption failed: %i\n", rc);
		goto fail;
	}

	sdata->dev->stats.rx_packets++;
	sdata->dev->stats.rx_bytes += skb->len;

	switch (mac_cb(skb)->type) {
	case IEEE802154_FC_TYPE_DATA:
		return mac802154_process_data(sdata->dev, skb);
	default:
		pr_warn("ieee802154: bad frame received (type = %d)\n",
			mac_cb(skb)->type);
		goto fail;
	}

fail:
	kfree_skb(skb);
	return NET_RX_DROP;
}

static void mac802154_print_addr(const char *name,
				 const struct ieee802154_addr *addr)
{
	if (addr->mode == IEEE802154_ADDR_NONE)
		pr_debug("%s not present\n", name);

	pr_debug("%s PAN ID: %04x\n", name, le16_to_cpu(addr->pan_id));
	if (addr->mode == IEEE802154_ADDR_SHORT) {
		pr_debug("%s is short: %04x\n", name,
			 le16_to_cpu(addr->short_addr));
	} else {
		u64 hw = swab64((__force u64)addr->extended_addr);

		pr_debug("%s is hardware: %8phC\n", name, &hw);
	}
}

static int mac802154_parse_frame_start(struct sk_buff *skb,
				       struct ieee802154_hdr *hdr)
{
	int hlen;
	struct ieee802154_mac_cb *cb = mac_cb_init(skb);

	hlen = ieee802154_hdr_pull(skb, hdr);
	if (hlen < 0)
		return -EINVAL;

	skb->mac_len = hlen;

	pr_debug("fc: %04x dsn: %02x\n", le16_to_cpup((__le16 *)&hdr->fc),
		 hdr->seq);

	cb->type = hdr->fc.type;
	cb->ackreq = hdr->fc.ack_request;
	cb->secen = hdr->fc.security_enabled;

	mac802154_print_addr("destination", &hdr->dest);
	mac802154_print_addr("source", &hdr->source);

	cb->source = hdr->source;
	cb->dest = hdr->dest;

	if (hdr->fc.security_enabled) {
		u64 key;

		pr_debug("seclevel %i\n", hdr->sec.level);

		switch (hdr->sec.key_id_mode) {
		case IEEE802154_SCF_KEY_IMPLICIT:
			pr_debug("implicit key\n");
			break;

		case IEEE802154_SCF_KEY_INDEX:
			pr_debug("key %02x\n", hdr->sec.key_id);
			break;

		case IEEE802154_SCF_KEY_SHORT_INDEX:
			pr_debug("key %04x:%04x %02x\n",
				 le32_to_cpu(hdr->sec.short_src) >> 16,
				 le32_to_cpu(hdr->sec.short_src) & 0xffff,
				 hdr->sec.key_id);
			break;

		case IEEE802154_SCF_KEY_HW_INDEX:
			key = swab64((__force u64)hdr->sec.extended_src);
			pr_debug("key source %8phC %02x\n", &key,
				 hdr->sec.key_id);
			break;
		}
	}

	return 0;
}

static void
mac802154_wpans_rx(struct ieee802154_local *local, struct sk_buff *skb)
{
	int ret;
	struct ieee802154_sub_if_data *sdata;
	struct ieee802154_hdr hdr;

	ret = mac802154_parse_frame_start(skb, &hdr);
	if (ret) {
		pr_debug("got invalid frame\n");
		kfree_skb(skb);
		return;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (sdata->type != IEEE802154_DEV_WPAN ||
		    !netif_running(sdata->dev))
			continue;

		mac802154_subif_frame(sdata, skb, &hdr);
		skb = NULL;
		break;
	}
	rcu_read_unlock();

	if (skb)
		kfree_skb(skb);
}

static void
mac802154_monitors_rx(struct ieee802154_local *local, struct sk_buff *skb)
{
	struct sk_buff *skb2;
	struct ieee802154_sub_if_data *sdata;
	u16 crc = crc_ccitt(0, skb->data, skb->len);
	u8 *data;

	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_IEEE802154);

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (sdata->type != IEEE802154_DEV_MONITOR ||
		    !netif_running(sdata->dev))
			continue;

		skb2 = skb_clone(skb, GFP_ATOMIC);
		skb2->dev = sdata->dev;
		skb2->pkt_type = PACKET_HOST;
		data = skb_put(skb2, 2);
		data[0] = crc & 0xff;
		data[1] = crc >> 8;

		netif_rx_ni(skb2);
	}
	rcu_read_unlock();
}

void ieee802154_rx(struct ieee802154_hw *hw, struct sk_buff *skb)
{
	struct ieee802154_local *local = hw_to_local(hw);

	WARN_ON_ONCE(softirq_count() == 0);

	skb_reset_mac_header(skb);

	if (!(local->hw.flags & IEEE802154_HW_OMIT_CKSUM)) {
		u16 crc;

		if (skb->len < 2) {
			pr_debug("got invalid frame\n");
			goto fail;
		}
		crc = crc_ccitt(0, skb->data, skb->len);
		if (crc) {
			pr_debug("CRC mismatch\n");
			goto fail;
		}
		skb_trim(skb, skb->len - 2); /* CRC */
	}

	mac802154_monitors_rx(local, skb);
	mac802154_wpans_rx(local, skb);

	return;

fail:
	kfree_skb(skb);
}
EXPORT_SYMBOL(ieee802154_rx);

void
ieee802154_rx_irqsafe(struct ieee802154_hw *hw, struct sk_buff *skb, u8 lqi)
{
	struct ieee802154_local *local = hw_to_local(hw);

	mac_cb(skb)->lqi = lqi;
	skb->pkt_type = IEEE802154_RX_MSG;
	skb_queue_tail(&local->skb_queue, skb);
	tasklet_schedule(&local->tasklet);
}
EXPORT_SYMBOL(ieee802154_rx_irqsafe);
