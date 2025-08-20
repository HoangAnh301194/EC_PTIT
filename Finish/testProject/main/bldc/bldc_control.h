#ifndef BLDC_CONTROL_H
#define BLDC_CONTROL_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/ledc.h"
#include "esp_log.h"

// Cấu hình BLDC
#define US_MIN                  1000
#define US_MAX                  2000
#define US_PERIOD               20000
#define DEADZONE_US             10
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_16_BIT
#define LEDC_FREQUENCY          50

// Khai báo các hàm
void bldc_init(gpio_num_t esc_gpio);
void set_bldc_throttle(int microseconds);
bool is_bldc_armed(void);

#endif // BLDC_CONTROL_H