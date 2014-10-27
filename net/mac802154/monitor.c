/*
 * Copyright 2007, 2008, 2009 Siemens AG
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
#include <linux/ieee802154.h>

#include <net/mac802154.h>
#include <net/netlink.h>
#include <net/cfg802154.h>
#include <linux/nl802154.h>

#include "ieee802154_i.h"

static const struct net_device_ops mac802154_monitor_ops = {
	.ndo_open		= mac802154_slave_open,
	.ndo_stop		= mac802154_slave_close,
	.ndo_start_xmit		= ieee802154_monitor_start_xmit,
};

void mac802154_monitor_setup(struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata;

	dev->addr_len		= 0;
	dev->hard_header_len	= 0;
	dev->needed_tailroom	= 2; /* room for FCS */
	dev->mtu		= IEEE802154_MTU;
	dev->tx_queue_len	= 10;
	dev->type		= ARPHRD_IEEE802154_MONITOR;
	dev->flags		= IFF_NOARP | IFF_BROADCAST;
	dev->watchdog_timeo	= 0;

	dev->destructor		= free_netdev;
	dev->netdev_ops		= &mac802154_monitor_ops;
	dev->ml_priv		= &mac802154_mlme_reduced;

	sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	sdata->type = IEEE802154_DEV_MONITOR;

	sdata->chan = MAC802154_CHAN_NONE; /* not initialized */
	sdata->page = 0;
}
