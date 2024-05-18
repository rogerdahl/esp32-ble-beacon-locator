#include <stdarg.h>
#include <stdio.h>

#include <hd44780.h>
#include <inttypes.h>
#include <pcf8574.h>
#include <string.h>

#include "esp_log.h"
#include "host/ble_hs.h"

#define CONFIG_EXAMPLE_I2C_ADDR 0x27
#define CONFIG_EXAMPLE_I2C_MASTER_SDA 21
#define CONFIG_EXAMPLE_I2C_MASTER_SCL 22

static const char *tag = "lcd";

static i2c_dev_t pcf8574;

static const uint8_t char_data[] = {
    // Bell symbol
    0x04, 0x0e, 0x0e, 0x0e, 0x1f, 0x00, 0x04, 0x00,
    // Hourglass symbol
    0x1f, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x1f, 0x00};

static esp_err_t write_lcd_data(const hd44780_t *lcd, uint8_t data)
{
    return pcf8574_port_write(&pcf8574, data);
}

hd44780_t lcd = {
    .write_cb = write_lcd_data, // use callback to send data to LCD by I2C GPIO expander
    .font = HD44780_FONT_5X8,
    .lines = 2,
    .pins = {
        .rs = 0,
        .e = 2,
        .d4 = 4,
        .d5 = 5,
        .d6 = 6,
        .d7 = 7,
        .bl = 3},
};

SemaphoreHandle_t mutex;

void lcd_init()
{
    ESP_LOGD(tag, "lcd_init() START");

    // Note: "Timeout: ticks =" messages are just noise from the esp-idf-lib hd44780 driver. It can be commented out
    // in esp-idf-lib/components/pcf8574/pcf8574.c, or the logging level can be bumped up to above "DEBUG".
    memset(&pcf8574, 0, sizeof(i2c_dev_t));
    ESP_ERROR_CHECK(pcf8574_init_desc(&pcf8574, CONFIG_EXAMPLE_I2C_ADDR, 0, CONFIG_EXAMPLE_I2C_MASTER_SDA,
                                      CONFIG_EXAMPLE_I2C_MASTER_SCL));
    ESP_ERROR_CHECK(hd44780_init(&lcd));
    hd44780_switch_backlight(&lcd, true);
    // Bell symbol, print with \x08
    hd44780_upload_character(&lcd, 0, char_data);
    // Hourglass symbol, print with \x09
    hd44780_upload_character(&lcd, 1, char_data + 8);

    // Create a mutex type semaphore
    mutex = xSemaphoreCreateMutex();
    assert(mutex != NULL);

    xSemaphoreGive(mutex);

    ESP_LOGD(tag, "lcd_init() END");
}

void lcd_print(int line, const char *fmt, ...)
{
    ESP_LOGD(tag, "lcd_print() START");

    xSemaphoreTake(mutex, portMAX_DELAY);

    // Clear the line
    hd44780_gotoxy(&lcd, 0, line);
    hd44780_puts(&lcd, "                ");

    hd44780_gotoxy(&lcd, 0, line);

    char buf[16] = {0};
    memset(buf, 0, 16);

    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, 16, fmt, args);
    va_end(args);

    ESP_LOGI(tag, "buf: %s", buf);
    ESP_LOGI(tag, "strlen %d", strlen(buf));

    hd44780_puts(&lcd, buf);

    xSemaphoreGive(mutex);

    ESP_LOGD(tag, "lcd_print() END");
}

// Print 0-4 char msg and address on line 0.
// E.g: ADDR3649a1e5c578
void lcd_print_addr(char *msg, ble_addr_t *addr)
{
    lcd_print(1, "%s%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx    ",
              msg,
              addr->val[0],
              addr->val[1],
              addr->val[2],
              addr->val[3],
              addr->val[4],
              addr->val[5]);
}
