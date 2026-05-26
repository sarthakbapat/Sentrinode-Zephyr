#include "nvs_manager.h"

/* ---------------------------- NVS functions ------------------------------------- */


int init_nvs(void) 
{
    int rc = 0;
    struct flash_pages_info info;

    const struct device *flash_dev = FIXED_PARTITION_DEVICE(storage_partition);

    if (!device_is_ready(flash_dev)) {
        printk("Flash device not ready\n");
        return;
    }


    fs.flash_device = flash_dev;
    fs.offset = FIXED_PARTITION_OFFSET(storage_partition);

    // Get flash page info
    rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
    if (rc) {
        printk("Flash page info failed: %d\n", rc);
        return rc;
    }

    fs.sector_size = info.size;
    fs.sector_count = 3;            // Using 3 pages

    rc = nvs_mount(&fs);
	if (rc) {
		printk("Flash Init failed, rc=%d\n", rc);
		return rc;
	}
}

int nvs_write_data(uint16_t id, const void *data, size_t len)
{
    int rc;
    rc = nvs_write(&fs, id, data, len);
    if (rc < 0) {
        printk("NVS write error %d\n", rc);
    }
    else {
        printk("NVS write successful, bytes written %d, value written %d\n", len, data);
    }
    return rc;
}

int nvs_read_data(uint16_t id, void *data, size_t len)
{
    int rc;
    rc = nvs_read(&fs, id, data, len);
    if (rc < 0) {
        printk("NVS: No previous count found, starting at 0\n");
    }
    return rc;
}

int nvs_stress_test() 
{
    int rc;
    uint8_t buf[256];

    memset(buf, 0x3c, sizeof(buf));
    printk("--- Starting NVS Stress Test ---\n");

    for (int i = 0; i < 10; i++) {
        // We keep writing to the SAME ID. 
        // NVS will keep appending new versions and "hiding" old ones.
        buf[0] = (uint8_t)i;
        rc = nvs_write(&fs, i, buf, sizeof(buf));
        
        if (rc < 0) {
            printk("Write failed at iteration %d: %d\n", i, rc);
            break;
        }

        // Get info about the filesystem to see it filling up
        ssize_t free_space = nvs_calc_free_space(&fs);
        
        printk("[%d] Write successful. Estimated free space: %zd bytes\n", i, free_space);
    }
    printk("--- Stress Test Complete ---\n");
}

/* ---------------------------- NVS functions end ------------------------------------- */
