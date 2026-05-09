/*
 * Copyright (c) 2024 Your Name
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Simplified VirtIO Serial implementation for TI AM62x M4 core
 * using Zephyr's IPC service with static VRINGs
 *
 * This version is more practical and uses Zephyr's built-in infrastructure
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>

#include <openamp/open_amp.h>
#include "resource_table.h"

LOG_MODULE_REGISTER(virtio_serial, LOG_LEVEL_DBG);

/* Shared memory buffers for VRINGs (must be in shared memory region) */
static uint8_t vring_rx_buffer[VRING_SIZE * 512] __aligned(VRING_ALIGNMENT);
static uint8_t vring_tx_buffer[VRING_SIZE * 512] __aligned(VRING_ALIGNMENT);

/* VirtIO structures */
struct virtio_device vdev;
struct virtqueue *vq_rx;
struct virtqueue *vq_tx;
struct metal_io_region io_region;

/**
 * Callback when data is received from Linux
 */
static void rx_callback(struct virtqueue *vq)
{
	void *buffer;
	uint32_t len;
	int ret;

	LOG_INF("=== DATA RECEIVED FROM LINUX ===");

	/* Process all available buffers */
	while ((buffer = virtqueue_get_buffer(vq, &len)) != NULL) {
		if (len > 0) {
			LOG_INF("Received %u bytes from Linux", len);

			/* Log the received data */
			LOG_HEXDUMP_INF(buffer, MIN(len, 64), "Data:");

			/* Return buffer to the vring for reuse */
			ret = virtqueue_add_buffer(vq, buffer, 0, len, buffer);
			if (ret < 0) {
				LOG_ERR("Failed to return buffer: %d", ret);
			}
		}
	}

	/* Notify Linux that we've processed the buffers */
	virtqueue_kick(vq);
}

/**
 * Callback when Linux has consumed our TX data
 */
static void tx_callback(struct virtqueue *vq)
{
	void *buffer;
	uint32_t len;

	LOG_DBG("TX callback - Linux consumed data");

	/* Reclaim sent buffers */
	while ((buffer = virtqueue_get_buffer(vq, &len)) != NULL) {
		LOG_DBG("TX buffer returned: %p", buffer);
		/* Free or reuse buffer */
	}
}

/**
 * VirtIO notify function - trigger interrupt to Linux
 * On AM62x, this should use the mailbox subsystem
 */
static int notify_func(struct virtqueue *vq)
{
	LOG_DBG("Notify Linux about vring update");

	/* TODO: On real hardware, trigger mailbox interrupt here
	 * Example:
	 * struct device *mbox = device_get_binding("MAILBOX_0");
	 * mailbox_send(mbox, MAILBOX_CHANNEL, NULL);
	 */

	return 0;
}

/**
 * Main function
 */
int main(void)
{
	int ret;

	printk("\n");
	printk("===============================================\n");
	printk("  VirtIO Serial - Simple Implementation       \n");
	printk("  TI AM62x M4 Core with Zephyr RTOS            \n");
	printk("===============================================\n");
	printk("\n");

	LOG_INF("VirtIO Serial Device starting...");
	LOG_INF("Resource table: %p", &resource_table);
	LOG_INF("Device ID: VIRTIO_ID_RPROC_SERIAL (%d)", VIRTIO_ID_RPROC_SERIAL);

	/* Initialize libmetal */
	struct metal_init_params metal_params = METAL_INIT_DEFAULTS;
	ret = metal_init(&metal_params);
	if (ret) {
		LOG_ERR("Failed to init metal: %d", ret);
		return ret;
	}
	LOG_INF("LibMetal initialized");

	/* Setup IO region for shared memory */
	metal_io_init(&io_region, vring_rx_buffer,
	              (metal_phys_addr_t *)vring_rx_buffer,
	              sizeof(vring_rx_buffer) + sizeof(vring_tx_buffer),
	              (unsigned)-1, 0, NULL);

	/* Initialize VirtIO device */
	vdev.role = VIRTIO_DEV_DEVICE;  /* We are the device, Linux is driver */
	vdev.func = (void *)notify_func;

	/* Create RX virtqueue (Linux to M4) */
	vq_rx = virtqueue_create(&vdev, 0, "rx",
	                         VRING_SIZE, VRING_ALIGNMENT,
	                         vring_rx_buffer, &io_region,
	                         rx_callback);
	if (!vq_rx) {
		LOG_ERR("Failed to create RX virtqueue");
		return -ENOMEM;
	}
	LOG_INF("RX virtqueue created");

	/* Create TX virtqueue (M4 to Linux) */
	vq_tx = virtqueue_create(&vdev, 1, "tx",
	                         VRING_SIZE, VRING_ALIGNMENT,
	                         vring_tx_buffer, &io_region,
	                         tx_callback);
	if (!vq_tx) {
		LOG_ERR("Failed to create TX virtqueue");
		return -ENOMEM;
	}
	LOG_INF("TX virtqueue created");

	printk("\n");
	LOG_INF("===========================================");
	LOG_INF("  VirtIO Serial Device READY");
	LOG_INF("  Waiting for Linux to send data...");
	LOG_INF("===========================================");
	printk("\n");

	LOG_INF("Instructions:");
	LOG_INF("1. Boot Linux on A53 core");
	LOG_INF("2. Load this M4 firmware via remoteproc");
	LOG_INF("3. Linux will create /dev/vportXpY");
	LOG_INF("4. Write to device: echo 'test' > /dev/vportXpY");
	LOG_INF("5. Watch this console for received data!");

	/* Main loop - handle interrupts */
	while (1) {
		/* In a real implementation, this would be interrupt-driven
		 * For now, we can poll or just sleep
		 */
		k_sleep(K_SECONDS(10));
		LOG_DBG("Alive - waiting for data...");
	}

	return 0;
}
