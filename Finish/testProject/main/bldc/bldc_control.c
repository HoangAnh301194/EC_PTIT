#include "bldc_control.h"

// Biến toàn cục
static const char *TAG = "BLDC_Control";
static SemaphoreHandle_t bldc_mutex;
static int current_throttle_us = US_MIN;
static bool bldc_armed = false;

/**
 * Chuyển đổi microseconds sang LEDC duty cycle (16-bit)
 */
static uint32_t us_to_duty_16bit(int us) {
    uint32_t duty_max = (1UL << LEDC_DUTY_RES) - 1;
    return (uint32_t)((uint64_t)us * duty_max / US_PERIOD);
}

/**
 * Khởi tạo BLDC
 */
void bldc_init(gpio_num_t esc_gpio) {
    bldc_mutex = xSemaphoreCreateMutex();
    if (bldc_mutex == NULL) {
        ESP_LOGE(TAG, "Không thể tạo mutex!");
        return;
    }

    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY,
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .channel = LEDC_CHANNEL,
        .duty = 0,
        .gpio_num = esc_gpio,
        .speed_mode = LEDC_MODE,
        .hpoint = 0,
        .timer_sel = LEDC_TIMER
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    set_bldc_throttle(US_MIN);
    ESP_LOGI(TAG, "BLDC đã khởi tạo với GPIO%d", esc_gpio);
}

/**
 * Đặt throttle BLDC (thread-safe)
 */
void set_bldc_throttle(int microseconds) {
    if (xSemaphoreTake(bldc_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        current_throttle_us = microseconds;
        uint32_t duty = us_to_duty_16bit(microseconds);
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        xSemaphoreGive(bldc_mutex);
    }
}

/**
 * Kiểm tra trạng thái armed
 */
bool is_bldc_armed(void) {
    return bldc_armed;
}