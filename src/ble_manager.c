#include "ble_manager.h"

int16_t ble_temp = 0;
uint16_t ble_hum = 0;
uint8_t  tamper_status = 0; // 0: OK, 1: ALERT
atomic_t tamper_total_count = 0;

const struct bt_uuid_16 ess_uuid = BT_UUID_INIT_16(BT_UUID_ESS_VAL);
const struct bt_uuid_16 temp_uuid = BT_UUID_INIT_16(BT_UUID_TEMP_VAL);
const struct bt_uuid_16 hum_uuid = BT_UUID_INIT_16(BT_UUID_HUM_VAL);

const struct bt_uuid_128 sentri_sec_uuid = BT_UUID_INIT_128(BT_UUID_SENTRI_SEC_VAL);
const struct bt_uuid_16 sentri_tamper_status = BT_UUID_INIT_16(BT_UUID_SENTRI_TAMPER_STATUS);
const struct bt_uuid_16 sentri_tamper_count = BT_UUID_INIT_16(BT_UUID_SENTRI_TAMPER_COUNT);


/* -------------------------- Bluetooth related callbacks ------------------------------------------------------- */

/* Callback: Triggered when a remote device tries to READ the temp value */
ssize_t read_temp(struct bt_conn *conn, const struct bt_gatt_attr *attr,
    void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &ble_temp, sizeof(ble_temp));
}

/* Callback: Triggered when a remote device tries to READ the humidity value */
ssize_t read_hum(struct bt_conn *conn, const struct bt_gatt_attr *attr,
    void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &ble_hum, sizeof(ble_temp));
}

/* Callback: Triggered when a remote device tries to READ the tamper_status value */
ssize_t read_tamper_status(struct bt_conn *conn, const struct bt_gatt_attr *attr,
    void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &tamper_status, sizeof(tamper_status));
}

/* Callback: Triggered when a remote device tries to READ the tamper_count value */
ssize_t read_tamper_count(struct bt_conn *conn, const struct bt_gatt_attr *attr,
    void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &tamper_total_count, sizeof(tamper_total_count));
}

/* Callback for the CCCD, enable/disable notifications from the phone, can be used to control sensor power */
void tamper_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);
    printk("Tamper Notifications %s\n", notif_enabled ? "Enabled" : "Disabled");
}

/* --------------------------------------- Bluetooth callbacks end ------------------------------------------------ */

BT_GATT_SERVICE_DEFINE(ess_sentri_svc,
    BT_GATT_PRIMARY_SERVICE(&ess_uuid),
    BT_GATT_CHARACTERISTIC(&temp_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           read_temp, NULL, &ble_temp),
    BT_GATT_CHARACTERISTIC(&hum_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           read_hum, NULL, &ble_hum),
    
    // Security service Read + Notify
    BT_GATT_PRIMARY_SERVICE(&sentri_sec_uuid),
    BT_GATT_CHARACTERISTIC(&sentri_tamper_status.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           read_tamper_status, NULL, &tamper_status),
    BT_GATT_CCC(tamper_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(&sentri_tamper_count.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           read_tamper_count, NULL, &tamper_total_count),     
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),               
);

/* BLE Connection callbacks */

void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err != BT_HCI_ERR_SUCCESS) {
		printk("Failed to connect to %s (%u)\n", addr, conn_err);
		return;
	}

	printk("Connected: %s\n", addr);
}

void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);
}

void recycled(void)
{
    int err;
    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err != 0) {
		printk("Advertising failed to start (err %d)\n", err);
	}
}

// Register the connection callbacks
BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
};

/* BLE Connection Callbacks end */

void ble_notify_temp_hum(int16_t temp, uint16_t hum)
{
    bt_gatt_notify(NULL, &ess_sentri_svc.attrs[1], &ble_temp, sizeof(ble_temp));
    bt_gatt_notify(NULL, &ess_sentri_svc.attrs[1], &ble_hum, sizeof(ble_hum));
}

void ble_notify_tamper(uint8_t tamper_status, uint32_t tamper_count)
{
    const struct bt_gatt_attr *tamper_stat_attr = bt_gatt_find_by_uuid(ess_sentri_svc.attrs, 
        ess_sentri_svc.attr_count, 
        &sentri_tamper_status.uuid);
    bt_gatt_notify(NULL, tamper_stat_attr, &tamper_status, sizeof(tamper_status));

    const struct bt_gatt_attr *tamper_count_attr = bt_gatt_find_by_uuid(ess_sentri_svc.attrs, 
             ess_sentri_svc.attr_count, 
             &sentri_tamper_count.uuid);
    bt_gatt_notify(NULL, tamper_count_attr, &tamper_count, sizeof(tamper_count));
}