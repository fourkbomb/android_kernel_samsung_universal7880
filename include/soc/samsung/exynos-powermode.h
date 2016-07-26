/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS - PMU(Power Management Unit) support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __EXYNOS_POWERMODE_H
#define __EXYNOS_POWERMODE_H __FILE__

/**
 * System power down mode
 */
enum sys_powerdown {
        SYS_SICD,
        SYS_SICD_CPD,
        SYS_AFTR,
        SYS_STOP,
        SYS_DSTOP,
        SYS_LPD,
        SYS_ALPA,
        SYS_SLEEP,
        NUM_SYS_POWERDOWN,
};

/**
  IDLE_IP control
 */
extern void exynos_update_pd_idle_status(int index, int idle);
extern void exynos_update_ip_idle_status(int index, int idle);
extern int exynos_get_idle_ip_index(const char *name);

#endif /* __EXYNOS_POWERMODE_H */
