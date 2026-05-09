/*
 * Copyright (c) 2024 Your Name
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "resource_table.h"

/*
 * Resource table definition for Linux remoteproc
 *
 * This table is placed in a special section (.resource_table) that Linux
 * remoteproc can discover. It describes the virtio serial device and its
 * two vrings (RX and TX).
 *
 * IMPORTANT: The VRING addresses are initialized with FW_RSC_ADDR_ANY (0xFFFFFFFF).
 * This tells Linux remoteproc to allocate the VRING memory dynamically.
 * After Linux loads the firmware and allocates memory, it will update these
 * addresses in the resource table. The M4 can then read the actual addresses
 * from this table.
 */
struct fw_resource_table __attribute__((section(".resource_table"))) resource_table = {
	/* Resource table header */
	.table_hdr = {
		.ver = 1,
		.num = RSC_TABLE_NUM_ENTRY,
		.reserved = {0, 0},
	},

	/* Offsets to resource entries */
	.offset = {
		offsetof(struct fw_resource_table, vdev),
	},

	/* VirtIO device entry - RPROC_SERIAL */
	.vdev = {
		.type = RSC_VDEV,
		.id = VIRTIO_ID_RPROC_SERIAL,  /* Device ID 11 for rproc_serial */
		.notifyid = VDEV_ID,
		.dfeatures = RPMSG_IPU_C0_FEATURES,
		.gfeatures = 0,
		.config_len = 0,
		.status = 0,
		.num_of_vrings = VRING_COUNT,
		.reserved = {0, 0},
	},

	/* VRING 0 - Linux RX (M4 TX) - First vring allocated by Linux */
	.vring0 = {
		.da = VRING_TX_ADDRESS,  /* M4 sends data here */
		.align = VRING_ALIGNMENT,
		.num = VRING_SIZE,
		.notifyid = 0,
		.reserved = 0,
	},

	/* VRING 1 - Linux TX (M4 RX) - Second vring allocated by Linux */
	.vring1 = {
		.da = VRING_RX_ADDRESS,  /* M4 receives data from here */
		.align = VRING_ALIGNMENT,
		.num = VRING_SIZE,
		.notifyid = 1,
		.reserved = 0,
	},
};

/*
 * Helper function to get the M4 RX VRING address after Linux has allocated it
 * Returns vring1.da because Linux allocates vring1 for its TX (M4's RX)
 */
uint32_t rsc_table_get_vring_rx_addr(void)
{
	return resource_table.vring1.da;
}

/*
 * Helper function to get the M4 TX VRING address after Linux has allocated it
 * Returns vring0.da because Linux allocates vring0 for its RX (M4's TX)
 */
uint32_t rsc_table_get_vring_tx_addr(void)
{
	return resource_table.vring0.da;
}

/*
 * Check if Linux has allocated the VRING addresses
 * Returns true if addresses are valid (not FW_RSC_ADDR_ANY)
 */
bool rsc_table_vrings_allocated(void)
{
	return (resource_table.vring0.da != FW_RSC_ADDR_ANY &&
	        resource_table.vring1.da != FW_RSC_ADDR_ANY);
}
