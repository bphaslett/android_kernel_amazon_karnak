/*
 * Copyright 2007-2012 Siemens AG
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
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Sergey Lapin <slapin@ossfans.org>
 * Maxim Gorbachyov <maxim.gorbachev@siemens.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/crc-ccitt.h>

#include <net/rtnetlink.h>
#include <net/ieee802154_netdev.h>
#include <net/mac802154.h>
#include <net/cfg802154.h>

#include "ieee802154_i.h"

/* IEEE 802.15.4 transceivers can sleep during the xmit session, so process
 * packets through the workqueue.
 */
struct wpan_xmit_cb {
	struct sk_buff *skb;
	struct work_struct work;
	struct ieee802154_local *local;
};

static inline struct wpan_xmit_cb *wpan_xmit_cb(const struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(skb->cb) < sizeof(struct wpan_xmit_cb));

	return (struct wpan_xmit_cb *)skb->cb;
}

static void mac802154_xmit_worker(struct work_struct *work)
{
	struct wpan_xmit_cb *cb = container_of(work, struct wpan_xmit_cb, work);
	struct ieee802154_local *local = cb->local;
	struct sk_buff *skb = cb->skb;
	int res;

	rtnl_lock();

	/* check if ifdown occurred while schedule */
	if (!netif_running(skb->dev))
		goto err_tx;

	res = local->ops->xmit_sync(&local->hw, skb);
	if (res)
		goto err_tx;

	ieee802154_xmit_complete(&local->hw, skb);

	rtnl_unlock();

	return;

err_tx:
	/* Restart the netif queue on each sub_if_data object. */
	ieee802154_wake_queue(&local->hw);
	rtnl_unlock();
	kfree_skb(skb);
	netdev_dbg(skb->dev, "transmission failed\n");
}

static netdev_tx_t
mac802154_tx(struct ieee802154_local *local, struct sk_buff *skb)
{
	struct wpan_xmit_cb *cb = wpan_xmit_cb(skb);
	int ret;

	mac802154_monitors_rx(local, skb);

	if (!(local->hw.flags & IEEE802154_HW_OMIT_CKSUM)) {
		__le16 crc = cpu_to_le16(crc_ccitt(0, skb->data, skb->len));

		memcpy(skb_put(skb, 2), &crc, 2);
	}

	if (skb_cow_head(skb, local->hw.extra_tx_headroom))
		goto err_tx;

	/* Stop the netif queue on each sub_if_data object. */
	ieee802154_stop_queue(&local->hw);

	/* async is priority, otherwise sync is fallback */
	if (local->ops->xmit_async) {
		ret = local->ops->xmit_async(&local->hw, skb);
		if (ret) {
			ieee802154_wake_queue(&local->hw);
			goto err_tx;
		}
	} else {
		INIT_WORK(&cb->work, mac802154_xmit_worker);
		cb->skb = skb;
		cb->local = local;

		queue_work(local->workqueue, &cb->work);
	}

	return NETDEV_TX_OK;

err_tx:
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

netdev_tx_t mac802154_monitor_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);

	skb->skb_iif = dev->ifindex;
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	return mac802154_tx(sdata->local, skb);
}

netdev_tx_t mac802154_wpan_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	int rc;

	rc = mac802154_llsec_encrypt(&sdata->sec, skb);
	if (rc) {
		netdev_warn(dev, "encryption failed: %i\n", rc);
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	skb->skb_iif = dev->ifindex;
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	return mac802154_tx(sdata->local, skb);
}
