#include <SPI.h>
#include <LoRa.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED Ekran Ayarları
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// LoRa Pin Tanımlamaları
#define LORA_SCK     5
#define LORA_MISO    19
#define LORA_MOSI    27
#define LORA_SS      18
#define LORA_RST     14
#define LORA_DIO0    26
#define GPIO2_PIN    2

// GPS için Serial2 kullanımı (ESP32)
#define GPS_RX_PIN 13
#define GPS_TX_PIN 12

// APRS Ayarları
const char* CALLSIGN = "TA3OER-6";  // Çağrı işaretinizi buraya yazın
const char* DESTINATION = "APLTE";
const char SYMBOL_TABLE = '/';       // APRS sembol tablosu
const char SYMBOL = '>';             // Araç sembolü
const char* WIDE = "WIDE1-1";             // 

//APRS Değişkenleri
//String trackerCallSign ="TA3OER-6";          // Tracker çağrı işareti
//String digiCallSign ="TR0Y-5";          // Digi çağrı işareti
//String symbolCode = ">";            // Araç sembolü
//String symbolTable = "/";           // Sembol tablosu
String statusMessage = " TROY LoRa Tracker by TA3OER";
String aprsComment = " LoRa iGate/Digirepeater on 433.775mhz";
String header = "<\xff\x01";     
//String wide ="WIDE1-1";

// Smart Beacon Parametreleri
const float slowSpeed = 10.0;      // km/h -Cihazın hızı bu değerin altındaysa, slowRate değeri kullanılarak beacon gönderme aralığı belirlenir.
const float fastSpeed = 60.0;     // km/h -Cihazın hızı bu değerin üzerindeyse, fastRate değeri kullanılarak beacon gönderme aralığı belirlenir.
const float slowRate = 300.0;     // saniye -Eğer cihazın hızı slowSpeed değerinin altındaysa, beacon her slowRate saniyede bir gönderilir.
const float fastRate = 60.0;      // saniye -Eğer cihazın hızı fastSpeed değerinin üzerindeyse, beacon her fastRate saniyede bir gönderilir.
const float minTxDist = 100.0;    // metre -Cihaz, son beacon gönderildiği konumdan en az minTxDist metre uzaklaşmadan yeni bir beacon göndermez.
const float minDeltaBeacon = 40.0;  // saniye -Son beacon gönderildikten sonra en az minDeltaBeacon saniye geçmeden yeni bir beacon gönderilmez.
const float turnMinDeg = 28.0;    // derece -Cihazın yönü turnMinDeg değerinden fazla değişirse, beacon hemen gönderilir.
const float turnSlope = 255.0;    // eğim faktörü -Yön değişikliği arttıkça beacon gönderme sıklığı artar. Bu değer, yön değişikliğinin beacon sıklığına etkisini belirler.

// Global değişkenler
TinyGPSPlus gps;
float lastLat = 0.0;
float lastLon = 0.0;
float lastCourse = 0.0;
unsigned long lastBeaconTime = 0;
float currentSpeed = 0.0;
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 1000; // 1 saniye

// Zaman takibi için değişkenler (txstatus ve txcomment)
unsigned long previousMillis = 0;
int currentMinute = 0;

// Fonksiyon prototipleri
void displayError(const char* error);
void displayStartup();
void updateDisplay();
void sendBeacon();
void Txstatus();
void Txcomment();
void blinkGPIO2();
String convertToAPRSFormat(double lat, double lon, float speed, float course);
float calculateBeaconRate(float speed);
float calculateTurnTime(float courseDelta);
float calculateDistance(float lat1, float lon1, float lat2, float lon2);

// Fonksiyon tanımlamaları
void displayError(const char* error) {
  display.clearDisplay();
  display.setCursor(0,0);
  display.println(F("ERROR:"));
  display.println(error);
  display.display();
}

void displayStartup() {
  display.clearDisplay();
  display.setCursor(0,0);
  display.println(F("APRS Smart Beacon"));
  display.println(F("----------------"));
  display.println(CALLSIGN);
  display.println(F("LoRa: OK"));
  display.println(F("GPS: Waiting..."));
  display.display();
}

