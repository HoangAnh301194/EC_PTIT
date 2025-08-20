#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "driver/adc.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// ============= CẤU HÌNH CHÂN GPIO =============
#define LED_GPIO GPIO_NUM_2
#define POT_ADC_CHANNEL ADC_CHANNEL_6        // GPIO34 cho biến trở điều khiển BLDC
#define ESC_GPIO GPIO_NUM_25                 // GPIO25 cho tín hiệu ESC/BLDC

// ============= CẤU HÌNH PWM CHO ESC =============
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_16_BIT    // Độ phân giải 16-bit (0-65535)
#define LEDC_FREQUENCY          50                   // Tần số 50 Hz cho ESC

// ============= CÀI ĐẶT ESC/BLDC =============
#define US_MIN                  1000    // Độ rộng xung tối thiểu (1000μs)
#define US_MAX                  2000    // Độ rộng xung tối đa (2000μs)
#define US_PERIOD               20000   // Chu kỳ PWM (20ms = 20000μs)
#define ARM_HOLD_MS             1500    // Thời gian giữ để arming (1.5 giây)
#define ARM_THRESHOLD           50      // Ngưỡng ADC cho vị trí minimum
#define DEADZONE_US             10      // Vùng chết gần minimum

// ============= BỘ LỌC LÀMỊN TÍN HIỆU =============
#define ALPHA                   0.1f    // Hệ số bộ lọc thông thấp (0-1)

static const char *TAG = "BLDC_Control";

// ============= BIẾN TOÀN CỤC =============
// Biến điều khiển BLDC
static bool bldc_armed = false;          // Trạng thái đã arming chưa
static int64_t arm_start_time = 0;       // Thời điểm bắt đầu arming  
static float smoothed_pot = 0.0f;        // Giá trị biến trở đã làm mịn
static int current_throttle_us = US_MIN; // Throttle hiện tại (μs)

// Mutex để bảo vệ biến chia sẻ
static SemaphoreHandle_t bldc_mutex;

// ============= HÀM HỖ TRỢ =============

/**
 * Chuyển đổi microseconds sang LEDC duty cycle (16-bit)
 */
uint32_t us_to_duty_16bit(int us) {
    uint32_t duty_max = (1UL << LEDC_DUTY_RES) - 1;  // 2^16 - 1 = 65535
    uint32_t duty = (uint32_t)((uint64_t)us * duty_max / US_PERIOD);
    return duty;
}

/**
 * Map giá trị ADC (0-4095) sang microseconds (1000-2000)
 */
