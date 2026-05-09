/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * VirtIO Serial application for TI AM62x M4 core
 * Adapted from RPMsg-TTY design to use VirtIO APIs directly
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/mbox.h>
#include <string.h>

#include <openamp/open_amp.h>
#include <metal/sys.h>
#include <metal/io.h>

#include "resource_table.h"

LOG_MODULE_REGISTER(virtio_serial, LOG_LEVEL_DBG);

/* Metal and VirtIO structures */
static struct metal_io_region shm_io_data;
static struct metal_io_region rsc_io_data;
static struct metal_io_region *shm_io = &shm_io_data;
static struct metal_io_region *rsc_io = &rsc_io_data;

static struct virtio_device *vdev;
static struct virtqueue *vq_rx;  /* RX: Linux to M4 */
static struct virtqueue *vq_tx;  /* TX: M4 to Linux */

/* Message structure to hold received data */
struct virtio_rcv_msg {
	void *data;
	uint32_t len;
	uint16_t idx;
};

static struct virtio_rcv_msg tty_msg;

/* Semaphores - matching RPMsg design */
static K_SEM_DEFINE(data_sem, 0, 1);      /* For mailbox interrupt */
static K_SEM_DEFINE(data_tty_sem, 0, 1);  /* For TTY data */

/* Mailbox device */
static const struct device *mbox_dev = DEVICE_DT_GET(DT_NODELABEL(mbox0));
#define MBOX_RX_CHANNEL 1
#define MBOX_TX_CHANNEL 0

/* Thread stacks */
#define APP_TASK_STACK_SIZE (1024)
#define APP_TTY_TASK_STACK_SIZE (1536)

K_THREAD_STACK_DEFINE(thread_mng_stack, APP_TASK_STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_tty_stack, APP_TTY_TASK_STACK_SIZE);

static struct k_thread thread_mng_data;
static struct k_thread thread_tty_data;

/**
 * Mailbox callback - matches platform_ipm_callback from RPMsg
 */
static void platform_mbox_callback(const struct device *dev, uint32_t channel,
				   void *user_data, struct mbox_msg *msg)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	k_sem_give(&data_sem);
}

/**
 * VirtIO RX callback - matches rpmsg_recv_tty_callback
 * Holds buffer and signals TTY task
 */
static void virtio_serial_rx_callback(struct virtqueue *vq)
{
	void *buffer;
	uint32_t len;
	uint16_t idx;

	if (!vq) {
		return;
	}

	/* Get one buffer - "hold" pattern like RPMsg */
	buffer = virtqueue_get_available_buffer(vq, &idx, &len);
	if (buffer && len > 0) {
		/* Hold buffer - store for deferred processing */
		tty_msg.data = buffer;
		tty_msg.len = len;
		tty_msg.idx = idx;

		/* Signal TTY task */
		k_sem_give(&data_tty_sem);
	}
}

/**
 * VirtIO TX callback - not used in echo pattern
 */
static void virtio_serial_tx_callback(struct virtqueue *vq)
{
	/* TX buffer reclamation handled by Linux polling */
}

/**
 * Mailbox notify function - sends interrupt to Linux
 */
static int mailbox_notify(void *priv, uint32_t id)
{
	ARG_UNUSED(priv);


	struct mbox_msg msg = {
		.data = &id,
		.size = sizeof(id),
	};

	int ret = mbox_send(mbox_dev, MBOX_TX_CHANNEL, &msg);
	if (ret < 0) {
		//LOG_ERR("Failed to send mailbox message: %d", ret);
		return ret;
	}

	return 0;
}

/**
 * Receive message function - matches RPMsg receive_message
 * Called by management task to process interrupts
 */
static void receive_message(unsigned char **msg, unsigned int *len)
{
	int status = k_sem_take(&data_sem, K_FOREVER);

	if (status == 0) {
		/* Notify virtio framework - this triggers rx_callback */
		rproc_virtio_notified(vdev, VRING1_ID);
	}
}

/**
 * Platform initialization - matches RPMsg platform_init
 */