void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0,0);
  
  // Üst bilgi
  display.println(CALLSIGN);
  display.println("----------------");
  
  // GPS Durumu
  if (gps.location.isValid()) {
    // Konum
    display.print("Lat: ");
    display.println(gps.location.lat(), 6);
    display.print("Lon: ");
    display.println(gps.location.lng(), 6);
    
    // Hız ve Yön
    display.print("Spd: ");
    display.print(gps.speed.kmph(), 1);
    display.print("km/h ");
    display.print(gps.course.deg(), 0);
    display.println("deg");
    
    // Son beacon zamanı
    unsigned long secsSinceLastBeacon = (millis() - lastBeaconTime) / 1000;
    display.print("Last Tx: ");
    display.print(secsSinceLastBeacon);
    display.println("s");
  } else {
    display.println("GPS: No Fix");
  }
  
  // Uydu sayısı
  display.print("Sats: ");
  display.println(gps.satellites.value());
  
  display.display();
}

// APRS formatında konum kodlama
String convertToAPRSFormat(double lat, double lon, float speed, float course) {
  char aprsPacket[100];
  
  // Enlem hesaplama
  char latStr[9];
  int latDeg = abs((int)lat);
  float latMin = (abs(lat) - latDeg) * 60;
  snprintf(latStr, sizeof(latStr), "%02d%05.2f%c", 
           latDeg, latMin, (lat >= 0) ? 'N' : 'S');

  // Boylam hesaplama
  char lonStr[10];
  int lonDeg = abs((int)lon);
  float lonMin = (abs(lon) - lonDeg) * 60;
  snprintf(lonStr, sizeof(lonStr), "%03d%05.2f%c", 
           lonDeg, lonMin, (lon >= 0) ? 'E' : 'W');

  // Hız (knots) ve yön
  float speedKnots = speed * 0.539957; // km/h to knots

  // APRS paket formatı oluşturma
  snprintf(aprsPacket, sizeof(aprsPacket), 
           "%s>%s,%s:!%s%c%s%c%03.0f/%03.0f/A=%06.0f",
           CALLSIGN, DESTINATION, WIDE,
           latStr, SYMBOL_TABLE, lonStr, SYMBOL,
           course, speedKnots,
           gps.altitude.feet());

  return String(aprsPacket);
}

float calculateBeaconRate(float speed) {
  if (speed < slowSpeed) return slowRate;
  if (speed > fastSpeed) return fastRate;
  
  return slowRate - ((speed - slowSpeed) * (slowRate - fastRate) / (fastSpeed - slowSpeed));
}

float calculateTurnTime(float courseDelta) {
  if (abs(courseDelta) < turnMinDeg) return 1.0;
  return 1.0 + (abs(courseDelta) / turnSlope);
}

float calculateDistance(float lat1, float lon1, float lat2, float lon2) {
  const float R = 6371000; // Dünya yarıçapı (metre)
  float phi1 = lat1 * PI / 180;
  float phi2 = lat2 * PI / 180;
  float deltaPhi = (lat2 - lat1) * PI / 180;
  float deltaLambda = (lon2 - lon1) * PI / 180;

  float a = sin(deltaPhi/2) * sin(deltaPhi/2) +
          cos(phi1) * cos(phi2) *
          sin(deltaLambda/2) * sin(deltaLambda/2);
  float c = 2 * atan2(sqrt(a), sqrt(1-a));
  return R * c;
}