int adc_to_microseconds(int adc_val) {
    if (adc_val < 0) adc_val = 0;
    if (adc_val > 4095) adc_val = 4095;
    
    return US_MIN + ((adc_val * (US_MAX - US_MIN)) / 4095);
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

// ============= CÁC TASK FREERTOS =============

// Task 1: Nháy LED (Hiển thị trạng thái)
void led_task(void *pvParameters)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    while (1) {
        if (bldc_armed) {
            // Nháy nhanh khi đã armed
            gpio_set_level(LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_set_level(LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            // Nháy chậm khi chưa armed
            gpio_set_level(LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(300));
            gpio_set_level(LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }
}

// Task 2: In "EC - PTIT"
void print_task(void *pvParameters)
{
    while (1) {
        printf("EC - PTIT\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Task 3: Đọc biến trở và điều khiển BLDC
void potentiometer_bldc_task(void *pvParameters)
{
    // Cấu hình ADC cho biến trở
    adc1_config_width(ADC_WIDTH_BIT_12);  // 0-4095
    adc1_config_channel_atten(POT_ADC_CHANNEL, ADC_ATTEN_DB_11);  // Dải 0-3.3V

    ESP_LOGI(TAG, "Task điều khiển BLDC bằng biến trở đã khởi động");
    ESP_LOGI(TAG, "CẢNH BÁO: Tháo cánh quạt trước khi test!");
    ESP_LOGI(TAG, "Giữ biến trở ở vị trí MINIMUM trong 1.5s để ARM");

    // Khởi tạo giá trị làm mịn
    int initial_raw = adc1_get_raw(POT_ADC_CHANNEL);
    smoothed_pot = (float)initial_raw;

    while (1) {
        // Đọc ADC từ biến trở
        int raw_adc = adc1_get_raw(POT_ADC_CHANNEL);
        
        // Áp dụng bộ lọc thông thấp để làm mịn
        smoothed_pot = ALPHA * raw_adc + (1.0f - ALPHA) * smoothed_pot;
        int pot_value = (int)smoothed_pot;

        // Xử lý quá trình arming
        if (!bldc_armed) {
            if (pot_value < ARM_THRESHOLD) {
                // Biến trở gần vị trí minimum
                if (arm_start_time == 0) {
                    arm_start_time = esp_timer_get_time();  // Lấy thời gian hiện tại (μs)
                    ESP_LOGI(TAG, "Bắt đầu arming... Giữ biến trở ở minimum!");
                }
                
                int64_t elapsed_ms = (esp_timer_get_time() - arm_start_time) / 1000;
                
                if (elapsed_ms >= ARM_HOLD_MS) {
                    bldc_armed = true;
                    ESP_LOGI(TAG, ">>> BLDC ĐÃ ARM! Bây giờ có thể tăng throttle.");
                    ESP_LOGI(TAG, ">>> CẢNH BÁO: Motor đã sẵn sàng quay!");
                } else {
                    // Hiển thị tiến trình arming mỗi 200ms
                    static int64_t last_progress_print = 0;
                    if ((esp_timer_get_time() - last_progress_print) > 200000) {  // 200ms
                        float progress = (float)elapsed_ms / ARM_HOLD_MS * 100;
                        ESP_LOGI(TAG, "Tiến trình arming: %.1f%%", progress);
                        last_progress_print = esp_timer_get_time();
                    }
                }
            } else {
                // Reset arming nếu biến trở rời khỏi vị trí minimum
                if (arm_start_time != 0) {
                    ESP_LOGI(TAG, "Hủy arming - biến trở rời khỏi vị trí minimum");
                }
                arm_start_time = 0;
            }
            
            // Luôn gửi tín hiệu minimum khi chưa armed
            set_bldc_throttle(US_MIN);
            
        } else {
            // BLDC đã armed - điều khiển throttle dựa trên biến trở
            int throttle_us = adc_to_microseconds(pot_value);
            
            // Áp dụng vùng chết gần minimum để tránh rung
            if (throttle_us < US_MIN + DEADZONE_US) {
                throttle_us = US_MIN;
            }
            
            // Đặt throttle
            set_bldc_throttle(throttle_us);
            
            // Debug output
            float throttle_percent = ((float)(throttle_us - US_MIN) / (US_MAX - US_MIN)) * 100;
            ESP_LOGI(TAG, "ADC: %4d, Throttle: %4d μs (%.1f%%)", 
                     pot_value, throttle_us, throttle_percent);
        }

        // Cập nhật với tần số ~25Hz (delay 40ms)
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

// Task 4: Giám sát hệ thống và trạng thái BLDC
void system_monitor_task(void *pvParameters)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (1) {
        ESP_LOGI(TAG, "=== TRẠNG THÁI HỆ THỐNG ===");
        ESP_LOGI(TAG, "Heap còn trống: %lu bytes", (unsigned long)esp_get_free_heap_size());
        ESP_LOGI(TAG, "BLDC đã Armed: %s", bldc_armed ? "CÓ" : "KHÔNG");
        
        if (bldc_armed && xSemaphoreTake(bldc_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            float throttle_percent = ((float)(current_throttle_us - US_MIN) / (US_MAX - US_MIN)) * 100;
            ESP_LOGI(TAG, "Throttle hiện tại: %d μs (%.1f%%)", current_throttle_us, throttle_percent);
            xSemaphoreGive(bldc_mutex);
        }
        
        ESP_LOGI(TAG, "Số task đang chạy: 4");
        ESP_LOGI(TAG, "Giá trị biến trở làm mịn: %.1f", smoothed_pot);
        ESP_LOGI(TAG, "==========================");
        
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(3000));  // Mỗi 3 giây
    }
}

// ============= ỨNG DỤNG CHÍNH =============
void app_main(void)
{
    ESP_LOGI(TAG, "Khởi động ứng dụng điều khiển BLDC FreeRTOS ESP32");
    
    // Tạo mutex để điều khiển thread-safe
    bldc_mutex = xSemaphoreCreateMutex();
    if (bldc_mutex == NULL) {
        ESP_LOGE(TAG, "Không thể tạo mutex!");
        return;
    }

    // Cấu hình PWM cho ESC/BLDC
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
        .gpio_num = ESC_GPIO,
        .speed_mode = LEDC_MODE,
        .hpoint = 0,
        .timer_sel = LEDC_TIMER
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // Khởi tạo với throttle minimum (an toàn)
    set_bldc_throttle(US_MIN);
    ESP_LOGI(TAG, "PWM đã khởi tạo: %d Hz, GPIO%d, độ phân giải 16-bit", LEDC_FREQUENCY, ESC_GPIO);

    // Tạo các FreeRTOS task với priority phù hợp
    xTaskCreate(led_task, "LED Status", 2048, NULL, 2, NULL);
    xTaskCreate(print_task, "Print Task", 2048, NULL, 1, NULL);
    xTaskCreate(potentiometer_bldc_task, "Pot BLDC Control", 4096, NULL, 5, NULL);  // Priority cao nhất
    xTaskCreate(system_monitor_task, "System Monitor", 3072, NULL, 1, NULL);

    ESP_LOGI(TAG, "Tất cả các task đã được tạo thành công!");
    ESP_LOGI(TAG, "AN TOÀN: Tháo cánh quạt và giữ biến trở ở minimum để bắt đầu!");
}