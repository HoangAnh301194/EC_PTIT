# Báo cáo nghiên cứu đề tài ESP32 - Thiết kế bộ điều khiển và thuật toán điều khiển cho hệ thống Drone 

## A. Công việc đã làm
- Cài đặt ESP-IDF, tạo project FreeRTOS đầu tiên, viết task LED nhấp nháy với xTaskCreate().
- Tìm hiểu queue và semaphore: tạo 2 task giao tiếp qua queue, đồng bộ bằng semaphore
### 1.1 Cài đặt ESP-IDF, tạo Project và thực hiện Blink led
- Cài đặt ESP-IDF: 
    -   ESP-IDF (Espressif IoT Development Framework) là bộ công cụ chính thức của hãng Espressif để lập trình cho các dòng chip ESP32, ESP32-S3, ESP32-C3,...Việc cài đặt ESP-IDF là rất cần thiết vì nó có tích hợp sẵn FreeRTOS, cung cấp driver phần cứng gốc cho ESP32, có hệ thống build chuyên nghiệp (idf.py, menuconfig) và tương thích với ESP32.
    - Truy cập link sau để tiến hành tải ESP-IDF : [link](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html)
        + có thể chọn phần mềm ESP-IDF riêng hoặc Extention trên VS code tuy nhiên theo em tìm hiểu thì thấy đa số là tải cả Extention và Esp - IDF.
        + Sau khi tải ESP-IDF thì tiến hành liên kết terminal của ESP-IDF với vs code để thực hiện build và upload code trên VS Code luôn .
        
- Thực hiện chạy code Blink LED 
    - Sau khi tải ESP-IDF, vào thư mục frameworks -> esp-idf-v5.5 -> examples -> get-started -> blink 
        - cần copy một form có sẵn vì một số file cấu hình board có sẵn em chưa thể tự tạo đc ạ.
        - Sau khi copy thì tạo 1 folder mới và đặt tên project
        - Truy cập VS code và mở folder vừa tạo.
        - Trong folder project vừa tọa có folder main, trong folder main sẽ có file code. Ta sẽ viết lại file code cho phù hợp dự án.
        - bấm tổ hợp phím ```Ctrl + Shift + P ``` và tìm ```settings JSON ```, bấm chọn ```Open User Settings (JSON) ``` và đổi toàn bộ code thành:
        ```json
                {
            "security.workspace.trust.untrustedFiles": "open",
            "cmake.additionalCompilerSearchDirs": [
                "C:/msys64/mingw32/bin",
                "C:/msys64/mingw64/bin",
                "C:/msys64/clang32/bin",
                "C:/msys64/clang64/bin",
                "C:/msys64/clangarm64/bin",
                "C:/msys64/ucrt64/bin"
            ],
            "window.confirmSaveUntitledWorkspace": false,
            "explorer.confirmDragAndDrop": false,
            "files.autoSave": "afterDelay",
            "explorer.confirmDelete": false,
            "git.autofetch": true,
            "idf.hasWalkthroughBeenShown": true,
            "terminal.integrated.defaultProfile.windows": "Command Prompt",
            "terminal.integrated.profiles.windows": {
                "Command Prompt": {
                    "path": [
                        "C:\\Windows\\System32\\cmd.exe"
                    ],
                    "args": [
                        "/K",
                        "C:\\Espressif\\idf_cmd_init.bat esp-idf-b877a3348464be9150868e7879f3ecbc"
                    ],
                    "icon": "terminal-cmd"
                },
                "PowerShell": {
                    "path": [
                        "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"
                    ],
                    "args": [],
                    "icon": "terminal-powershell"
                },
                "Git Bash": {
                    "path": [
                        "C:\\Program Files\\Git\\bin\\bash.exe"
                    ],
                    "args": ["--login", "-i"],
                    "icon": "terminal-bash"
                }
            },
            "idf.gitPathWin": "C:/Espressif/tools/idf-git/2.44.0/cmd/git.exe"
        }
        ```
    - Sau khi chỉnh sửa lại file main và link với ESP - IDF, tiến hành build và nạp code cho ESP:
        - trên giao diện của Extension ESP-IDF có nút build và upload 
        - Hoặc có thể gõ lệnh trên terminal của VS code :
            + lệnh build code : ``` idf.py build ``` 
            + lệnh nạp code ( flash code ) vào board mạch : ``` idf.py -p COMx flash```
        - Nạp code Blink LED cho ESP sử dụng FreeRTOS :
        ```cpp
            #include <stdio.h>
            #include "freertos/FreeRTOS.h"
            #include "freertos/task.h"
            #include "driver/gpio.h"

            #define LED_GPIO GPIO_NUM_2  // GPIO gắn LED

            // Task nhấp nháy LED
            void led_task(void *pvParameter) {
                gpio_pad_select_gpio(LED_GPIO);
                gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

                while (1) {
                    gpio_set_level(LED_GPIO, 1);  // Bật LED
                    vTaskDelay(pdMS_TO_TICKS(500));
                    gpio_set_level(LED_GPIO, 0);  // Tắt LED
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
            }
            void app_main() {
                xTaskCreate(led_task, "LED Task", 2048, NULL, 5, NULL);
            }

        ```
### 1.2  Queu và semaphore:
- Queue – Hàng đợi (Giao tiếp dữ liệu)

Queue trong FreeRTOS là một cơ chế truyền dữ liệu giữa các task theo nguyên tắc FIFO (First-In-First-Out). Đây là công cụ chính để các task trao đổi thông tin với nhau một cách an toàn.

