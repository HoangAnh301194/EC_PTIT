#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>


// Biến điều khiển bộ đếm
bool isCounting = false;
unsigned long previousMillis = 0;
int countValue = 0;
int interval = 1000;
bool keep = true; // biến giữ chương trình dừng cho đến khi Web kết nối được ble ;
// WebServer
WebServer server(80);
String ssid, password, topic, message, username, passBroker, mqttServer, mqttport;
String chipID;
// MQTT
// const char *mqtt_broker = "192.168.1.12";
// int mqtt_port = 1883;
WiFiClient espClient;
PubSubClient client(espClient);
String client_id = "esp32-client";

// BLE UUID
#define BatteryService BLEUUID((uint16_t)0x181F)
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_CONNECTION_STATUS "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer *pServer;
BLEService *pBattery;
BLECharacteristic BatteryLevelCharacteristic(BLEUUID((uint16_t)0x2A18),
  BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
BLEDescriptor BatteryLevelDescriptor(BLEUUID((uint16_t)0x2901));
BLECharacteristic connectionStatusCharacteristic(CHARACTERISTIC_UUID_CONNECTION_STATUS, BLECharacteristic::PROPERTY_NOTIFY);

bool _BLEClientConnected = false;

// BLE Server Callbacks
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) { _BLEClientConnected = true; }
  void onDisconnect(BLEServer* pServer) { _BLEClientConnected = false; }
};

// Gửi dữ liệu lên MQTT
void publishMessage(const char* topic, String payload, bool retained) {
  if (client.publish(topic, payload.c_str(), retained)) {
    Serial.println("Message published [" + String(topic) + "]: " + payload);
  }
}



// BLE nhận dữ liệu
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String rxValue = pCharacteristic->getValue().c_str();
    if (rxValue.length() == 0) return;

    String receivedData = String(rxValue.c_str());
    receivedData.trim(); // loại bỏ khoảng trắng thừa ở cuối chuỗi 
    Serial.print("Received: ");
    Serial.println(receivedData);
    if (receivedData == "SCAN_WIFI"){
      keep = false;
    }
    size_t delimiterPos = receivedData.indexOf(':');
    if (delimiterPos == -1) return;

    String check = receivedData.substring(0, delimiterPos);
    String data = receivedData.substring(delimiterPos + 1);

    if (check == "WIFI") {
      size_t sep = data.indexOf(';');
      if (sep != -1) {
        ssid = data.substring(0, sep);
        password = data.substring(sep + 1);
      } else {
        ssid = data;
        password = "";
      }
      Serial.println("WiFi SSID: " + ssid + ", Password: " + password);

    } 
    else if (check == "MQTTdata") {

      size_t sep = data.indexOf(';');
      if(sep != -1){
        mqttServer = data.substring(0,sep);
        data = data.substring(sep + 1);
      }
      sep = data.indexOf(';');
      if(sep != 1){
        mqttport = data.substring(0, sep);
        data = data.substring(sep + 1);
      } else {
        mqttport = "1883"; // Mặc định nếu không có port
      }
      sep = data.indexOf(';');
      if (sep != -1) {
        username = data.substring(0, sep);
        passBroker = data.substring(sep + 1);
      } else {
        username = data;
        passBroker = "";
      }
      Serial.println("MQTT Auth: " + mqttServer + " / "+ mqttport + " / " + username + " / " + passBroker);

    } 
    else if (check == "MQTT") {
      size_t sep = data.indexOf(';');
      if (sep != -1) {
        topic = data.substring(0, sep);
        message = data.substring(sep + 1);
      } else {
        topic = data;
        message = "";
      }
      publishMessage(topic.c_str(), message, true);
      Serial.println("MQTT publish -> Topic: " + topic + ", Msg: " + message);

    } 
    else if (check == "Count") {
      if (data == "start") {
        isCounting = true;
        publishMessage("Counting", String(countValue), true);
        Serial.println("Start Counting");

      } 
      else if (data == "pause") {
        isCounting = false;
        publishMessage("Counting", String(countValue), true);
        Serial.println("Pause Counting");
        publishMessage("Counting", "Pause",true);

      } 
      else if (data == "reset") {
        isCounting = true;
        countValue = 0;
        publishMessage("Counting", String(countValue), true);
        Serial.println("Reset Count");


      } 
      else if (data.startsWith("setTime")) {
        size_t semiPos = data.indexOf(';');
        if (semiPos != -1) {
          String timeStr = data.substring(semiPos + 1);
          interval = timeStr.toInt();
          Serial.print("Set interval to ");
          Serial.println(interval);
        }
      }
    
    }
    else if (check == "CAPTURE"){
      
    }
  }
};

