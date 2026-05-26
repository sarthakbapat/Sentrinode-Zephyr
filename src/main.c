#include <math.h>
#include <zephyr/sys/atomic.h>
#include "ble_manager.h"
#include "nvs_manager.h"
#include <zephyr/drivers/gpio.h>

#define SECURITY_PRIO 5     // Higher priority
#define ENV_PRIO      7
#define STACK_SIZE    1024

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

K_SEM_DEFINE(tamper_sem, 0, 1); // Starts at 0, max 1

static int lsm6dsl_trig_cnt = 0;

enum system_state {
    STATE_INITIALIZING,
    STATE_SENSING,
    STATE_TAMPER
};

enum system_event {
    EVENT_NONE,
    EVENT_SENSOR_UPDATE,
    EVENT_TAMPER_DETECTED
};

// Define the system event queue.
struct system_event_queue {
    enum system_event type;
};

K_MSGQ_DEFINE(sys_event_q, sizeof(struct system_event_queue), 10, 4);

void env_thread_entry(void *p1, void *p2, void *p3) 
{
    /* const struct device *dev = (const struct device *)p1;
    struct sensor_value temperature, humidity;
    int ret; */
    while(1)
    {
        struct system_event_queue evt = { .type = EVENT_SENSOR_UPDATE };
        k_msgq_put(&sys_event_q, &evt, K_NO_WAIT);
        // hts221 logic here, polling mode for hts221.
        /* ret = sensor_sample_fetch(dev);
        sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temperature);
        sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &humidity);

        double temp = sensor_value_to_double(&temperature);
        double hum = sensor_value_to_double(&humidity);
        ble_temp = temp;
        ble_hum = hum;

        ble_notify_temp_hum(ble_temp, ble_hum); */

        // printk("Temp: %.2f deg celsius.\n", temp);
        // printk("Humidity: %.2f percent.\n", hum);

        k_sleep(K_MSEC(5000));
    }
}

void security_thread_entry(void *p1, void *p2, void *p3) 
{
    const struct device *dev = (const struct device *)p1;
    char out_str[64];
    static struct sensor_value accel_x, accel_y, accel_z;
	static struct sensor_value gyro_x, gyro_y, gyro_z;
    const double GRAVITY = 9.81;
    const double THRESHOLD = 2.5;
    int ret;
    while(1)
    {
        // lsm6dsl accelerometer logic for vibration, ISR to task offloading using semaphore.
        if (k_sem_take(&tamper_sem, K_FOREVER) == 0) {
            
            // 1. Capture detailed sensor data
            ret = sensor_sample_fetch(dev);
	        sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &accel_x);
	        sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &accel_y);
	        sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &accel_z);

	        /* lsm6dsl gyro */
	        ret = sensor_sample_fetch(dev);
	        sensor_channel_get(dev, SENSOR_CHAN_GYRO_X, &gyro_x);
	        sensor_channel_get(dev, SENSOR_CHAN_GYRO_Y, &gyro_y);
	        sensor_channel_get(dev, SENSOR_CHAN_GYRO_Z, &gyro_z);

            // 2. Logic: Is this a real tamper or noise?
            double a_x = sensor_value_to_double(&accel_x);
            double a_y = sensor_value_to_double(&accel_y);
            double a_z = sensor_value_to_double(&accel_z);

            double magnitude = sqrt(a_x*a_x + a_y*a_y + a_z*a_z);
            double delta = fabs(magnitude - GRAVITY);

            if (delta > THRESHOLD) 
            {
                struct system_event_queue evt = { .type = EVENT_TAMPER_DETECTED };
                k_msgq_put(&sys_event_q, &evt, K_NO_WAIT);

                printk("Tamper Event: Interrupt received !\n");

                tamper_status = 1;
                // Increment the tamper_total_count, make it atomic operation.
                atomic_inc(&tamper_total_count);
                
                uint32_t tamper_count = (uint32_t)atomic_get(&tamper_total_count);
                // 3. BLE: Notify the Gateway
                ble_notify_tamper(tamper_status, (uint32_t)tamper_count);

                // Write to NVS
                // nvs_write_tamper_count((uint32_t*)tamper_count);
                // nvs_stress_test();
                nvs_write_data(NVS_TAMPER_COUNT_ID, &tamper_count, sizeof(tamper_count));

                printk("ALERT: Tamper detected! Delta: %.2f m/s^2  Count: %d\n", delta, tamper_total_count);

            }
            
            k_sleep(K_MSEC(1000));

            // Reset tamper_status 
            tamper_status = 0;
            // bt_gatt_notify(NULL, tamper_attr, &tamper_status, sizeof(tamper_status));
        }
    }
}

/* FSM functions */
void process_send_temp_hum(const struct device *dev)
{
    struct sensor_value temperature, humidity;
    int ret;

    // hts221 logic here, polling mode for hts221.
    ret = sensor_sample_fetch(dev);
    sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temperature);
    sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &humidity);

    double temp = sensor_value_to_double(&temperature);
    double hum = sensor_value_to_double(&humidity);
    ble_temp = temp;
    ble_hum = hum;

    ble_notify_temp_hum(ble_temp, ble_hum);
}

