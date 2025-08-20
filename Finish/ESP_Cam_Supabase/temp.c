#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include <Arduino.h>
#define FLASH_LED_PIN 4

// WiFi config
const char* ssid = "Xuong";
const char* password = "68686868";

// Supabase config
const char* SUPABASE_URL = "https://bhvgejarhxulboorwljd.supabase.co"; 
const char* SUPABASE_API_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImJodmdlamFyaHh1bGJvb3J3bGpkIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NTM1MDk0NTYsImV4cCI6MjA2OTA4NTQ1Nn0.eR-go5TNlhaQgOLmVw9bn5-AboFjfXHT3z-Ieux4Wco";     // Anon public key
const char* BUCKET_NAME = "espfile";                       // Tên bucket bạn tạo
char filePath[64];

void uploadImageToSupabase(uint8_t* imageData, size_t imageSize) {
    String fileName = "esp32cam_" + String(millis()) + ".jpg";

    String paths[2] = {
        "espCamImage/" + fileName,
        "public/" + fileName
    };

    for (int i = 0; i < 2; i++) {
        String filePath = paths[i];
        String url = String(SUPABASE_URL) + "/storage/v1/object/" + BUCKET_NAME + "/" + filePath;

        Serial.println("Starting upload to Supabase...");
        Serial.print("Upload URL: ");
        Serial.println(url);

        HTTPClient http;
        http.begin(url);
        http.addHeader("Content-Type", "image/jpeg");
        http.addHeader("apikey", SUPABASE_API_KEY);
        http.addHeader("Authorization", "Bearer " + String(SUPABASE_API_KEY));

        Serial.printf("Uploading image to %s, size: %u bytes\n", filePath.c_str(), imageSize);
        int httpResponseCode = http.PUT(imageData, imageSize);

        if (httpResponseCode > 0) {
            Serial.printf("Upload response code: %d\n", httpResponseCode);
            String responseBody = http.getString();
            Serial.println("Response body:");
            Serial.println(responseBody);

            if (httpResponseCode == 200 || httpResponseCode == 201) {
                Serial.println("Upload successful!");
            } else {
                Serial.println("Upload failed: server returned error status.");
            }
        } else {
            Serial.printf("Upload failed: %s\n", http.errorToString(httpResponseCode).c_str());
        }

        http.end();
        delay(100); // Delay nhẹ giữa các upload
    }
}

void captureAndUploadPhoto() {
    Serial.println("Turning on flash LED...");
    digitalWrite(FLASH_LED_PIN, HIGH);
    Serial.println("Capturing photo...");
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        digitalWrite(FLASH_LED_PIN, LOW);
        return;
    }
    Serial.printf("Photo captured: size %d bytes\n", fb->len);

    uploadImageToSupabase(fb->buf, fb->len);

    esp_camera_fb_return(fb);

    Serial.println("Turning off flash LED...");
    digitalWrite(FLASH_LED_PIN, LOW);
    Serial.println("Photo captured and upload process completed");
}
void takeAndUploadPhoto(framesize_t resolution, int brightness, String mode) {
    sensor_t * s = esp_camera_sensor_get();
    if (!s) {
        Serial.println("Cannot get camera sensor!");
        return;
    }

    // Cấu hình camera để ảnh sáng hơn
    s->set_framesize(s, resolution);
    if (brightness < -2) brightness = -2;
    if (brightness > 2) brightness = 2;
    s->set_brightness(s, brightness);
    s->set_contrast(s, 0);        // tăng contrast
    s->set_whitebal(s, 0);        // bật cân bằng trắng
    s->set_gainceiling(s, GAINCEILING_4X); // tăng gain để sáng hơn

    // Chụp timed capture non-blocking (ESP32 vẫn chạy loop)
    static unsigned long previousMillis = 0;
    const long interval = 10000; // 10 giây
    if (mode == "timed_capture") {
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= interval) {
            previousMillis = currentMillis;

            digitalWrite(FLASH_LED_PIN, HIGH);
            camera_fb_t * fb = esp_camera_fb_get();
            digitalWrite(FLASH_LED_PIN, LOW);

            if (!fb) {
                Serial.println("Camera capture failed");
                return;
            }

            Serial.printf("Timed capture: size %zu bytes\n", fb->len);
            uploadImageToSupabase(fb->buf, fb->len);
            esp_camera_fb_return(fb);
        }
    } else {
        // Chụp 1 lần
        digitalWrite(FLASH_LED_PIN, HIGH);
        camera_fb_t* fb = esp_camera_fb_get();
        digitalWrite(FLASH_LED_PIN, LOW);

        if (!fb) {
            Serial.println("Camera capture failed");
            return;
        }

        Serial.printf("Captured photo, size: %zu bytes\n", fb->len);
        uploadImageToSupabase(fb->buf, fb->len);
        esp_camera_fb_return(fb);
    }
}


void setup() {
    pinMode(FLASH_LED_PIN, OUTPUT);
    digitalWrite(FLASH_LED_PIN, LOW);  // Tắt mặc định
    Serial.begin(115200);
    delay(10);

    Serial.printf("Connecting to WiFi SSID: %s\n", ssid);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
        Serial.print(".");
    }
    Serial.println();
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    for(int i = 1; i<= 20; i++){
        digitalWrite(FLASH_LED_PIN, HIGH);
        delay(20);
        digitalWrite(FLASH_LED_PIN, LOW); 
        delay(20); 
    }

    Serial.println("Initializing camera...");
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = 5;
    config.pin_d1 = 18;
    config.pin_d2 = 19;
    config.pin_d3 = 21;
    config.pin_d4 = 36;
    config.pin_d5 = 39;
    config.pin_d6 = 34;
    config.pin_d7 = 35;
    config.pin_xclk = 0;
    config.pin_pclk = 22;
    config.pin_vsync = 25;
    config.pin_href = 23;
    config.pin_sccb_sda = 26;
    config.pin_sccb_scl = 27;
    config.pin_pwdn = 32;
    config.pin_reset = -1;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    if (psramFound()) {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 10;
        config.fb_count = 1;
    } else {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }

    esp_err_t cam_init = esp_camera_init(&config);
    if (cam_init != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", cam_init);
        return;
    }
    Serial.println("Camera initialized successfully");

    delay(500); // Chờ ổn định

    captureAndUploadPhoto(); // Chụp & gửi ảnh lên Supabase
    takeAndUploadPhoto(FRAMESIZE_QVGA, 0, "normal");
}

void loop() {
    // Nếu muốn chụp định kỳ, bạn có thể gọi captureAndUploadPhoto() ở đây
}
