#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>
#include "AdafruitIO_WiFi.h"
// PINS
#define RST 22
#define SDA 5
#define MOSI 23
#define MISO 19
#define SCK 18
////----RELAY PIN
#define relay 4

// --- ADAFRUIT IO AYARLARI ---
#define IO_USERNAME   "yourUSERNAME"
#define IO_KEY        "yourkey"
// ====STATION MODE
const char* SSID = "fase";
const char* PASSWORD = "12345678";

// --- ACCESS POINT 
const char* AP_SSID = "ESP32-RFID-Controller";
const char* AP_PASSWORD = "laundry123";

const char* SERVER_URL = "http://192.168.4.4:5000/scan_card";  // Pi should have this static IP
const char* SCAN_CARD_URL = "yourURL"; //MUTABLE...

//RFID READER OBJECT
MFRC522 rfid(SDA, RST);
// ADAFRUIT OBJECT
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, SSID, PASSWORD);// AP_SSID, AP_PASSWORD
//feed Tanimlama
AdafruitIO_Feed *coin_feed = io.feed("laundry-machine");

// Timing constants- ZAMANLAMA DEGERLERI
const unsigned long CARD_COOLDOWN_MS = 1000;         // Time between individual scans (1 second)
const unsigned long TRANSACTION_TIMEOUT_MS = 4000;   // Wait 3 seconds after last scan to finalize transaction
const unsigned long RELAY_PULSE_MS = 150;            // Relay pulse duration
const unsigned long RELAY_PAUSE_MS = 500;
const float BACKOFF_BASE = 1.5;
const unsigned long BACKOFF_MAX_MS = 10000;

// State variables = DURUM DEGISKENLERI
// String lastCard = "";
// unsigned long lastCardTime = 0;
const int COINS_PER_SCAN = 1;
int totalCoins = 0;
unsigned long lastScanTime = 0;
unsigned long transactionStartTime = 0;
bool transactionInProgress = false;
bool relayActive = false;
unsigned long relayStartTime = 0;
int relayPulsesRemaining = 0;
unsigned long relayPauseTime = 0;
String currentCard = "";


// Function declarations
bool connectWiFi(int timeout=20);//WIFI
bool setupAccessPoint();//AP

String readCardOnce();
void activateRelay();
void updateRelay();
void processTransaction();

bool sendCardToServer(String cardId);bool scanCardDisplay(String cardId);// FLASK SERVER
void sendToAda(String id, int coins);// ADAFRUIT IO   
bool sendTransactionToServer(String cardId, int coins, String machine_id);// FLASK SERVER


void setup(){
  Serial.begin(115200);
  delay(1000);
  // ---RELAY OUTPUT
  pinMode(relay,OUTPUT);
  digitalWrite(relay,HIGH);//role kapali = relay OFF
  SPI.begin(SCK,MISO,MOSI,SDA);
  rfid.PCD_Init();

  // Serial.println("Baglaniyor...")
  // io.connect();
  //connectWiFi(15);
  //setupAccessPoint();

  //  while(io.status() < AIO_CONNECTED) {
  //   Serial.print(".");
  //   delay(500);
  // }

  Serial.println("\n" + String(io.statusText()));
  Serial.println("Sistem hazır. Kart okutun.");
  Serial.println("MFRC522 initialized!!!");
  rfid.PCD_DumpVersionToSerial();


  Serial.println("Setup complete. Ready to scan cards.");
  Serial.println("Clients can connect to: " + String(AP_SSID));
  Serial.println("Server URL: " + String(SERVER_URL));
}


void loop(){
  // io.run();
  unsigned long now = millis();

  updateRelay();//ALWAYS check relay - DAIMA roleyi kontrol et!

  // if(WiFi.status()!= WL_CONNECTED){
  //   Serial.println("wifi disconnected, reconnecting...");
  //   connectWiFi(15);
  //   delay(1000);
  //   return;
  // }

  String card = readCardOnce();
  if(card.length>0){
    scanCardDisplay(card);

    if(!transactionInProgress || card != currentCard){
      if(transactionInProgress && totalCoins > 0){
        processTransaction();
      }

      // Yeni islem baslat = new transaction
      currentCard = card;
      totalCoins = 0;
      transactionInProgress = true;
      transactionStartTime = now;
      Serial.println("\n╔════════════════════════════════╗");
      Serial.println("  NEW TRANSACTION STARTED");
      Serial.print("  Card: ");
      Serial.println(card);
      Serial.println("╚════════════════════════════════╝");

    }

  // Sure kontrolu = Cooldown between scans
    if(now - lastScanTime >= CARD_COOLDOWN_MS){
      totalCoins += COINS_PER_SCAN;
      lastScanTime = now;
      transactionStartTime = now;

      Serial.print("► Scan #");
      Serial.print(totalCoins);
      Serial.print(" detected | Total coins: ");
      Serial.println(totalCoins);
    }

  }

  if(transactionInProgress && (now - transactionStartTime >= TRANSACTION_TIMEOUT_MS)){
    processTransaction();
  }

  // delay(100);
}


