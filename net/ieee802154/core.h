#ifndef __IEEE802154_CORE_H
#define __IEEE802154_CORE_H

#include <net/cfg802154.h>

struct cfg802154_registered_device {
	const struct cfg802154_ops *ops;
	struct list_head list;

	/* wpan_phy index, internal only */
	int wpan_phy_idx;

	/* also protected by devlist_mtx */
	int opencount;
	wait_queue_head_t dev_wait;

	/* protected by RTNL only */
	int num_running_ifaces;

	/* associated wpan interfaces, protected by rtnl or RCU */
	struct list_head wpan_dev_list;
	int devlist_generation, wpan_dev_id;

	/* must be last because of the way we do wpan_phy_priv(),
	 * and it should at least be aligned to NETDEV_ALIGN
	 */
	struct wpan_phy wpan_phy __aligned(NETDEV_ALIGN);
};

static inline struct cfg802154_registered_device *
wpan_phy_to_rdev(struct wpan_phy *wpan_phy)
{
	BUG_ON(!wpan_phy);
	return container_of(wpan_phy, struct cfg802154_registered_device,
			    wpan_phy);
}

/* free object */
void cfg802154_dev_free(struct cfg802154_registered_device *rdev);

#endif /* __IEEE802154_CORE_H */
