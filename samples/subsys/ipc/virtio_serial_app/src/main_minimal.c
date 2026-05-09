/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal VirtIO Serial application for TI AM62x M4
 * This is a skeleton that compiles successfully
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>

#include "resource_table.h"

LOG_MODULE_REGISTER(virtio_serial, LOG_LEVEL_DBG);

int main(void)
{
	uint32_t vring_rx_addr, vring_tx_addr;
	int timeout = 100;

	printk("\n");
	printk("===============================================\n");
	printk("  VirtIO Serial Device - Zephyr on AM62x M4   \n");
	printk("===============================================\n");
	printk("\n");

	LOG_INF("Application started on M4 core");
	LOG_INF("Resource table at: %p", &resource_table);
	LOG_INF("VirtIO Device ID: VIRTIO_ID_RPROC_SERIAL (%lu)", (unsigned long)VIRTIO_ID_RPROC_SERIAL);
	LOG_INF("Number of VRINGs: %d", VRING_COUNT);
	LOG_INF("Initial VRING addresses (before Linux allocation):");
	LOG_INF("  RX: 0x%08lX (FW_RSC_ADDR_ANY = magic value)", (unsigned long)VRING_RX_ADDRESS);
	LOG_INF("  TX: 0x%08lX (FW_RSC_ADDR_ANY = magic value)", (unsigned long)VRING_TX_ADDRESS);
	LOG_INF("Linux will allocate actual addresses and update resource table");

	printk("\n");
	LOG_INF("Waiting for Linux to allocate VRING memory...");

	/* Wait for Linux to allocate VRING addresses */
	while (!rsc_table_vrings_allocated() && timeout > 0) {
		k_sleep(K_MSEC(100));
		timeout--;
	}

	if (!rsc_table_vrings_allocated()) {
		LOG_ERR("Timeout waiting for Linux VRING allocation");
		LOG_ERR("RX addr: 0x%08lX, TX addr: 0x%08lX",
		        (unsigned long)rsc_table_get_vring_rx_addr(),
		        (unsigned long)rsc_table_get_vring_tx_addr());
		LOG_WRN("This is normal if Linux hasn't loaded the firmware yet");
	} else {
		/* Get the addresses Linux allocated */
		vring_rx_addr = rsc_table_get_vring_rx_addr();
		vring_tx_addr = rsc_table_get_vring_tx_addr();

		printk("\n");
		LOG_INF("===========================================");
		LOG_INF("Linux allocated VRINGs:");
		LOG_INF("  RX VRING: 0x%08lX", (unsigned long)vring_rx_addr);
		LOG_INF("  TX VRING: 0x%08lX", (unsigned long)vring_tx_addr);
		LOG_INF("===========================================");
		printk("\n");

		LOG_INF("VirtIO Serial device ready!");
		LOG_INF("To complete: Implement vring initialization and callbacks");
	}

	/* Main loop */
	while (1) {
		k_sleep(K_SECONDS(10));
		LOG_DBG("Still running - waiting for full implementation...");
	}

	return 0;
}
