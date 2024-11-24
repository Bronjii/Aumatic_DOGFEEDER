#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP8266HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Servo.h>

// ต่อเน็ต
const char* ssid = "Your WiFi";
const char* password = "Your password";

// พินขาแต่ละตัว
#define LED_PIN D0
#define TRIG_PIN D4
#define ECHO_PIN D5
#define SERVO1_PIN D2
#define SERVO2_PIN D3

// HC-SR04
#define MIN_DISTANCE 0
#define MAX_DISTANCE 100

// ตัวเชื่อม webnetpie
WiFiClient espClient;
PubSubClient client(espClient);

const char* mqtt_server = "broker.netpie.io";
const int mqtt_port = 1883;
const char* mqtt_client_id = "Client ID";
const char* mqtt_username = "TOKEN";
const char* mqtt_password = "Secret";

//LineNotify
const char* lineToken = "Line_Token(noysupport in 2025)";

//ultrasonic sensor
long duration;
int distance;
int filteredDistance;

// NTP ตัวเทียบเวลาในคอมให้ตรงกับโค้ด
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200);  // +7 GMT (25200 seconds)

// Servo
Servo servo1;
Servo servo2;

// การควบคุมระบบ auto
bool autoMode = false;
bool lastFeedingDone8 = false;
bool lastFeedingDone18 = false;
unsigned long lastFeedingTime = 0;
const unsigned long FEEDING_COOLDOWN = 5000;  // 5 seconds minimum between feedings

// ตัวเช็คค่าของการวัดระยะ 5 ครั้งแล้วเอาค่าที่แน่นอนที่สุด
int filterDistance(int currentDistance) {
  static int readings[5];
  static int readIndex = 0;
  static int total = 0;

  total = total - readings[readIndex];
  readings[readIndex] = currentDistance;
  total = total + readings[readIndex];
  readIndex = (readIndex + 1) % 5;
  return total / 5;
}

int measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  duration = pulseIn(ECHO_PIN, HIGH);
  distance = duration * 0.034 / 2;

  if (distance > MAX_DISTANCE) distance = MAX_DISTANCE;
  if (distance < MIN_DISTANCE) distance = MIN_DISTANCE;

  return filterDistance(distance);
}

