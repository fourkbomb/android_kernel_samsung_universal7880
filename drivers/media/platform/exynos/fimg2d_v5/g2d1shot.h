/* linux/drivers/media/platform/exynos/fimg2d_v5/g2d1shot.h
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *	http://www.samsung.com/
 *
 * Samsung Graphics 2D driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __EXYNOS_G2D1SHOT_H_
#define __EXYNOS_G2D1SHOT_H_

#include <linux/videodev2.h>
#include <media/m2m1shot2.h>

#define MODULE_NAME	"exynos-g2d"
#define NODE_NAME	"fimg2d"

#define	G2D_MAX_PLANES	1

#define G2D_MAX_NORMAL_SIZE	16383
#define G2D_MAX_COMP_SIZE	8192

/* flags for G2D supported format */
#define G2D_FMT_FLAG_SUPPORT_COMP	(1 << 0)

struct g2d1shot_fmt {
	char	*name;
	u32	pixelformat;
	u8	bpp[G2D_MAX_PLANES];
	u8	num_planes;
	u8	flag;
};

struct g2d1shot_dev {
	struct m2m1shot2_device *oneshot2_dev;
	struct device *dev;
	struct clk *clock;
	void __iomem *reg;

	u32 version;
	unsigned long state;
};

struct g2d1shot_ctx {
	struct g2d1shot_dev *g2d_dev;
};

#endif /* __EXYNOS_G2D1SHOT_H_ */