// SET UP AP
bool setupAccessPoint(){
  Serial.println("Setting up AP: ");
  Serial.println(AP_SSID);

  // configure as AP
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("Failed to create Access Point!");
    return false;
  }

  delay(1000);  // Give AP time to start
  
  Serial.println("Access Point created successfully!");
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("AP MAC address: ");
  Serial.println(WiFi.softAPmacAddress());
  
  // Print connection info for clients
  Serial.println("=== Network Information ===");
  Serial.println("SSID: " + String(AP_SSID));
  Serial.println("Password: " + String(AP_PASSWORD));
  Serial.println("Gateway: 192.168.4.1");
  Serial.println("Subnet: 255.255.255.0");
  Serial.println("Recommended client IP range: 192.168.4.2 - 192.168.4.100");
  Serial.println("===========================");
  return true;
}

// CONNECT TO WIFI
bool connectWiFi(int timeout){
  if(WiFi.status()==WL_CONNECTED){
    Serial.println("connected already!!");
    return true;
  }
  Serial.println("Connecting to WiFi: ");
  Serial.println(SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD)
  int attempts = 0;
  while(WiFi.status()!=WL_CONNECTED && attempts<timeout){
    delay(1000);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\nWiFi connection failed!");
    return false;
  }

  
}

//PROCESS TRANSACTION
void processTransaction(){
  if(!transactionInProgress || totalCoins ==0){
    return;
  }
  Serial.println("\n══════════════════════════════════");
  Serial.println("  FINALIZING TRANSACTION");
  Serial.print("  Card: ");
  Serial.println(currentCard);
  Serial.print("  Total Coins: ");
  Serial.println(totalCoins);
  Serial.println("══════════════════════════════════");

  if(sendTransactionToServer(currentCard, totalCoins, "laundry_machine_1")){
    Serial.println("Successful");
  } else{
    Serial.println("Failed!!");
  }

  // Reset Transaction state
  transactionInProgress = false;
  currentCard = "";
  totalCoins = 0;

  Serial.println("==========================\n");
}

//Just send card TAG
bool scanCardDisplay(String cardId){
  HTTPClient http;
  http.begin(SCAN_CARD_URL);
  // http.begin(SERVER_URL);

  http.addHeader("Content-Type","application/json");

  StaticJsonDocument<64> doc;
  doc["card_id"] = cardId;

  String jsonString;
  serializeJson(doc, jsonString);

  int httpResponseCode = http.POST(jsonString);
  http.end();
  
  return(httpResponseCode>0)
}

