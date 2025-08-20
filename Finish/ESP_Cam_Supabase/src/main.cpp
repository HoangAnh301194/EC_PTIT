#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include <Arduino.h>
#define FLASH_LED_PIN 4

// WiFi config
const char* ssid = "Xuong";
const char* password = "68686868";
String flash = "ON"; // Biến để theo dõi trạng thái flash
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
        delay(500); // Delay nhẹ giữa các upload
    }
}

void captureAndUploadPhotoCustom(int sharpness, framesize_t resolution) {
    if( flash == "ON" ) {
        Serial.println("FLASH...");
        digitalWrite(FLASH_LED_PIN, HIGH);
        delay(50);
    }
    // Lấy sensor để chỉnh tham số
    sensor_t * s = esp_camera_sensor_get();
    if (!s) {
        Serial.println("Cannot access camera sensor!");
        digitalWrite(FLASH_LED_PIN, LOW);
        return;
    }

    // Giới hạn sharpness (-2..2)
    if (sharpness < -2) sharpness = -2;
    if (sharpness > 2) sharpness = 2;
    s->set_sharpness(s, sharpness);

    // Chỉnh độ phân giải
    s->set_framesize(s, resolution);

    Serial.printf("Capturing photo (sharpness=%d, resolution=%d)......\n", sharpness, resolution);
    // Chụp ảnh
    camera_fb_t* fb = esp_camera_fb_get();

    if (flash == "ON") {
        digitalWrite(FLASH_LED_PIN, LOW);     
        flash = "OFF"; // Tắt flash sau khi chụp
    }
    if (!fb) {
        Serial.println("Camera capture failed");
        return;
    }
    Serial.printf("Photo captured: size %d bytes, %dx%d\n", fb->len, fb->width, fb->height);

    // Upload ảnh lên Supabase
    uploadImageToSupabase(fb->buf, fb->len);

    // Trả lại buffer cho camera
    esp_camera_fb_return(fb);
    Serial.println("Custom capture and upload completed");
}


void setup() {
    pinMode(FLASH_LED_PIN, OUTPUT);
    digitalWrite(FLASH_LED_PIN, LOW);  // Tắt mặc định
    Serial.begin(115200);
    delay(1000);

    Serial.printf("Connecting to WiFi SSID: %s\n", ssid);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    for(int i = 1; i<= 10; i++){
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

    delay(300);
    captureAndUploadPhotoCustom(2, FRAMESIZE_QVGA); 
    
}

void loop() {
}