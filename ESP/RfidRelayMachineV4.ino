#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "AdafruitIO_WiFi.h"

// ============== PIN DEFINITIONS ==============
#define RST_PIN 22
#define SDA_PIN 5
#define MOSI_PIN 23
#define MISO_PIN 19
#define SCK_PIN 18
#define RELAY_PIN 4

// ============== NETWORK MODE ==============
enum WiFiMode {
  MODE_STATION,    // Connect to existing WiFi (with internet)
  MODE_AP_ONLY     // Access Point only (local network)
};

WiFiMode currentMode = MODE_STATION; // Start with Station mode, fallback to AP

// ============== STATION MODE CONFIG ==============
const char* STATION_SSID = "DUZCEYURDU";
const char* STATION_PASSWORD = "duzceyurdu34!";
const char* STATION_SERVER_URL = "http://172.16.3.191:5000//scan_card";

// ============== ACCESS POINT CONFIG ==============
const char* AP_SSID = "ESP32-RFID-Controller";
const char* AP_PASSWORD = "laundry123";
const char* AP_SERVER_URL = "http://192.168.4.4:5000/scan_card";

// ============== ADAFRUIT IO CONFIG ==============
#define IO_USERNAME  ""
#define IO_KEY       ""

// ============== SYSTEM CONSTANTS ==============
const int MAX_COINS_PER_TRANSACTION = 10;
const int COINS_PER_SCAN = 1;
const unsigned long CARD_COOLDOWN_MS = 1000;
const unsigned long TRANSACTION_TIMEOUT_MS = 4000;
const unsigned long RELAY_PULSE_MS = 150;
const unsigned long RELAY_PAUSE_MS = 500;
const float BACKOFF_BASE = 1.5;
const unsigned long BACKOFF_MAX_MS = 10000;
const unsigned long WIFI_RETRY_INTERVAL = 30000; // Try reconnecting every 30s
const int WIFI_CONNECT_TIMEOUT = 20; // 20 seconds timeout

// ============== GLOBAL OBJECTS ==============
MFRC522 rfid(SDA_PIN, RST_PIN);
AdafruitIO_WiFi *io = nullptr; // Pointer, only initialize in Station mode

// Feeds (only used in Station mode)
AdafruitIO_Feed *coin_feed = nullptr;

// ============== STATE VARIABLES ==============
int totalCoins = 0;
unsigned long lastScanTime = 0;
unsigned long transactionStartTime = 0;
bool transactionInProgress = false;
bool relayActive = false;
unsigned long relayStartTime = 0;
int relayPulsesRemaining = 0;
unsigned long relayPauseTime = 0;
String currentCard = "";
unsigned long lastWiFiCheck = 0;
bool adafruitConnected = false;

// ============== FUNCTION DECLARATIONS ==============
void setupNetworking();
bool connectStationMode();
bool setupAccessPoint();
void checkAndReconnectWiFi();
void initializeAdafruitIO();
void setupOTA();

String readCardOnce();
void activateRelay();
void updateRelay();
void processTransaction();

bool scanCardDisplay(String cardId);
bool sendTransactionToServer(String cardId, int coins, String machine_id);
void sendToAdafruit(String cardId, int coins);

String getServerURL();
String getModeString();


// ============================================
//              SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n╔════════════════════════════════════════╗");
  Serial.println("║  ESP32 RFID Laundry Controller v2.0   ║");
  Serial.println("║  Dual Mode: Station/AP + OTA Support   ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  // Configure relay pin
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Relay OFF (active-low)
  
  // Initialize SPI and RFID
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SDA_PIN);
  rfid.PCD_Init();
  
  Serial.println("✓ RFID Reader initialized");
  rfid.PCD_DumpVersionToSerial();
  
  // Setup networking (tries Station, falls back to AP)
  setupNetworking();
  
  // Setup OTA updates
  setupOTA();
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║         SYSTEM READY                   ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("Mode: " + getModeString());
  Serial.println("Server: " + getServerURL());
  Serial.println("Max coins per transaction: " + String(MAX_COINS_PER_TRANSACTION));
  Serial.println("\nWaiting for cards...\n");
}