// Kết nối MQTT
void MQTT_Connect() {
  if (!client.connected()) {
    client.setServer(mqttServer.c_str(), atoi(mqttport.c_str()));
    Serial.print("Connecting to MQTT...");
    if (client.connect(client_id.c_str(), username.c_str(), passBroker.c_str())) {
      Serial.println("Connected to MQTT Broker!");
      connectionStatusCharacteristic.setValue("MQTT is connected");
      connectionStatusCharacteristic.notify();
    } else {
      Serial.print("Failed to connect, rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}
// Khởi tạo BLE
void InitBLE() {
  BLEDevice::init(std::string("BLE_Wifi_") + chipID.c_str());
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  pBattery = pServer->createService(BatteryService);
  pBattery->addCharacteristic(&BatteryLevelCharacteristic);
  BatteryLevelDescriptor.setValue("Percentage 0 - 100");
  BatteryLevelCharacteristic.addDescriptor(&BatteryLevelDescriptor);
  BatteryLevelCharacteristic.addDescriptor(new BLE2902());

  pBattery->addCharacteristic(&connectionStatusCharacteristic);
  connectionStatusCharacteristic.addDescriptor(new BLE2902());

  BLECharacteristic *pWriteCharacteristic = pBattery->createCharacteristic(
    CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE
  );
  pWriteCharacteristic->setCallbacks(new MyCallbacks());

  pServer->getAdvertising()->addServiceUUID(BatteryService);
  pBattery->start();
  pServer->getAdvertising()->start();
}




void networksList() {
  int cnt = WiFi.scanNetworks();
  Serial.print("Số mạng quét được là: ");
  Serial.println(cnt);

  // Thông báo bắt đầu gửi WiFi list
  connectionStatusCharacteristic.setValue("WIFI_BEGIN");
  connectionStatusCharacteristic.notify();
  delay(30);

  for (int i = 0; i < cnt; i++) {
    digitalWrite(2, HIGH);
    delay(30);

    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    String security = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "OPEN" : "SECURED";

    Serial.printf("%d: %s (RSSI: %d dBm) %s\n", i + 1, ssid.c_str(), rssi, security.c_str());

    String message = "part:" + ssid + ";" + rssi + ";" + security;

    connectionStatusCharacteristic.setValue(message.c_str());
    connectionStatusCharacteristic.notify();
    delay(50);  // Cho web BLE có thời gian xử lý

    digitalWrite(2, LOW);
    delay(30);
  }

  connectionStatusCharacteristic.setValue("WIFI_END");
  connectionStatusCharacteristic.notify();
  delay(30);

  // Nhấp nháy LED báo xong
  for (int i = 0; i < 3; i++) {
    digitalWrite(2, LOW);
    delay(200);
    digitalWrite(2, HIGH);
    delay(200);
  }

  WiFi.scanDelete();  // Dọn dẹp sau khi quét
}



// Setup chính
void setup() {
  chipID = String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF), HEX);
  chipID.toUpperCase();
  Serial.println("MAC Address (Chip ID): " + chipID);
  pinMode(2, OUTPUT);
  Serial.begin(115200);
  InitBLE();
  Serial.println("BLE Initialized");

  // Nháy đèn chờ Web kết nối BLE
  while (keep) {
    digitalWrite(2, HIGH);
    delay(100);
    digitalWrite(2, LOW);
    delay(100);
  }

  // Gửi danh sách WiFi về Web
  networksList();

  // Vòng lặp: chờ đến khi WiFi kết nối đúng
  while (true) {
    // Chờ Web gửi lại thông tin WiFi
    ssid = "";
    password = "";
    while (ssid.isEmpty()) {
      client.loop(); // BLE vẫn hoạt động để nhận lệnh mới
    }

    // Kết nối WiFi
    WiFi.disconnect(true);  // xóa thông tin cũ
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.print("Connecting to WiFi");

    int retryCount = 0;
    const int maxRetry = 20;

    while (WiFi.status() != WL_CONNECTED && retryCount < maxRetry) {
      digitalWrite(2, LOW);
      delay(150);
      Serial.print(".");
      digitalWrite(2, HIGH);
      delay(150);
      retryCount++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      // Nháy nhanh báo thành công
      for (int i = 0; i < 10; i++) {
        digitalWrite(2, HIGH);
        delay(50);
        digitalWrite(2, LOW);
        delay(50);
      }

      Serial.println("\nWiFi Connected!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      String ip = "IP Address : " +  WiFi.localIP().toString() ;
      connectionStatusCharacteristic.setValue(ip.c_str());
      connectionStatusCharacteristic.notify();
      delay(100);
      connectionStatusCharacteristic.setValue("Wifi is connected");
      connectionStatusCharacteristic.notify();
      break;  

    } 
    else {
      // Nháy chậm báo lỗi
      for (int i = 0; i < 3; i++) {
        digitalWrite(2, HIGH);
        delay(400);
        digitalWrite(2, LOW);
        delay(400);
      }

      Serial.println("\nWiFi Connection Failed!");
      connectionStatusCharacteristic.setValue("Password is incorrect");
      connectionStatusCharacteristic.notify();

      // Sau khi báo sai mật khẩu, sẽ quay lại vòng lặp chờ Web gửi SSID/password mới
    }
  }

  // Tiếp tục chương trình sau khi WiFi kết nối thành công
  MQTT_Connect();
  server.begin();
}

// Loop chính
void loop() {
  client.loop();

  if (!client.connected()) {
    MQTT_Connect();
  }

  if (isCounting) {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis;
      Serial.println(countValue);
      publishMessage("Counting", String(countValue), true);
      countValue++;
      if (countValue > 100) countValue = 0;
    }
  }
}
