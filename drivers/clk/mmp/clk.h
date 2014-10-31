#ifndef __MACH_MMP_CLK_H
#define __MACH_MMP_CLK_H

#include <linux/clk-provider.h>
#include <linux/clkdev.h>

#define APBC_NO_BUS_CTRL	BIT(0)
#define APBC_POWER_CTRL		BIT(1)


/* Clock type "factor" */
struct mmp_clk_factor_masks {
	unsigned int factor;
	unsigned int num_mask;
	unsigned int den_mask;
	unsigned int num_shift;
	unsigned int den_shift;
};

struct mmp_clk_factor_tbl {
	unsigned int num;
	unsigned int den;
};

struct mmp_clk_factor {
	struct clk_hw hw;
	void __iomem *base;
	struct mmp_clk_factor_masks *masks;
	struct mmp_clk_factor_tbl *ftbl;
	unsigned int ftbl_cnt;
	spinlock_t *lock;
};

extern struct clk *mmp_clk_register_factor(const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *base, struct mmp_clk_factor_masks *masks,
		struct mmp_clk_factor_tbl *ftbl, unsigned int ftbl_cnt,
		spinlock_t *lock);

extern struct clk *mmp_clk_register_pll2(const char *name,
		const char *parent_name, unsigned long flags);
extern struct clk *mmp_clk_register_apbc(const char *name,
		const char *parent_name, void __iomem *base,
		unsigned int delay, unsigned int apbc_flags, spinlock_t *lock);
extern struct clk *mmp_clk_register_apmu(const char *name,
		const char *parent_name, void __iomem *base, u32 enable_mask,
		spinlock_t *lock);
#endif
