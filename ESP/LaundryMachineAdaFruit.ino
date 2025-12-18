#include <WiFi.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>
#include "AdafruitIO_WiFi.h"

// --- ADAFRUIT IO AYARLARI ---
#define IO_USERNAME  "yourusername"
#define IO_KEY       "yourkey"

// WiFi Bilgileri
const char* SSID = "fase";
const char* PASSWORD = "12345678";

// Adafruit IO Bağlantısı
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, SSID, PASSWORD);

// Feed Tanımlama
AdafruitIO_Feed *laundryFeed = io.feed("laundry-machine");

// RFID Pinleri
#define RST_PIN 22
#define SS_PIN 5
#define SCK_PIN 18
#define MOSI_PIN 23
#define MISO_PIN 19

// Röle Pini
#define RELAY_PIN 4

// Zamanlama Sabitleri
const unsigned long CARD_COOLDOWN_MS = 1000;
const unsigned long TRANSACTION_TIMEOUT_MS = 3000;
const unsigned long RELAY_PULSE_MS = 150;
const unsigned long RELAY_PAUSE_MS = 500;
const int COINS_PER_SCAN = 1;

// Global Nesneler
MFRC522 rfid(SS_PIN, RST_PIN);

// Durum Değişkenleri
String currentCard = "";
int totalCoins = 0;
unsigned long lastScanTime = 0;
unsigned long transactionStartTime = 0;
bool transactionInProgress = false;
bool relayActive = false;
unsigned long relayStartTime = 0;
int relayPulsesRemaining = 0;
unsigned long relayPauseTime = 0;

// Fonksiyon Protokolleri
void connectWiFi();
String readCardOnce();
void activateRelay();
void updateRelay();
void processTransaction();
void sendToAdafruitIO(String cardId, int coins);

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== SADECE ADAFRUIT IO SISTEMI ===");
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Röle Kapalı (Active-Low)
  
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  rfid.PCD_Init();
  
  // WiFi ve Adafruit IO Bağlantısı
  Serial.print("Adafruit IO'ya bağlanılıyor...");
  io.connect();
  
  while(io.status() < AIO_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\n" + String(io.statusText()));
  Serial.println("Sistem hazır. Kart okutun.");
}

void loop() {
  // Bağlantıyı canlı tut
  io.run();
  
  unsigned long now = millis();
  updateRelay();

  // Kart Okuma İşlemi
  String card = readCardOnce();
  
  if (card.length() > 0) {
    // Yeni bir kart mı yoksa mevcut işlem devam mı ediyor?
    if (!transactionInProgress || card != currentCard) {
      if (transactionInProgress && totalCoins > 0) {
        processTransaction();
      }
      currentCard = card;
      totalCoins = 0;
      transactionInProgress = true;
      transactionStartTime = now;
      Serial.println("\n--- YENİ İŞLEM BAŞLADI ---");
      Serial.println("Kart ID: " + card);
    }
    
    // Tarama cooldown kontrolü
    if (now - lastScanTime >= CARD_COOLDOWN_MS) {
      totalCoins += COINS_PER_SCAN;
      lastScanTime = now;
      transactionStartTime = now; // Zaman aşımını sıfırla
      Serial.println("Jeton: " + String(totalCoins));
    }
  }
  
  // 3 saniye boyunca yeni tarama olmazsa işlemi bitir ve gönder
  if (transactionInProgress && (now - transactionStartTime >= TRANSACTION_TIMEOUT_MS)) {
    processTransaction();
  }
}

// İşlemi sonlandıran ve Adafruit'e gönderen fonksiyon
void processTransaction() {
  if (!transactionInProgress || totalCoins == 0) return;
  
  Serial.println("\n--- İŞLEM TAMAMLANDI ---");
  Serial.println("Kart: " + currentCard);
  Serial.println("Toplam Jeton: " + String(totalCoins));
  
  // Adafruit IO'ya veriyi gönder
  sendToAdafruitIO(currentCard, totalCoins);
  
  // Otomatı çalıştır (Röle darbesi)
  relayPulsesRemaining = totalCoins;
  activateRelay();
  
  // Durumu sıfırla
  transactionInProgress = false;
  currentCard = "";
  totalCoins = 0;
}

// Adafruit IO'ya JSON gönderimi
void sendToAdafruitIO(String cardId, int coins) {
  StaticJsonDocument<128> doc;
  doc["card_id"] = cardId;
  doc["coins"] = coins;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.print("Adafruit IO'ya Gönderiliyor: ");
  Serial.println(jsonString);
  
  laundryFeed->save(coins);
}

// RFID Okuma
String readCardOnce() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return "";
  String cardId = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) cardId += "0";
    cardId += String(rfid.uid.uidByte[i], HEX);
  }
  cardId.toUpperCase();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  return cardId;
}

// Röle Kontrolü
void activateRelay() {
  if (relayActive) return;
  digitalWrite(RELAY_PIN, LOW); // Aktif
  relayActive = true;
  relayStartTime = millis();
}

void updateRelay() {
  unsigned long now = millis();
  if (relayActive) {
    if (now - relayStartTime >= RELAY_PULSE_MS) {
      digitalWrite(RELAY_PIN, HIGH); // Pasif
      relayActive = false;
      relayPulsesRemaining--;
      if (relayPulsesRemaining > 0) relayPauseTime = now;
    }
  } else if (relayPulsesRemaining > 0) {
    if (now - relayPauseTime >= RELAY_PAUSE_MS) activateRelay();
  }
}