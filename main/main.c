#include "freertos/FreeRTOS.h"

#include "host/ble_hs.h"
#include "host/util/util.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include <pcf8574.h>

#include "beep.h"
#include "blecent.h"
#include "lcd.h"


static const char *TAG = "main";


struct BEACON {
    // char[32] complete_local_name;
    char *nickName;
    ble_addr_t bleAddr;
};

struct BEACON BEACONS[] = {
    // Complete Local Name = Kensington Eureka 4936
    {
        "RACHAEL",
        // The Address bytes are required in reverse order.
        // Regular order, from phone: 0x78, 0xc5, 0xe5, 0xa1, 0x49, 0x36
        {0x00, {0x36, 0x49, 0xa1, 0xe5, 0xc5, 0x78}},
    },
    // Complete Local Name = Kensington Eureka 5727
    {
        "BELA",
        // Regular order, from phone: 0x78, 0xc5, 0xe5, 0xa1, 0x57, 0x27
        {0x00, {0x27, 0x57, 0xa1, 0xe5, 0xc5, 0x78}},
    },
    {0}
    // Something
    // {0x00, {0x66, 0x28, 0x15, 0xc4, 0x98, 0x3f}},
    // {0x00, {0x02, 0x6c, 0x40, 0xfd, 0x2a, 0x86}};
};


// GPIO5:  Select BLE address to listen for
#define GPIO_BUTTON_SELECT_BEACON 5
#define GPIO_INPUT_PIN_SEL (1ULL << GPIO_BUTTON_SELECT_BEACON)
#define ESP_INTR_FLAG_DEFAULT 0

volatile int listenBeaconIndex = 0;
volatile int lastDbm = 0;
volatile int secondsSinceLast = 0;

static void selectBeaconDebounceTask(void *arg);
void ble_store_config_init(void);
static int blecent_gap_event(struct ble_gap_event *event, void *arg);
static void blecent_on_reset(int reason);
static void blecent_on_sync(void);
void blecent_host_task(void *param);
void gpio_isr_select_beacon_handler(void *arg);
static void secondCounterTask(void *arg);


void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_ERROR);

    ESP_LOGD(TAG, "### MAIN START");

    int rc;

    /* Initialize NVS — it is used to store PHY calibration data */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(i2cdev_init());

    lcd_init();

    lcd_print(0, "Starting...");

    ESP_LOGD(TAG, "ledc_init() START");
    ledc_init();
    ESP_LOGD(TAG, "ledc_init() END");

    // Not available and not needed?
    // ESP_ERROR_CHECK(esp_nimble_hci_and_controller_init());

    ESP_LOGD(TAG, "nimble_port_init() START");
    nimble_port_init();
    ESP_LOGD(TAG, "nimble_port_init() END");

    /* Configure the host. */
    ble_hs_cfg.reset_cb = blecent_on_reset;
    ble_hs_cfg.sync_cb = blecent_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Initialize data structures to track connected peers. */
    rc = peer_init(MYNEWT_VAL(BLE_MAX_CONNECTIONS), 64, 64, 64);
    assert(rc == 0);

    /* Set the default device name. */
    rc = ble_svc_gap_device_name_set("ble-finder");
    assert(rc == 0);

    ble_store_config_init();

    nimble_port_freertos_init(blecent_host_task);

    // Button interrupts

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    // install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    // Hook ISR handler for specific GPIO pin
    gpio_isr_handler_add(GPIO_BUTTON_SELECT_BEACON, gpio_isr_select_beacon_handler, (void *) GPIO_BUTTON_SELECT_BEACON);

    // printf("Minimum free heap size: %d bytes\n", esp_get_minimum_free_heap_size());

    xTaskCreate(secondCounterTask, "secondCounterTask", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL);

    lcd_print(0, BEACONS[listenBeaconIndex].nickName);

    ESP_LOGD(TAG, "### MAIN END");

    // We don't have to enter an endless loop here.
}

static void selectBeaconDebounceTask(void *arg)
{
    lcd_print(0, BEACONS[listenBeaconIndex].nickName);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_intr_enable(GPIO_BUTTON_SELECT_BEACON);
    vTaskDelete(NULL);
}

/**
 * Initiates the GAP general discovery procedure.
 */
