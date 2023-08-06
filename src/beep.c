#include <driver/ledc.h>

#include <esp_err.h>
#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <math.h>


#define TAG "beep"

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          (4)
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT
// #define LEDC_DUTY_RES           (11)
// Set duty to 50%. ((2 ** 13) - 1) * 50% = 4095
// #define LEDC_DUTY               (4095)
#define LEDC_DUTY               (((1<<LEDC_DUTY_RES) - 1) / 2)
// Frequency in Hertz (10 Hz is lowest supported)
#define LEDC_FREQUENCY          (10) \

//
#define SINE_FACTOR 127.0 // measured for step size = 1 and no advantage (8.3MHz)
int STACK_SIZE = 8192;

void beep_start();
void beep_stop();

// The ESP32 has a nice sine wave generator, but it can only output on a couple of pins, which are
// not available on my dev board, so we settle for square waves, which can be output on pretty much any
// pin. We generate square waves with the LED Control (LEDC) PWM peripheral.
void ledc_init(void) {
  ledc_timer_config_t ledc_timer = {
      .speed_mode       = LEDC_MODE,
      .timer_num        = LEDC_TIMER,
      .duty_resolution  = LEDC_DUTY_RES,
      .freq_hz          = LEDC_FREQUENCY,
      .clk_cfg          = LEDC_AUTO_CLK
  };
  ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

  ledc_channel_config_t ledc_channel = {
      .speed_mode     = LEDC_MODE,
      .channel        = LEDC_CHANNEL,
      .timer_sel      = LEDC_TIMER,
      .intr_type      = LEDC_INTR_DISABLE,
      .gpio_num       = LEDC_OUTPUT_IO,
      .duty           = LEDC_DUTY,
      .hpoint         = 0
  };
  ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
  beep_stop();
}

struct beep_task_param_t {
  uint32_t freq_hz;
  int duration_ms;
} beep_task_param;


void beep_task(void *pvParameters) {
  ESP_LOGI(TAG, "beep freq_hz=%ld duration_ms=%d", beep_task_param.freq_hz, beep_task_param.duration_ms);
  ledc_set_freq(LEDC_MODE, LEDC_TIMER, beep_task_param.freq_hz);
  beep_start();
  vTaskDelay(pdMS_TO_TICKS(beep_task_param.duration_ms));
  beep_stop();
  vTaskDelete(NULL);
}

void beep_start() {
  ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY);
  ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

void beep_stop() {
  ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
  ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}


void beep(uint32_t freq_hz, int duration_ms) {
  TaskHandle_t h = NULL;
  beep_task_param.freq_hz = freq_hz;
  beep_task_param.duration_ms = duration_ms;
  xTaskCreate(beep_task, "beep_task", STACK_SIZE, NULL, 0, &h);
  configASSERT(h);
}
