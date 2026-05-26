#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/types.h>

#define NVS_TAMPER_COUNT_ID 1

static struct nvs_fs fs;

int init_nvs(void);
int nvs_write_data(uint16_t id, const void *data, size_t len);
int nvs_read_data(uint16_t id, void *data, size_t len);

#endif