// ============================================
//              MAIN LOOP
// ============================================
void loop() {
  unsigned long now = millis();
  
  // Handle OTA updates
  ArduinoOTA.handle();
  
  // Handle Adafruit IO (only in Station mode)
  if (currentMode == MODE_STATION && adafruitConnected && io != nullptr) {
    io->run();
  }
  
  // Check relay state
  updateRelay();
  
  // Periodic WiFi check (only in Station mode)
  if (currentMode == MODE_STATION) {
    checkAndReconnectWiFi();
  }
  
  // Read RFID card
  String card = readCardOnce();
  
  if (card.length() > 0) {
    // Display card on server
    scanCardDisplay(card);
    
    // Check if this is a new transaction or continuation
    if (!transactionInProgress || card != currentCard) {
      // New card detected - finalize previous transaction if exists
      if (transactionInProgress && totalCoins > 0) {
        processTransaction();
      }
      
      // Start new transaction
      currentCard = card;
      totalCoins = 0;
      transactionInProgress = true;
      transactionStartTime = now;
      
      Serial.println("\n╔════════════════════════════════════════╗");
      Serial.println("║      NEW TRANSACTION STARTED           ║");
      Serial.println("╚════════════════════════════════════════╝");
      Serial.println("Card ID: " + card);
      Serial.println("Mode: " + getModeString());
    }
    
    // Check cooldown and max limit
    if (now - lastScanTime >= CARD_COOLDOWN_MS) {
      if (totalCoins < MAX_COINS_PER_TRANSACTION) {
        totalCoins += COINS_PER_SCAN;
        lastScanTime = now;
        transactionStartTime = now; // Reset timeout
        
        Serial.print("► Scan #");
        Serial.print(totalCoins);
        Serial.print(" | Total coins: ");
        Serial.print(totalCoins);
        Serial.print("/");
        Serial.println(MAX_COINS_PER_TRANSACTION);
      } else {
        Serial.println("⚠ Maximum coins reached! Remove card to finalize.");
      }
    }
  }
  
  // Check for transaction timeout
  if (transactionInProgress && (now - transactionStartTime >= TRANSACTION_TIMEOUT_MS)) {
    processTransaction();
  }
  
  delay(50); // Small delay to prevent watchdog issues
}


// ============================================
//          NETWORKING FUNCTIONS
// ============================================

void setupNetworking() {
  Serial.println("\n═══ Network Setup ═══");
  
  // Try Station mode first
  if (connectStationMode()) {
    currentMode = MODE_STATION;
    Serial.println("✓ Operating in STATION MODE (Internet available)");
    
    // Initialize Adafruit IO
    initializeAdafruitIO();
    
  } else {
    // Fallback to AP mode
    currentMode = MODE_AP_ONLY;
    Serial.println("⚠ Station mode failed, switching to AP MODE");
    
    if (setupAccessPoint()) {
      Serial.println("✓ Operating in AP-ONLY MODE (Local network)");
    } else {
      Serial.println("✗ CRITICAL: Both modes failed!");
    }
  }
}

bool connectStationMode() {
  Serial.println("Attempting STATION mode connection...");
  Serial.print("SSID: ");
  Serial.println(STATION_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(STATION_SSID, STATION_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < WIFI_CONNECT_TIMEOUT) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✓ WiFi connected!");
    Serial.print("  IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("  Signal strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    return true;
  } else {
    Serial.println("✗ Station mode connection failed");
    return false;
  }
}

bool setupAccessPoint() {
  Serial.println("Setting up Access Point...");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  
  WiFi.mode(WIFI_AP);
  
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("✗ Failed to create Access Point!");
    return false;
  }
  
  delay(1000);
  
  Serial.println("✓ Access Point created successfully!");
  Serial.print("  AP IP address: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("  AP MAC address: ");
  Serial.println(WiFi.softAPmacAddress());
  Serial.println("\n  Connection Info for Raspberry Pi:");
  Serial.println("  ─────────────────────────────────");
  Serial.println("  SSID: " + String(AP_SSID));
  Serial.println("  Password: " + String(AP_PASSWORD));
  Serial.println("  Gateway: 192.168.4.1");
  Serial.println("  Recommended Pi IP: 192.168.4.4");
  
  return true;
}

void checkAndReconnectWiFi() {
  unsigned long now = millis();
  
  // Only check periodically
  if (now - lastWiFiCheck < WIFI_RETRY_INTERVAL) {
    return;
  }
  lastWiFiCheck = now;
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n⚠ WiFi disconnected! Attempting reconnection...");
    
    if (connectStationMode()) {
      Serial.println("✓ WiFi reconnected successfully");
      // Reinitialize Adafruit if needed
      if (!adafruitConnected) {
        initializeAdafruitIO();
      }
    } else {
      Serial.println("✗ Reconnection failed, switching to AP mode");
      currentMode = MODE_AP_ONLY;
      setupAccessPoint();
    }
  }
}

