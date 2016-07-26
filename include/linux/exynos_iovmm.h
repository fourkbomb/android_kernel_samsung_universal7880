/* linux/arch/arm/plat-s5p/include/plat/iovmm.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_PLAT_IOVMM_H
#define __ASM_PLAT_IOVMM_H

#ifdef CONFIG_EXYNOS_IOVMM
#include <linux/dma-direction.h>
#include <linux/iommu.h>

struct scatterlist;
struct device;

int iovmm_activate(struct device *dev);
void iovmm_deactivate(struct device *dev);

/* iovmm_map() - Maps a list of physical memory chunks
 * @dev: the owner of the IO address space where the mapping is created
 * @sg: list of physical memory chunks to map
 * @offset: length in bytes where the mapping starts
 * @size: how much memory to map in bytes. @offset + @size must not exceed
 *        total size of @sg
 * @direction: dma data direction for iova
 * @id: From where iovmm allocates iova
 *
 * This function returns mapped IO address in the address space of @dev.
 * Returns minus error number if mapping fails.
 * Caller must check its return code with IS_ERROR_VALUE() if the function
 * succeeded.
 *
 * The caller of this function must ensure that iovmm_cleanup() is not called
 * while this function is called.
 *
 */
dma_addr_t iovmm_map(struct device *dev, struct scatterlist *sg, off_t offset,
		size_t size, enum dma_data_direction direction, int id);

/* iovmm_unmap() - unmaps the given IO address
 * @dev: the owner of the IO address space where @iova belongs
 * @iova: IO address that needs to be unmapped and freed.
 *
 * The caller of this function must ensure that iovmm_cleanup() is not called
 * while this function is called.
 */
void iovmm_unmap(struct device *dev, dma_addr_t iova);

/* iovmm_map_oto - create one to one mapping for the given physical address
 * @dev: the owner of the IO address space to map
 * @phys: physical address to map
 * @size: size of the mapping to create
 *
 * This function return 0 if mapping is successful. Otherwise, minus error
 * value.
 */
int iovmm_map_oto(struct device *dev, phys_addr_t phys, size_t size);

/* iovmm_unmap_oto - remove one to one mapping
 * @dev: the owner ofthe IO address space
 * @phys: physical address to remove mapping
 */
void iovmm_unmap_oto(struct device *dev, phys_addr_t phys);

int exynos_create_iovmm(struct device *dev, int inplanes, int onplanes);

#define SYSMMU_FAULT_BITS	4
#define SYSMMU_FAULT_SHIFT	16
#define SYSMMU_FAULT_MASK	((1 << SYSMMU_FAULT_BITS) - 1)
#define SYSMMU_FAULT_FLAG(id) (((id) & SYSMMU_FAULT_MASK) << SYSMMU_FAULT_SHIFT)
#define SYSMMU_FAULT_ID(fg)   (((fg) >> SYSMMU_FAULT_SHIFT) & SYSMMU_FAULT_MASK)

#define SYSMMU_FAULT_PTW_ACCESS   0
#define SYSMMU_FAULT_PAGE_FAULT   1
#define SYSMMU_FAULT_TLB_MULTIHIT 2
#define SYSMMU_FAULT_ACCESS       3
#define SYSMMU_FAULT_SECURITY     4
#define SYSMMU_FAULT_UNKNOWN      5

#define IOMMU_FAULT_EXYNOS_PTW_ACCESS SYSMMU_FAULT_FLAG(SYSMMU_FAULT_PTW_ACCESS)
#define IOMMU_FAULT_EXYNOS_PAGE_FAULT SYSMMU_FAULT_FLAG(SYSMMU_FAULT_PAGE_FAULT)
#define IOMMU_FAULT_EXYNOS_TLB_MULTIHIT \
				SYSMMU_FAULT_FLAG(SYSMMU_FAULT_TLB_MULTIHIT)
#define IOMMU_FAULT_EXYNOS_ACCESS     SYSMMU_FAULT_FLAG(SYSMMU_FAULT_ACCESS)
#define IOMMU_FAULT_EXYNOS_SECURITY   SYSMMU_FAULT_FLAG(SYSMMU_FAULT_SECURITY)
#define IOMMU_FAULT_EXYNOS_UNKNOWN    SYSMMU_FAULT_FLAG(SYSMMU_FAULT_UNKOWN)

/*
 * iovmm_set_fault_handler - register fault handler of dev to iommu controller
 * @dev: the device that wants to register fault handler
 * @handler: fault handler
 * @token: any data the device driver needs to get when fault occurred
 */
void iovmm_set_fault_handler(struct device *dev,
			     iommu_fault_handler_t handler, void *token);
#else
#define iovmm_activate(dev)		(-ENOSYS)
#define iovmm_deactivate(dev)		do { } while (0)
#define iovmm_map(dev, sg, offset, size) (-ENOSYS)
#define iovmm_unmap(dev, iova)		do { } while (0)
#define iovmm_map_oto(dev, phys, size)	(-ENOSYS)
#define iovmm_unmap_oto(dev, phys)	do { } while (0)
#define exynos_create_iovmm(sysmmu, inplanes, onplanes) 0
#define iovmm_set_fault_handler(dev, handler, token) do { } while (0)
#endif /* CONFIG_EXYNOS_IOVMM */

#endif /*__ASM_PLAT_IOVMM_H*/