static int platform_init(void)
{
	struct metal_init_params metal_params = METAL_INIT_DEFAULTS;
	uint32_t vring_rx_addr, vring_tx_addr;
	int ret;

	//LOG_INF("Initializing platform");

	/* Initialize Metal */
	ret = metal_init(&metal_params);
	if (ret) {
		//LOG_ERR("metal_init failed: %d", ret);
		return -1;
	}

	/* Wait for Linux to allocate VRING addresses */
	//LOG_INF("Waiting for Linux to allocate VRING memory...");
	int timeout = 100;
	while (!rsc_table_vrings_allocated() && timeout > 0) {
		k_sleep(K_MSEC(100));
		timeout--;
	}

	if (!rsc_table_vrings_allocated()) {
		LOG_ERR("Timeout waiting for Linux VRING allocation");
		return -1;
	}

	vring_rx_addr = rsc_table_get_vring_rx_addr();
	vring_tx_addr = rsc_table_get_vring_tx_addr();

	//LOG_INF("Linux allocated VRINGs: RX=0x%08X, TX=0x%08X",
		//vring_rx_addr, vring_tx_addr);

	/* Initialize shared memory IO region */
	metal_phys_addr_t shm_physmap = vring_rx_addr;
	metal_io_init(shm_io, (void *)vring_rx_addr, &shm_physmap,
		      0x20000, -1, 0, NULL);

	/* Initialize resource table IO region */
	metal_phys_addr_t rsc_tab_physmap = (uintptr_t)&resource_table;
	metal_io_init(rsc_io, (void *)&resource_table, &rsc_tab_physmap,
		      sizeof(resource_table), -1, 0, NULL);

	/* Setup mailbox */
	if (!device_is_ready(mbox_dev)) {
		LOG_ERR("Mailbox device not ready");
		return -1;
	}

	ret = mbox_register_callback(mbox_dev, MBOX_RX_CHANNEL,
				     platform_mbox_callback, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to register mailbox callback: %d", ret);
		return -1;
	}

	ret = mbox_set_enabled(mbox_dev, MBOX_RX_CHANNEL, true);
	if (ret < 0) {
		LOG_ERR("Failed to enable mailbox: %d", ret);
		return -1;
	}

	//LOG_INF("Mailbox initialized: RX=%d, TX=%d", MBOX_RX_CHANNEL, MBOX_TX_CHANNEL);

	return 0;
}

/**
 * Cleanup - matches RPMsg cleanup_system
 */
static void cleanup_system(void)
{
	mbox_set_enabled(mbox_dev, MBOX_RX_CHANNEL, false);
	if (vdev) {
		rproc_virtio_remove_vdev(vdev);
	}
	metal_finish();
}

/**
 * Create VirtIO device - matches RPMsg platform_create_rpmsg_vdev
 */
static int platform_create_virtio_vdev(void)
{
	uint32_t vring_rx_addr, vring_tx_addr;
	int ret;

	//LOG_INF("Creating VirtIO device");

	/* Create VirtIO device */
	vdev = rproc_virtio_create_vdev(VIRTIO_DEV_DEVICE, VDEV_ID,
					&resource_table.vdev,
					rsc_io, NULL, mailbox_notify, NULL);
	if (!vdev) {
		LOG_ERR("Failed to create vdev");
		return -1;
	}

	/* Wait for Linux driver to be ready */
	//LOG_INF("Waiting for Linux virtio_console driver...");
	rproc_virtio_wait_remote_ready(vdev);
	LOG_INF("Linux driver is ready");

	vring_rx_addr = rsc_table_get_vring_rx_addr();
	vring_tx_addr = rsc_table_get_vring_tx_addr();

	/* Initialize VRING 0: M4 TX (Linux RX) */
	ret = rproc_virtio_init_vring(vdev, 0, resource_table.vring0.notifyid,
				      (void *)vring_tx_addr, shm_io,
				      resource_table.vring0.num,
				      resource_table.vring0.align);
	if (ret) {
		LOG_ERR("Failed to init TX vring: %d", ret);
		goto failed;
	}

	/* Initialize VRING 1: M4 RX (Linux TX) */
	ret = rproc_virtio_init_vring(vdev, 1, resource_table.vring1.notifyid,
				      (void *)vring_rx_addr, shm_io,
				      resource_table.vring1.num,
				      resource_table.vring1.align);
	if (ret) {
		LOG_ERR("Failed to init RX vring: %d", ret);
		goto failed;
	}

	/* Create virtqueues */
	const char *vq_names[] = {"tx_vq", "rx_vq"};
	vq_callback vq_cbs[] = {virtio_serial_tx_callback, virtio_serial_rx_callback};

	ret = virtio_create_virtqueues(vdev, 0, VRING_COUNT, vq_names, vq_cbs, NULL);
	if (ret) {
		LOG_ERR("Failed to create virtqueues: %d", ret);
		goto failed;
	}

	/* Get virtqueue pointers */
	vq_tx = vdev->vrings_info[0].vq;
	vq_rx = vdev->vrings_info[1].vq;

	if (!vq_rx || !vq_tx) {
		LOG_ERR("Failed to get virtqueue pointers");
		ret = -1;
		goto failed;
	}

	/* Set shared memory I/O region for buffer access */
	vq_rx->shm_io = shm_io;
	vq_tx->shm_io = shm_io;

	//LOG_INF("VirtIO device initialized successfully");

	return 0;

failed:
	if (vdev) {
		rproc_virtio_remove_vdev(vdev);
	}
	return ret;
}