//Main api POST to server side and get response from server
bool sendTransactionToServer(String cardId, int coins, String machine_id) {
  float backoff = BACKOFF_BASE;
  int maxRetries = 3;
  int retryCount = 0;
  
  while (retryCount < maxRetries) {
    HTTPClient http;
    
    http.begin(SCAN_CARD_URL);
    // http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");
    
    // Create JSON payload with total coins
    StaticJsonDocument<256> doc;
    doc["card_id"] = cardId;
    doc["coins"] = coins;
    doc["machine_id"] = machine_id;
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    Serial.println("→ Sending to server: " + jsonString);
    
    int httpResponseCode = http.POST(jsonString);
    
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("← HTTP Response code: ");
      Serial.println(httpResponseCode);
      Serial.print("← Server response: ");
      Serial.println(response);
      
      // Parse JSON response
      StaticJsonDocument<512> responseDoc;
      DeserializationError error = deserializeJson(responseDoc, response);
      
      if (!error) {
        bool success = responseDoc["success"] | false;
        bool userExists = responseDoc["user_exists"] | false;
        bool shouldActivate = responseDoc["activate_machine"] | false;
        const char* message = responseDoc["message"] | "No message";
        
        Serial.println("┌─────────────────────────────┐");
        Serial.print("│ Success: ");
        Serial.println(success ? "YES" : "NO");
        Serial.print("│ User exists: ");
        Serial.println(userExists ? "YES" : "NO");
        Serial.print("│ Activate machine: ");
        Serial.println(shouldActivate ? "YES" : "NO");
        Serial.print("│ Message: ");
        Serial.println(message);
        
        if (responseDoc.containsKey("balance")) {
          int balance = responseDoc["balance"];
          Serial.print("│ Remaining balance: ");
          Serial.println(balance);
        }
        if (responseDoc.containsKey("coins_used")) {
          int coinsUsed = responseDoc["coins_used"];
          Serial.print("│ Coins used: ");
          Serial.println(coinsUsed);
        }
        Serial.println("└─────────────────────────────┘");
        
        if (success && shouldActivate && userExists) {
          Serial.println("🔌 Activating laundry machine...");
          relayPulsesRemaining = coins; // Set number of pulses
          Serial.print("⚡ Will pulse relay ");
          Serial.print(coins);
          Serial.println(" times");
          activateRelay(); // Start first pulse
          sendToAda(cardId,coins);
        }
      } else {
        Serial.print("✗ JSON parse error: ");
        Serial.println(error.c_str());
      }
      
      http.end();
      return true;
    } else {
      Serial.print("✗ HTTP Error code: ");
      Serial.println(httpResponseCode);
      Serial.println("✗ Error: " + http.errorToString(httpResponseCode));
      http.end();
      
      // Exponential backoff with cap
      unsigned long sleepTime = min((unsigned long)(backoff * 1000), BACKOFF_MAX_MS);
      Serial.print("⏳ Retrying in ");
      Serial.print(sleepTime / 1000);
      Serial.println(" seconds...");
      
      delay(sleepTime);
      backoff = min(backoff * 2.0f, (float)(BACKOFF_MAX_MS / 1000));
      retryCount++;
    }
  }
  
  Serial.println("✗ Max retries reached, giving up");
  return false;
}

// ADAFRUIT IO
void sendToAda(String id, int coins){
  StaticJsonDocument<64> doc;
  doc["card_id"] = cardId;
  doc["coins"] = coins;
  doc["username"] = username;

  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.print("Adafruit IO'ya gonderiliyor: "+jsonString);
  coin_feed->save(coins);
  // cardId_feed->save(cardId);
}


String readCardOnce(){
  if(!rfid.PICC_IsNewCardPresent()){
    return "";
  }

  if(!rfid.PICC_ReadCardSerial()){
    return "";
  }

  String cardId = "";
  for(byte i = 0; i<rfid.uid.size; i++){
    if(rfid.uid.uidByte[i]<0x10){
      cardId+="0";
    }
    cardId += String(rfid.uid.uidByte[i], HEX);
  }
   cardId.toUpperCase();
  
  // Halt PICC
  rfid.PICC_HaltA();
  // Stop encryption on PCD
  rfid.PCD_StopCrypto1();
  
  return cardId;
}

// Activate RELAY==
void activateRelay() {
  if (relayActive) {
    Serial.println("⚠ Relay already active, skipping");
    return;
  }
  
  Serial.println("╔═══════════════════════════╗");
  Serial.println("║   RELAY ACTIVATED         ║");
  Serial.println("╚═══════════════════════════╝");
  
  digitalWrite(RELAY_PIN, LOW);   // Relay ON (active-low)
  relayActive = true;
  relayStartTime = millis();
  
  Serial.print("⚡ Relay ON for ");
  Serial.print(RELAY_PULSE_MS);
  Serial.println(" ms");
}

void updateRelay(){
  unsigned long now = millis();

  if(relayActive){
    if(now-relayStartTime >= RELAY_PULSE_MS){
      digitalWrite(RELAY_PIN, HIGH);
      relayActive = false;
      relayPulsesRemaining--;

      Serial.print("⚡ Relay pulse completed. Remaining pulses: ");
      Serial.println(relayPulsesRemaining);
      
      // If more pulses needed, start pause timer
      if (relayPulsesRemaining > 0) {
        relayPauseTime = now;
      }
    }
  }

  else if (relayPulsesRemaining > 0) {
    if (now - relayPauseTime >= RELAY_PAUSE_MS) {
      // Start next pulse
      activateRelay();
    }
  }

}