void sendBeacon() {
  if (gps.location.isValid()) {
    String aprsPacket = convertToAPRSFormat(
      gps.location.lat(),
      gps.location.lng(),
      gps.speed.kmph(),
      gps.course.deg()
    );
                       
    LoRa.beginPacket();
    LoRa.print(String(header) + aprsPacket);
    LoRa.endPacket();
    blinkGPIO2();
    Serial.println("APRS Beacon gönderildi: " + aprsPacket);
    
    lastLat = gps.location.lat();
    lastLon = gps.location.lng();
    lastCourse = gps.course.deg();
    lastBeaconTime = millis();
  }
}
//------------------------------------------------------------------------------------------------//
//------------------------------------------------------------------------------------------------//
void setup() {
  Serial.begin(115200);

  // OLED başlatma
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 başlatılamadı"));
    while(1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.display();
  
  // GPS başlatma
  Serial2.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  
  // LoRa başlatma
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  
  if (!LoRa.begin(433.775E6)) {
    Serial.println("LoRa başlatılamadı!");
    displayError("LoRa Error!");
    while (1);
  }
  
  // LoRa parametreleri
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setTxPower(19);
  LoRa.enableCrc();
  
  displayStartup();
  Serial.println("APRS Smart Beacon başlatıldı!");
  
}
//------------------------------------------------------------------------------------------------//
//------------------------------------------------------------------------------------------------//
void loop() {
  while (Serial2.available() > 0) {
    gps.encode(Serial2.read());
  }

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= 60000) {
    previousMillis = currentMillis;
    currentMinute = (currentMinute + 1) % 60;
    
    if (currentMinute == 30 || currentMinute == 59) {
      Txstatus();
    }
    if (currentMinute == 5 || currentMinute == 25 || currentMinute == 45) {
      Txcomment();
    }
  }

  int packetSize = LoRa.parsePacket();
  if (packetSize) {
  blinkGPIO2();
    
    String packet = "";
    while (LoRa.available()) {
      packet += (char)LoRa.read();
    }

    // OLED ekranda son alınan paketi göster
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Packet Received!");
    display.setCursor(0,10);
    display.println(packet.substring(0, 20)); // İlk 20 karakter
    display.setCursor(0,20);
    display.print("RSSI: ");
    display.println(LoRa.packetRssi());
    display.display();
    delay(2000); // 2 saniye göster

    Serial.print("Alınan Mesaj: ");
    Serial.print(packet);
    Serial.print(" RSSI: ");
    Serial.print(LoRa.packetRssi());
    Serial.print(" SNR: ");
    Serial.println(LoRa.packetSnr());
  }
  // Ekran güncelleme
  if (millis() - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  if (gps.location.isValid()) {
    currentSpeed = gps.speed.kmph();
    float currentLat = gps.location.lat();
    float currentLon = gps.location.lng();
    float currentCourse = gps.course.deg();
    
    unsigned long currentTime = millis();
    float timeSinceLastBeacon = (currentTime - lastBeaconTime) / 1000.0;
    
    // Mesafe kontrolü
    float distance = calculateDistance(lastLat, lastLon, currentLat, currentLon);
    
    // Yön değişimi kontrolü
    float courseDelta = abs(currentCourse - lastCourse);
    if (courseDelta > 180) courseDelta = 360 - courseDelta;
    
    // Beacon gönderme kararı
    float beaconRate = calculateBeaconRate(currentSpeed);
    float turnFactor = calculateTurnTime(courseDelta);
    float adjustedRate = beaconRate / turnFactor;
    
    bool shouldSendBeacon = false;
    
    // Minimum mesafe kontrolü
    if (distance >= minTxDist && timeSinceLastBeacon >= minDeltaBeacon) {
      shouldSendBeacon = true;
    }
    
    // Zaman kontrolü
    if (timeSinceLastBeacon >= adjustedRate && timeSinceLastBeacon >= minDeltaBeacon) {
      shouldSendBeacon = true;
    }
    
    if (shouldSendBeacon) {
      sendBeacon();
    }
  }
}

void Txcomment() {
  String packet = String(CALLSIGN) + ">" + DESTINATION + "," + WIDE + ":>" + aprsComment;
  
  LoRa.beginPacket();
  LoRa.print(String(header) + packet);
  LoRa.endPacket();
  blinkGPIO2();
  Serial.println("APRS comment mesaj yollandı: " + packet);
}

void Txstatus() {
  if (gps.location.isValid()) {
    String basePacket = convertToAPRSFormat(
      gps.location.lat(),
      gps.location.lng(),
      gps.speed.kmph(),
      gps.course.deg()
    );
    
    // Add comment to the base packet
    String fullPacket = basePacket + " " + statusMessage;
    
    LoRa.beginPacket();
    LoRa.print(String(header) + fullPacket);
    LoRa.endPacket();
    blinkGPIO2();
    Serial.println("APRS status mesaj yollandı: " + fullPacket);
  }
}

void blinkGPIO2() {
  digitalWrite(GPIO2_PIN, HIGH);
  delay(200);
  digitalWrite(GPIO2_PIN, LOW);
}