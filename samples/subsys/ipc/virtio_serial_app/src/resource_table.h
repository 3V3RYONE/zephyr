/*
 * Copyright (c) 2024 Your Name
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RESOURCE_TABLE_H_
#define RESOURCE_TABLE_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <openamp/open_amp.h>

/* VirtIO rpmsg device ID - for rproc_serial */
#define VDEV_ID                    0xFF
#define VRING_COUNT                2

/* VRING IDs (notifyid) */
#define VRING0_ID                  0
#define VRING1_ID                  1

/*
 * VRING addresses: Use magic value FW_RSC_ADDR_ANY to tell Linux
 * remoteproc to allocate the VRING memory dynamically.
 * Linux will fill in the actual addresses after allocation.
 */
#define FW_RSC_ADDR_ANY            0xFFFFFFFF
#define VRING_RX_ADDRESS           FW_RSC_ADDR_ANY
#define VRING_TX_ADDRESS           FW_RSC_ADDR_ANY
#define VRING_ALIGNMENT            0x1000  /* 4KB alignment for safety */
#define VRING_SIZE                 16  /* Number of descriptors in each ring */

#define RPMSG_IPU_C0_FEATURES      1

/* Resource table entries */
#define RSC_TABLE_NUM_ENTRY        1

/* VirtIO device features */
#ifndef VIRTIO_ID_RPROC_SERIAL
#define VIRTIO_ID_RPROC_SERIAL     11
#endif

struct fw_resource_table {
	struct resource_table table_hdr;
	uint32_t offset[RSC_TABLE_NUM_ENTRY];
	struct fw_rsc_vdev vdev;
	struct fw_rsc_vdev_vring vring0;
	struct fw_rsc_vdev_vring vring1;
} __packed;

/*
 * Resource table - will be modified by Linux remoteproc
 * Linux fills in the VRING addresses after allocation
 */
extern struct fw_resource_table resource_table;

/*
 * Helper functions to read Linux-allocated VRING addresses
 */
uint32_t rsc_table_get_vring_rx_addr(void);
uint32_t rsc_table_get_vring_tx_addr(void);
bool rsc_table_vrings_allocated(void);

#endif /* RESOURCE_TABLE_H_ */