/**
 * TTY task - matches app_rpmsg_tty exactly
 */
void app_virtio_tty(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	void *tx_buffer;
	uint32_t tx_len;
	uint16_t tx_idx;

	/* Wait for management task to signal us */
	k_sem_take(&data_tty_sem, K_FOREVER);

//	LOG_INF("VirtIO TTY task started");

	/* Main loop - process messages one at a time */
	while (1) {
		k_sem_take(&data_tty_sem, K_FOREVER);

		if (tty_msg.len > 0 && tty_msg.data) {
			/* Get TX buffer */
			tx_buffer = virtqueue_get_available_buffer(vq_tx, &tx_idx, &tx_len);
			if (tx_buffer) {
				/* Echo: copy RX data to TX buffer */
				uint32_t copy_len = (tty_msg.len < tx_len) ?
						    tty_msg.len : tx_len;
				metal_io_block_write(shm_io,
						     metal_io_virt_to_offset(shm_io, tx_buffer),
						     tty_msg.data, copy_len);

				/* Send to Linux */
				virtqueue_add_consumed_buffer(vq_tx, tx_idx, copy_len);
				virtqueue_kick(vq_tx);
			}

			/* Release RX buffer - matching rpmsg_release_rx_buffer behavior */
			virtqueue_add_consumed_buffer(vq_rx, tty_msg.idx, tty_msg.len);
			virtqueue_kick(vq_rx);
		}

		/* Clear message */
		tty_msg.len = 0;
		tty_msg.data = NULL;
	}

	//LOG_INF("VirtIO TTY task ended");
}

/**
 * Management task - matches rpmsg_mng_task exactly
 */
void virtio_mng_task(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	unsigned char *msg;
	unsigned int len;
	int ret = 0;

	//LOG_INF("VirtIO management task started");

	/* Initialize platform */
	ret = platform_init();
	if (ret) {
		LOG_ERR("Failed to initialize platform");
		ret = -1;
		goto task_end;
	}

	/* Create VirtIO device */
	ret = platform_create_virtio_vdev();
	if (ret) {
		LOG_ERR("Failed to create VirtIO device");
		ret = -1;
		goto task_end;
	}

	/* Start the TTY task */
	k_sem_give(&data_tty_sem);

	/* Main loop - process interrupts */
	while (1) {
		receive_message(&msg, &len);
	}

task_end:
	cleanup_system();
	//LOG_INF("VirtIO management task ended");
}

/**
 * Main entry point - matches RPMsg main exactly
 */
int main(void)
{
	printk("\n");
	printk("===============================================\n");
	printk("  VirtIO Serial - Zephyr on AM62x M4          \n");
	printk("===============================================\n");
	printk("\n");

//	LOG_INF("Starting application threads!");

	/* Create management task */
	k_thread_create(&thread_mng_data, thread_mng_stack, APP_TASK_STACK_SIZE,
			virtio_mng_task,
			NULL, NULL, NULL, K_PRIO_COOP(8), 0, K_NO_WAIT);

	/* Create TTY task */
	k_thread_create(&thread_tty_data, thread_tty_stack, APP_TTY_TASK_STACK_SIZE,
			app_virtio_tty,
			NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);

	return 0;
}