static void blecent_scan(void)
{
    uint8_t own_addr_type;
    struct ble_gap_disc_params disc_params;
    int rc;

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "error determining address type; rc=%d\n", rc);
        return;
    }

    // We're scanning continuously and want to see repeated advertisements
    disc_params.filter_duplicates = 0;

    /**
     * Perform a passive scan.  I.e., don't send follow-up scan requests to
     * each advertiser.
     */
    disc_params.passive = 1;

    /* Use defaults for the rest of the parameters. */
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
                      blecent_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error initiating GAP discovery procedure; rc=%d\n",
                 rc);
    }
}

// Convert RSSI dBm (signal strength) to audible frequency in Hz.
// Stronger signal gives higher pitch.
// dBm is 127 is unavailable.
int dbm_to_hz(int dbm)
{
    int hz = -(-dbm - 100) * 20;
    if (hz < 10) {
        hz = 10;
    }
    if (hz > 900) {
        hz = 900;
    }
    return hz;
}

// Format address msg and address.
// msg must be 0-4 chars.
// E.g: ADDR3649a1e5c578
void addr_to_str(char *buf, int maxlen, char *msg, ble_addr_t *addr)
{
    // hh was added in C99 and performs a cast to byte. This is to make up for type information that is lost when
    // passing variadic parameters to printf().
    snprintf(buf, maxlen, "%s %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
             msg,
             addr->val[0],
             addr->val[1],
             addr->val[2],
             addr->val[3],
             addr->val[4],
             addr->val[5]);
}

void log_addr(char *msg, ble_addr_t *addr)
{
    char buf[128];
    addr_to_str(buf, 128, msg, addr);
    ESP_LOGD(TAG, "%s", buf);
}

void beep_rssi(int8_t rssi)
{
    int freq_hz = dbm_to_hz(rssi);
    beep(freq_hz, 250);
}

int addr_are_equal(ble_addr_t *a, ble_addr_t *b)
{
    return ble_addr_cmp(a, b) == 0;
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that is
 * established.  blecent uses the same callback for all connections.
 *
 * @param event                 The event being signalled.
 * @param arg                   Application-specified argument; unused by
 *                                  blecent.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int blecent_gap_event(struct ble_gap_event *event, void *arg)
{
    if (event->type != BLE_GAP_EVENT_DISC) {
        return 0;
    }

    ESP_LOGD(TAG, "listenBeaconIndex: %d", listenBeaconIndex);

    int8_t rssi = event->disc.rssi;
    ble_addr_t *addr = &event->disc.addr;

    // 127 is a reserved value for unavailable signal strength, meaning it's probably not a beacon.
    if (rssi == 127) {
        return 0;
    }

    // int ret;
    // struct ble_hs_adv_fields fields;
    //
    // ret = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
    // ESP_ERROR_CHECK(ret);
    //
    // if (fields.name_is_complete) {
    //   ESP_LOGE(TAG, "NAME: %s", fields.name);
    //   ESP_LOGE(TAG, "LEN: %d", fields.name_len);
    // }
    //
    // print_adv_fields(&fields);

    log_addr("RCV ", addr);
    // lcd_print_addr("RCV ", addr);

    if (addr_are_equal(addr, &BEACONS[listenBeaconIndex].bleAddr)) {
        beep_rssi(rssi);
        lastDbm = rssi;
        secondsSinceLast = 0;
    }
    // else {
    //   lcd_print_addr("UNK ", addr);
    // }

    return 0;
}

void blecent_on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting state; reason=%d\n", reason);
}

void blecent_on_sync(void)
{
    int rc;

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* Begin scanning for a peripheral to connect to. */
    blecent_scan();
}

void blecent_host_task(void *param)
{
    ESP_LOGD(TAG, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void gpio_isr_select_beacon_handler(void *arg)
{
    gpio_intr_disable(GPIO_BUTTON_SELECT_BEACON);

    listenBeaconIndex += 1;
    if (BEACONS[listenBeaconIndex].nickName == NULL) {
        listenBeaconIndex = 0;
    }

    lastDbm = 0;
    secondsSinceLast = 0;

    xTaskCreate(selectBeaconDebounceTask, "selectBeaconDebounceTask", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL);
}

void secondCounterTask(void *arg)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    for (;;) {
        lcd_print(1, "%ld dBm, %ld sec", lastDbm, secondsSinceLast);
        ++secondsSinceLast;
        xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }
}
