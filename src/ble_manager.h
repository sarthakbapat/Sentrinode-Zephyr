#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

// Define Bluetooth UUIDS
#define BT_UUID_ESS_VAL 0x181a
#define BT_UUID_TEMP_VAL 0x2a6e
#define BT_UUID_HUM_VAL 0x2a6f

#define BT_UUID_SENTRI_SEC_VAL BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
#define BT_UUID_SENTRI_TAMPER_STATUS 0x9901
#define BT_UUID_SENTRI_TAMPER_COUNT 0x9902

extern const struct bt_uuid_16 ess_uuid;
extern const struct bt_uuid_16 temp_uuid;
extern const struct bt_uuid_16 hum_uuid;

extern const struct bt_uuid_128 sentri_sec_uuid;
extern const struct bt_uuid_16 sentri_tamper_status;
extern const struct bt_uuid_16 sentri_tamper_count;

extern int16_t ble_temp;
extern uint16_t ble_hum;
extern uint8_t  tamper_status; // 0: OK, 1: ALERT
extern atomic_t tamper_total_count;

ssize_t read_temp(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                  void *buf, uint16_t len, uint16_t offset);

ssize_t read_hum(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                 void *buf, uint16_t len, uint16_t offset);

ssize_t read_tamper_status(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           void *buf, uint16_t len, uint16_t offset);

ssize_t read_tamper_count(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                          void *buf, uint16_t len, uint16_t offset);
                        
/* Callback for the CCCD, enable/disable notifications from the phone, can be used to control sensor power */
void tamper_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value);


void connected(struct bt_conn *conn, uint8_t conn_err);

void disconnected(struct bt_conn *conn, uint8_t reason);

void recycled(void);

void ble_notify_temp_hum(int16_t temp, uint16_t hum);

void ble_notify_tamper(uint8_t tamper_status, uint32_t tamper_count);

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    /* Advertise the 16-bit Service UUID (0x181A) */
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_ESS_VAL)),
    /* Advertise the 128-bit Service UUID */
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_SENTRI_SEC_VAL),
};

/* Packet 2: The "Extra Info" (The Name) */
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

#endif