void notify_store_tamper_event(void)
{
    tamper_status = 1;
    // Increment the tamper_total_count, make it atomic operation.
    atomic_inc(&tamper_total_count);
    
    uint32_t tamper_count = (uint32_t)atomic_get(&tamper_total_count);
    // 3. BLE: Notify the Gateway
    ble_notify_tamper(tamper_status, (uint32_t)tamper_count);

    // Write to NVS
    nvs_write_data(NVS_TAMPER_COUNT_ID, &tamper_count, sizeof(tamper_count));

    printk("ALERT: Tamper detected! Count: %d\n", tamper_total_count);
    
    // Reset tamper_status 
    tamper_status = 0;
    ble_notify_tamper(tamper_status, (uint32_t)tamper_count);
}

/* FSM functions end */

// Interrupt handler (ISR) for lsm6dsl accelerometer sensor.
static void lsm6dsl_trigger_handler(const struct device *dev,
                                    const struct sensor_trigger *trig)
{
    // Give the semaphore. Keeping it short here.
    k_sem_give(&tamper_sem);
    lsm6dsl_trig_cnt++;
}

static void lsm6dsl_trigger_config(const struct device *accel_dev)
{
    struct sensor_trigger trig;
    struct sensor_value odr;
    

    // Trigger interrupt on data ready.
    trig.type = SENSOR_TRIG_DATA_READY;
    trig.chan = SENSOR_CHAN_ACCEL_XYZ;

    odr.val1 = 104;
    odr.val2 = 0;


    if (sensor_attr_set(accel_dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr) < 0) 
    {
        printk("Cannot set sampling frequency for accelerometer.\n");
        return 0;
    }

    if (sensor_attr_set(accel_dev, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr) < 0) 
    {
        printk("Cannot set sampling frequency for gyro.\n");
        return 0;
    }

    if (sensor_trigger_set(accel_dev, &trig, lsm6dsl_trigger_handler) != 0)
    {
        printk("Could not set sensor type and channel\n");
		return 0;
    }
}

// Define the two threads.
K_THREAD_DEFINE(env_id, STACK_SIZE, env_thread_entry, DEVICE_DT_GET_ONE(st_hts221), NULL, NULL, ENV_PRIO, 0, 0);
K_THREAD_DEFINE(sec_id, STACK_SIZE, security_thread_entry, DEVICE_DT_GET_ONE(st_lsm6dsl), NULL, NULL, SECURITY_PRIO, 0, 0);

int main(void) 
{
    int err;
    enum system_state current_state = STATE_INITIALIZING;

    printk("Starting up Sentrinode ...\n");
    const struct device *const lsm6dsl_dev = DEVICE_DT_GET_ONE(st_lsm6dsl);
    const struct device *const hts221_dev = DEVICE_DT_GET_ONE(st_hts221);
    static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
    static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

    if (!device_is_ready(lsm6dsl_dev)) {
		printk("Sensor: Accelerometer not ready.\n");
		return 0;
	}

    if (!device_is_ready(hts221_dev)) {
        printk("Sensor: Environment data sensor HTS221 not ready.\n");
        return 0;
    }

    if (!gpio_is_ready_dt(&led0)) {
        printk("LED0 device not ready\n");
        return 0;
	}

    if (!gpio_is_ready_dt(&led1)) {
        printk("LED1 device not ready\n");
        return 0;
	}

    int ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return 0;
	}

    ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return 0;
	}

    // Initialize the NVS
    init_nvs();

    uint32_t temp_buffer = 0xDEADBEEF; // Start with a "Magic Number"

    // 2. Capture the exact return code
    int rc = nvs_read_data(NVS_TAMPER_COUNT_ID, &temp_buffer, sizeof(temp_buffer));

    printk("--- NVS Diagnostic ---\n");
    printk("Return Code (rc): %d\n", rc);
    printk("Buffer Value Hex: 0x%08x\n", temp_buffer);
    printk("Buffer Value Dec: %u\n", temp_buffer);

    if (rc == sizeof(temp_buffer)) {
        tamper_total_count = temp_buffer;
        printk("NVS startup read: Success! Using stored value.\n");
    } else {
        tamper_total_count = 0;
        printk("NVS startup read: Failure/Not Found. Resetting to 0.\n");
    }
    printk("----------------------\n");

    lsm6dsl_trigger_config(lsm6dsl_dev);

    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    /* Start Advertising */
    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
            printk("Advertising failed to start (err %d)\n", err);
            return 0;
    }

    while(1) 
    {
        // Wait for an event from any thread
        struct system_event_queue evt;
        k_msgq_get(&sys_event_q, &evt, K_FOREVER);

        printk("Event received: %d\n", evt.type);
        switch (current_state)
        {        
        case STATE_INITIALIZING:
            printk("System State: INITIALIZING\n");
            if (evt.type == EVENT_SENSOR_UPDATE) {
                current_state = STATE_SENSING;
            } else if (evt.type == EVENT_TAMPER_DETECTED) {
                current_state = STATE_TAMPER;
            } else {
                // Stay in initializing until we get a valid event
            }
            break;
        case STATE_SENSING:
            printk("System State: SENSING\n");
            if (evt.type == EVENT_TAMPER_DETECTED) {
                current_state = STATE_TAMPER;
                gpio_pin_set_dt(&led0, 0); // Turn off LED0 to indicate state change
                gpio_pin_set_dt(&led1, 1); // Turn on LED1 to indicate tamper
            } else {
                // Stay in sensing state
                process_send_temp_hum(hts221_dev);
                gpio_pin_toggle_dt(&led0);
            }
            break;
        case STATE_TAMPER:
            printk("System State: TAMPER\n");
            current_state = STATE_SENSING;
            gpio_pin_set_dt(&led1, 0); // Turn off LED1 to indicate state change
            break;
        default:
            break;
        }
        
        k_sleep(K_MSEC(2000));
    }

}