void initializeAdafruitIO() {
  Serial.println("\n═══ Initializing Adafruit IO ═══");
  
  // Create Adafruit IO object
  io = new AdafruitIO_WiFi(IO_USERNAME, IO_KEY, STATION_SSID, STATION_PASSWORD);
  
  // Connect to Adafruit IO
  io->connect();
  
  int attempts = 0;
  while (io->status() < AIO_CONNECTED && attempts < 10) {
    Serial.print(".");
    delay(500);
    attempts++;
  }
  Serial.println();
  
  if (io->status() >= AIO_CONNECTED) {
    Serial.println("✓ Adafruit IO connected!");
    Serial.println("  Status: " + String(io->statusText()));
    
    // Initialize feed
    coin_feed = io->feed("laundry-machine");
    adafruitConnected = true;
  } else {
    Serial.println("⚠ Adafruit IO connection failed");
    Serial.println("  Continuing without cloud features");
    adafruitConnected = false;
  }
}


// ============================================
//          OTA UPDATE FUNCTIONS
// ============================================

void setupOTA() {
  Serial.println("\n═══ Initializing OTA Updates ═══");
  
  // Set OTA hostname
  ArduinoOTA.setHostname("ESP32-RFID-Laundry");
  
  // Set OTA password for security
  ArduinoOTA.setPassword("laundry_ota_2025");
  
  // OTA callbacks
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║         OTA UPDATE STARTED             ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println("Type: " + type);
    
    // Disable relay during update
    digitalWrite(RELAY_PIN, HIGH);
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\n✓ OTA Update completed successfully!");
    Serial.println("Rebooting...");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static unsigned int lastPercent = 0;
    unsigned int percent = (progress / (total / 100));
    
    if (percent != lastPercent && percent % 10 == 0) {
      Serial.printf("Progress: %u%%\n", percent);
      lastPercent = percent;
    }
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("\n✗ OTA Error[%u]: ", error);
    switch (error) {
      case OTA_AUTH_ERROR:
        Serial.println("Auth Failed");
        break;
      case OTA_BEGIN_ERROR:
        Serial.println("Begin Failed");
        break;
      case OTA_CONNECT_ERROR:
        Serial.println("Connect Failed");
        break;
      case OTA_RECEIVE_ERROR:
        Serial.println("Receive Failed");
        break;
      case OTA_END_ERROR:
        Serial.println("End Failed");
        break;
    }
  });
  
  ArduinoOTA.begin();
  
  Serial.println("✓ OTA ready");
  Serial.println("  Hostname: ESP32-RFID-Laundry");
  Serial.println("  Password: laundry_ota_2025");
  
  // Only show IP in Station mode
  if (currentMode == MODE_STATION) {
    Serial.println("  IP: " + WiFi.localIP().toString());
  }
}


// ============================================
//          TRANSACTION FUNCTIONS
// ============================================

void processTransaction() {
  if (!transactionInProgress || totalCoins == 0) {
    return;
  }
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║      FINALIZING TRANSACTION            ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("Card: " + currentCard);
  Serial.println("Total Coins: " + String(totalCoins));
  Serial.println("Mode: " + getModeString());
  
  // Send to server
  if (sendTransactionToServer(currentCard, totalCoins, "laundry_machine_1")) {
    Serial.println("✓ Transaction successful");
  } else {
    Serial.println("✗ Transaction failed!");
  }
  
  // Reset state
  transactionInProgress = false;
  currentCard = "";
  totalCoins = 0;
  
  Serial.println("════════════════════════════════════════\n");
}