void setupWiFi() {
  Serial.print("Connecting to WiFi SSID: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Received message from topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  Serial.println(message);

  if (String(topic) == "@msg/cmd") {
    if (message == "auto") {
      autoMode = true;
      client.publish("@msg/status", "Auto mode enabled");
    } else if (message == "stop") {
      autoMode = false;
      client.publish("@msg/status", "Auto mode disabled");
    }
  }

  if (String(topic) == "@msg/feed") {
    if (message == "small") {
      feedSmall();
    } else if (message == "medium") {
      feedMedium();
    } else if (message == "large") {
      feedLarge();
    }
  }
}

void connectToNETPIE() {
  while (!client.connected()) {
    Serial.println("Connecting to NETPIE MQTT...");

    if (client.connect(mqtt_client_id, mqtt_username, mqtt_password)) {
      Serial.println("Connected to NETPIE");
      client.subscribe("@msg/#");
    } else {
      Serial.print("Connection failed, rc=");
      Serial.print(client.state());
      Serial.println(" Retrying in 5 seconds");
      delay(5000);
    }
  }
}

void sendToLineNotify(String message) {
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  String lineMessage = "message=" + message;

  http.begin(client, "https://notify-api.line.me/api/notify");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Authorization", String("Bearer ") + lineToken);
  int httpResponseCode = http.POST(lineMessage);

  Serial.print("Sent to Line Notify: ");
  Serial.println(message);

  http.end();
}

void controlServo1(int cycles) {
  for (int i = 0; i < cycles; i++) {
    servo1.write(45);
    delay(1000);
    servo1.write(0);
    delay(1000);
  }
}

void controlServo2() {
  servo2.write(90);
  delay(4000);
  servo2.write(0);
}

void feedSmall() {
  if (millis() - lastFeedingTime < FEEDING_COOLDOWN) {
    client.publish("@msg/status", "Cannot feed: Please wait for cooldown");
    return;
  }

  controlServo1(2);  // Servo1 ทำงาน 2 ครั้ง
  controlServo2();

  lastFeedingTime = millis();
  client.publish("@msg/status", "small");
  sendToLineNotify("Feeding complete");
}

void feedMedium() {
  if (millis() - lastFeedingTime < FEEDING_COOLDOWN) {
    client.publish("@msg/status", "Cannot feed: Please wait for cooldown");
    return;
  }

  controlServo1(3);  // Servo1 ทำงาน 3 ครั้ง
  controlServo2();

  lastFeedingTime = millis();
  client.publish("@msg/status", "medium");
  sendToLineNotify("Feeding complete");
}

void feedLarge() {
  if (millis() - lastFeedingTime < FEEDING_COOLDOWN) {
    client.publish("@msg/status", "Cannot feed: Please wait for cooldown");
    return;
  }

  controlServo1(4);  // Servo1 ทำงาน 4 ครั้ง
  controlServo2();

  lastFeedingTime = millis();
  client.publish("@msg/status", "large");
  sendToLineNotify("Feeding complete");
}

void checkAutoFeeding() {
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();

  // เช็คค่าเวลา 8:00 ถ้าเกินเวลาแล้วไม่ให้ ระบบก็จะ ออโต้แล้วให้อาหารเป็น meduim แทน
  if (currentHour == 8 && currentMinute == 0 && !lastFeedingDone8) {
    feedMedium();
    lastFeedingDone8 = true;
    client.publish("@msg/status", "Auto feeding at 8:00 - Medium portion");
  } else if (currentHour != 8) {
    lastFeedingDone8 = false;
  }

  // เช็คค่าเวลา 18:00 ถ้าเกินเวลาแล้วไม่ให้ ระบบก็จะ ออโต้แล้วให้อาหารเป็น meduim แทน
  if (currentHour == 18 && currentMinute == 0 && !lastFeedingDone18) {
    feedMedium();
    lastFeedingDone18 = true;
    client.publish("@msg/status", "Auto feeding at 18:00 - Medium portion");
  } else if (currentHour != 18) {
    lastFeedingDone18 = false;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // ก่อนเริ่มการทำงานให้ servo ทุกตัวกลับมาที่ 0 องศา
  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);
  servo1.write(0);  //servo1 0 องศา
  servo2.write(0);  //servo2 0 องศา

  setupWiFi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  timeClient.begin();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, reconnecting...");
    setupWiFi();
  }

  if (!client.connected()) {
    connectToNETPIE();
  }

  client.loop();
  timeClient.update();

  if (autoMode) {
    checkAutoFeeding();
  }

  static unsigned long lastPublish = 0;

  // ส่งค่า ทุกๆ 10 วินาที
  if (millis() - lastPublish > 10000) {
    lastPublish = millis();

    int currentDistance = measureDistance();
    int percentage = map(currentDistance, MIN_DISTANCE, MAX_DISTANCE, 0, 100);

    String payload = "{\"data\":{"
                     "\"distance\":"
                     + String(currentDistance) + ","
                                                 "\"percentage\":"
                     + String(percentage) + ","
                                            "\"timestamp\":\""
                     + String(millis()) + "\""
                                          "}}";

    Serial.print("Distance: ");
    Serial.print(currentDistance);
    Serial.print(" cm (");

    String currentStatus;
    if (currentDistance >= 0 && currentDistance <= 4) {
      currentStatus = "อาหารเต็ม";
    } else if (currentDistance >= 5 && currentDistance <= 7) {
      currentStatus = "อาหารเหลือปานกลาง";
    } else if (currentDistance >= 8) {
      currentStatus = "อาหารเหลือน้อย";
    }

    sendToLineNotify(currentStatus);

    Serial.println("Sending data to NETPIE: " + payload);
    client.publish("@shadow/data/update", payload.c_str());
  }
}