- **Thread-safe**: Có thể được truy cập từ nhiều task cùng lúc mà không gây xung đột
- **Blocking**: Task có thể chờ đợi nếu queue đầy (khi gửi) hoặc trống (khi nhận)
- **Size cố định**: Queue có kích thước được xác định khi tạo
- **Copy data**: Dữ liệu được sao chép vào queue, không phải con trỏ
- Các hàm API chính
```cpp
// Tạo queue
QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize);

// Gửi dữ liệu (từ task)
BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait);

// Gửi dữ liệu (từ ISR)
BaseType_t xQueueSendFromISR(QueueHandle_t xQueue, const void *pvItemToQueue, BaseType_t *pxHigherPriorityTaskWoken);

// Nhận dữ liệu
BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait);
```
- Code ví dụ 
```cpp
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdio.h>

// Cấu trúc dữ liệu sensor
typedef struct {
    int sensor_id;
    float temperature;
    float humidity;
} sensor_data_t;

QueueHandle_t sensor_queue;

// Task đọc cảm biến
void sensor_task(void *pvParameters) {
    sensor_data_t data;
    int sensor_id = (int)pvParameters;
    
    while (1) {
        // Giả lập đọc dữ liệu cảm biến
        data.sensor_id = sensor_id;
        data.temperature = 25.5 + (sensor_id * 2.1);
        data.humidity = 60.0 + (sensor_id * 5.2);
        
        // Gửi dữ liệu vào queue
        if (xQueueSend(sensor_queue, &data, pdMS_TO_TICKS(100)) == pdTRUE) {
            printf("Sensor %d: Sent data (T=%.1f°C, H=%.1f%%)\n", 
                   sensor_id, data.temperature, data.humidity);
        } else {
            printf("Sensor %d: Queue full!\n", sensor_id);
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000)); // Đọc mỗi 2 giây
    }
}

// Task xử lý dữ liệu
void data_processing_task(void *pvParameters) {
    sensor_data_t received_data;
    
    while (1) {
        // Nhận dữ liệu từ queue
        if (xQueueReceive(sensor_queue, &received_data, portMAX_DELAY) == pdTRUE) {
            printf("Processing: Sensor %d - Temp: %.1f°C, Humidity: %.1f%%\n",
                   received_data.sensor_id, 
                   received_data.temperature, 
                   received_data.humidity);
            
            // Xử lý dữ liệu (ví dụ: kiểm tra ngưỡng cảnh báo)
            if (received_data.temperature > 30.0) {
                printf("High temperature alert from sensor %d!\n", 
                       received_data.sensor_id);
            }
        }
    }
}

void app_main() {
    // Tạo queue có thể chứa 10 phần tử kiểu sensor_data_t
    sensor_queue = xQueueCreate(10, sizeof(sensor_data_t));
    
    if (sensor_queue != NULL) {
        // Tạo 3 task sensor
        xTaskCreate(sensor_task, "Sensor1", 2048, (void*)1, 2, NULL);
        xTaskCreate(sensor_task, "Sensor2", 2048, (void*)2, 2, NULL);
        xTaskCreate(sensor_task, "Sensor3", 2048, (void*)3, 2, NULL);
        
        // Tạo task xử lý dữ liệu
        xTaskCreate(data_processing_task, "DataProcessor", 2048, NULL, 3, NULL);
        
        printf("System started with queue-based sensor communication\n");
    }
}
```

- Semaphore :

- Semaphore là một cơ chế đồng bộ hóa giúp điều khiển quyền truy cập tài nguyên hoặc đánh dấu sự kiện giữa các task. Nó hoạt động như một "cờ hiệu" hoặc "giấy thông hành".

- Các loại Semaphore:
1. Binary Semaphore
Chỉ có 2 trạng thái: Available (1) hoặc Not Available (0).

Dùng để đồng bộ giữa task và ISR, hoặc giữa 2 task.

Thường dùng để báo hiệu sự kiện đã xảy ra.

2. Counting Semaphore
Có thể đếm từ 0 đến một giá trị tối đa.

Dùng để quản lý tài nguyên có số lượng hạn chế (ví dụ: buffer, kết nối,...).

Mỗi lần Take giảm đi 1, mỗi lần Give tăng lên 1.

3. Mutex (Mutual Exclusion)
Đặc biệt hóa của Binary Semaphore.

Có tính năng Priority Inheritance để tránh Priority Inversion.

Chỉ task nào đã Take thì mới được Give.

- Các hàm API chính
```cpp

// Binary Semaphore
SemaphoreHandle_t xSemaphoreCreateBinary(void);

// Counting Semaphore
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount);

// Mutex
SemaphoreHandle_t xSemaphoreCreateMutex(void);

// Give semaphore (từ task)
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);

// Give semaphore (từ ISR)
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t xSemaphore, BaseType_t *pxHigherPriorityTaskWoken);

// Take semaphore
BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);
```


## B. Khó khăn
- hiện tại cần đa tác vụ để thực hành cái FreeRTOS nên khi nào anh lên lab thì em lên cùng để test thử điều khiển cùng lúc cảm biến, động cơ, led,... ạ
- Giao diện với cách dùng esp-idf trên vs code hơi lằng nhằng nên em tạm thời chạy bằng command idf terminal ạ .
## C. Công việc tiếp theo
- Tiếp tục tìm hiểu Free RTOS :
    + Thiết lập kiến trúc hệ thống Drone
    + Tạo task đọc IMU (MPU6050)
