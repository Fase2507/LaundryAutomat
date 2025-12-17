#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>

// WiFi credentials
const char* SSID = "fase";
const char* PASSWORD = "12345678";

// Server configuration
const char* HOST_IP = "10.241.108.134:5000";
const char* SCAN_CARD_URL = "http://10.241.108.134:5000/scan_card";

// RFID reader pins
#define RST_PIN 22
#define SS_PIN 5
#define SCK_PIN 18
#define MOSI_PIN 23
#define MISO_PIN 19

// Relay pin
#define RELAY_PIN 4

// Timing constants
const unsigned long CARD_COOLDOWN_MS = 1000;         // Time between individual scans (1 second)
const unsigned long TRANSACTION_TIMEOUT_MS = 3000;   // Wait 3 seconds after last scan to finalize transaction
const unsigned long RELAY_PULSE_MS = 150;            // Relay pulse duration
const float BACKOFF_BASE = 1.5;
const unsigned long BACKOFF_MAX_MS = 10000;

// Coin settings
const int COINS_PER_SCAN = 1;  // 1 coin per scan

// Global objects
MFRC522 rfid(SS_PIN, RST_PIN);

// State variables
String currentCard = "";
int totalCoins = 0;
unsigned long lastScanTime = 0;
unsigned long transactionStartTime = 0;
bool transactionInProgress = false;
bool relayActive = false;
unsigned long relayStartTime = 0;
int relayPulsesRemaining = 0;
unsigned long relayPauseTime = 0;
const unsigned long RELAY_PAUSE_MS = 500; // Pause between relay pulses

// Function declarations
bool connectWiFi(int timeout = 20);
String readCardOnce();
void activateRelay();
void updateRelay();
bool sendTransactionToServer(String cardId, int coins, String machine_id);
bool scanCardDisplay(String cardId);
void processTransaction();

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=== ESP32 RFID Rapid Scan Fixed ===");
  
  // Initialize relay
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);  // Relay OFF (active-low)
  
  // Initialize SPI and RFID reader
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  rfid.PCD_Init();
  
  // Connect to WiFi
  connectWiFi();
  
  Serial.println("\nSetup complete. Ready to scan cards.");
  Serial.println("Scan the same card multiple times within 3 seconds for multiple coins.");
  Serial.println("====================================\n");
}

void loop() {
  unsigned long now = millis();
  
  updateRelay(); // Always check/turn off relay
  
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, reconnecting...");
    connectWiFi(10);
    delay(2000);
    return;
  }
  
  String card = readCardOnce();
  
  // Check if we have a card
  if (card.length() > 0) {
    
    // Send to display endpoint immediately (non-blocking)
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
      Serial.println("\n╔════════════════════════════════╗");
      Serial.println("  NEW TRANSACTION STARTED");
      Serial.print("  Card: ");
      Serial.println(card);
      Serial.println("╚════════════════════════════════╝");
    }
    
    // Check cooldown between scans (prevent double-reading same scan)
    if (now - lastScanTime >= CARD_COOLDOWN_MS) {
      totalCoins += COINS_PER_SCAN;
      lastScanTime = now;
      transactionStartTime = now; // Reset timeout
      
      Serial.print("► Scan #");
      Serial.print(totalCoins);
      Serial.print(" detected | Total coins: ");
      Serial.println(totalCoins);
    }
  }
  
  // Check if transaction timeout reached (no card for TRANSACTION_TIMEOUT_MS)
  if (transactionInProgress && (now - transactionStartTime >= TRANSACTION_TIMEOUT_MS)) {
    processTransaction();
  }
  
  delay(100);  // 100ms delay between scans
}

void processTransaction() {
  if (!transactionInProgress || totalCoins == 0) {
    return;
  }
  
  Serial.println("\n══════════════════════════════════");
  Serial.println("  FINALIZING TRANSACTION");
  Serial.print("  Card: ");
  Serial.println(currentCard);
  Serial.print("  Total Coins: ");
  Serial.println(totalCoins);
  Serial.println("══════════════════════════════════");
  
  // Send single transaction to server
  if (sendTransactionToServer(currentCard, totalCoins, "laundry_machine_1")) {
    Serial.println("✓ Transaction successful!");
  } else {
    Serial.println("✗ Transaction failed!");
  }
  
  // Reset transaction state
  transactionInProgress = false;
  currentCard = "";
  totalCoins = 0;
  
  Serial.println("══════════════════════════════════\n");
}

bool scanCardDisplay(String cardId) {
  HTTPClient http;
  http.begin(SCAN_CARD_URL);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<128> doc;
  doc["card_id"] = cardId;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  int httpResponseCode = http.POST(jsonString);
  http.end();
  
  return (httpResponseCode > 0);
}

bool connectWiFi(int timeout) {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Already connected to WiFi");
    return true;
  }
  
  Serial.print("Connecting to WiFi: ");
  Serial.println(SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < timeout) {
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

bool sendTransactionToServer(String cardId, int coins, String machine_id) {
  float backoff = BACKOFF_BASE;
  int maxRetries = 3;
  int retryCount = 0;
  
  while (retryCount < maxRetries) {
    HTTPClient http;
    
    http.begin(SCAN_CARD_URL);
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

String readCardOnce() {
  // Look for new cards
  if (!rfid.PICC_IsNewCardPresent()) {
    return "";
  }
  
  // Select one of the cards
  if (!rfid.PICC_ReadCardSerial()) {
    return "";
  }
  
  // Read UID
  String cardId = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) {
      cardId += "0";
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
  
  // Handle active relay pulse
  if (relayActive) {
    if (now - relayStartTime >= RELAY_PULSE_MS) {
      digitalWrite(RELAY_PIN, HIGH);  // Relay OFF
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
  // Handle pause between pulses
  else if (relayPulsesRemaining > 0) {
    if (now - relayPauseTime >= RELAY_PAUSE_MS) {
      // Start next pulse
      activateRelay();
    }
  }
}