bool sendTransactionToServer(String cardId, int coins, String machine_id) {
  float backoff = BACKOFF_BASE;
  int maxRetries = 3;
  int retryCount = 0;
  
  while (retryCount < maxRetries) {
    HTTPClient http;
    
    // Use appropriate URL based on mode
    String serverURL = getServerURL();
    
    http.begin(serverURL);
    http.setTimeout(5000); // 5 second timeout
    http.addHeader("Content-Type", "application/json");
    
    // Create JSON payload
    StaticJsonDocument<256> doc;
    doc["card_id"] = cardId;
    doc["coins"] = coins;
    doc["machine_id"] = machine_id;
    doc["mode"] = getModeString();
    
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
        
        Serial.println("┌─────────────────────────────────────┐");
        Serial.println("│ Success: " + String(success ? "YES" : "NO"));
        Serial.println("│ User exists: " + String(userExists ? "YES" : "NO"));
        Serial.println("│ Activate machine: " + String(shouldActivate ? "YES" : "NO"));
        Serial.println("│ Message: " + String(message));
        
        if (responseDoc.containsKey("balance")) {
          int balance = responseDoc["balance"];
          Serial.println("│ Remaining balance: " + String(balance));
        }
        if (responseDoc.containsKey("coins_used")) {
          int coinsUsed = responseDoc["coins_used"];
          Serial.println("│ Coins used: " + String(coinsUsed));
        }
        Serial.println("└─────────────────────────────────────┘");
        
        if (success && shouldActivate && userExists) {
          Serial.println("🔌 Activating laundry machine...");
          relayPulsesRemaining = coins;
          Serial.print("⚡ Will pulse relay ");
          Serial.print(coins);
          Serial.println(" times");
          activateRelay();
          
          // Send to Adafruit IO only in Station mode
          if (currentMode == MODE_STATION && adafruitConnected) {
            sendToAdafruit(cardId, coins);
          }
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
      
      // Exponential backoff
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

bool scanCardDisplay(String cardId) {
  HTTPClient http;
  
  String serverURL = getServerURL();
  http.begin(serverURL);
  http.setTimeout(3000);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<64> doc;
  doc["card_id"] = cardId;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  int httpResponseCode = http.POST(jsonString);
  http.end();
  
  return (httpResponseCode > 0);
}

void sendToAdafruit(String cardId, int coins) {
  if (!adafruitConnected || coin_feed == nullptr) {
    Serial.println("⚠ Adafruit IO not connected, skipping");
    return;
  }
  
  Serial.println("→ Sending to Adafruit IO...");
  
  // Send coins value to feed
  coin_feed->save(coins);
  
  Serial.println("✓ Data sent to Adafruit IO");
  Serial.println("  Feed: laundry-machine");
  Serial.println("  Value: " + String(coins));
}


// ============================================
//          RFID & RELAY FUNCTIONS
// ============================================

String readCardOnce() {
  if (!rfid.PICC_IsNewCardPresent()) {
    return "";
  }
  
  if (!rfid.PICC_ReadCardSerial()) {
    return "";
  }
  
  String cardId = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) {
      cardId += "0";
    }
    cardId += String(rfid.uid.uidByte[i], HEX);
  }
  cardId.toUpperCase();
  
  // Halt PICC and stop encryption
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  
  return cardId;
}

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

void updateRelay() {
  unsigned long now = millis();
  
  if (relayActive) {
    // Check if pulse duration is complete
    if (now - relayStartTime >= RELAY_PULSE_MS) {
      digitalWrite(RELAY_PIN, HIGH);  // Relay OFF
      relayActive = false;
      relayPulsesRemaining--;
      
      Serial.print("⚡ Relay pulse completed. Remaining: ");
      Serial.println(relayPulsesRemaining);
      
      // Start pause timer if more pulses needed
      if (relayPulsesRemaining > 0) {
        relayPauseTime = now;
      }
    }
  } else if (relayPulsesRemaining > 0) {
    // Check if pause duration is complete
    if (now - relayPauseTime >= RELAY_PAUSE_MS) {
      activateRelay();  // Start next pulse
    }
  }
}


// ============================================
//          UTILITY FUNCTIONS
// ============================================

String getServerURL() {
  switch (currentMode) {
    case MODE_STATION:
      return STATION_SERVER_URL;
    case MODE_AP_ONLY:
      return AP_SERVER_URL;
    default:
      return AP_SERVER_URL;
  }
}

String getModeString() {
  switch (currentMode) {
    case MODE_STATION:
      return "STATION (Internet)";
    case MODE_AP_ONLY:
      return "AP-ONLY (Local)";
    default:
      return "UNKNOWN";
  }
}
