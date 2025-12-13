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
const char* SCAN_CARD_URL = "http://10.241.108.134:5000/scan_card"; // Endpoint 1: Simple scan, display data (GET/POST depending on server)

// RFID reader pins
#define RST_PIN 22
#define SS_PIN 5
#define SCK_PIN 18
#define MOSI_PIN 23
#define MISO_PIN 19

// Relay pin
#define RELAY_PIN 4

// Timing constants
const unsigned long CARD_COOLDOWN_MS = 3000;        // Min time card must be removed before counting as a new "coin"
const unsigned long RAPID_SCAN_WINDOW_MS = 5000;    // Total time window for multi-coin counting
const unsigned long RELAY_PULSE_MS = 150;           // Relay pulse duration
const float BACKOFF_BASE = 1.5;
const unsigned long BACKOFF_MAX_MS = 10000;

// Coin settings
const int BASE_COINS = 1;          // Base coins per scan
const int RAPID_MULTIPLIER = 1;    // Extra coins per rapid scan

// Global objects
MFRC522 rfid(SS_PIN, RST_PIN);

// State variables
String lastCard = "";
unsigned long lastCardTime = 0; // Time of last successful SPEND
unsigned long lastCardDetectedTime = 0; // Time card was last seen (for cooldown)
int rapidScanCount = 0;
bool relayActive = false;
unsigned long relayStartTime = 0;

// Function declarations
bool connectWiFi(int timeout = 20);
String readCardOnce();
void activateRelay();
void updateRelay();

// New function to handle the SPEND logic (Endpoint 2)
bool sendCardToServer(String cardId, int coins, String machine_id);
// New function to handle the SCAN/Display logic (Endpoint 1)
bool scanCardTagOnly(String cardId); 


void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=== ESP32 RFID Multi-Endpoint System ===");
  
  // Initialize relay
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);  // Relay OFF (active-low)
  
  // Initialize SPI and RFID reader
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  rfid.PCD_Init();
  
  // Connect to WiFi
  connectWiFi();
  
  Serial.println("\nSetup complete. Ready to scan cards.");
  Serial.println("SCAN endpoint: " + String(SCAN_CARD_URL));
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
  
  if (card.length() > 0) {
    
    lastCardDetectedTime = now; // Update card presence time
    Serial.println("Card Detected: " + card);
    
    // --- 1. ALWAYS send to the SCAN endpoint for immediate server feedback ---
    scanCardTagOnly(card); 
    
    // --- 2. Check logic for triggering the SPEND transaction ---
    
    // Check if we are outside the cooldown (ready for a new transaction)
    bool cooldownPassed = (card != lastCard) || (now - lastCardTime) >= CARD_COOLDOWN_MS;
    
    if (cooldownPassed) {
        
        // Check for rapid consecutive scans within the window
        if (card == lastCard && (now - lastCardTime) < RAPID_SCAN_WINDOW_MS) {
          rapidScanCount++;
          Serial.print("[RAPID] Consecutive scan #");
          Serial.println(rapidScanCount);
        } else {
          rapidScanCount = BASE_COINS; // Reset to 1 coin for new transaction
        }
      
        // Calculate coins to deduct
        int coinsToDeduct = BASE_COINS + ((rapidScanCount - 1) * RAPID_MULTIPLIER);
        
        Serial.println("\n══════════════════════════════════");
        Serial.println("Triggering SPEND API...");
        Serial.print("Coins to request: ");
        Serial.println(coinsToDeduct);
        
        // Send data to the SPEND endpoint
        if (sendCardToServer(card, coinsToDeduct, "laundry_machine_1")) {
          Serial.println("✓ SENDCARD TO SERVER SPEND successful and machine activated.");
          lastCard = card;
          lastCardTime = now; // Record time of successful SPEND
        } else {
          Serial.println("✗ SENDCARD TO SERVER  SPEND failed. Resetting count.");
          rapidScanCount = 0; // Reset on failure
        }
        
        Serial.println("══════════════════════════════════\n");
    } else {
      Serial.println("Card held - waiting for cooldown before new SPEND transaction.");
    }
  }
  
  delay(100);  // 100ms delay between scans
}

// =========================================================================
// NEW FUNCTION: Send simple card data (Endpoint 1: /scan_card)
// This is typically a fast call to update a display or log the card presence.
// =========================================================================
bool scanCardTagOnly(String cardId) {
  HTTPClient http;
  http.begin(SCAN_CARD_URL);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<128> doc;
  doc["card_id"] = cardId;
  // doc["status"] = "present";
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.println("[SCAN] Sending: " + jsonString);
  
  int httpResponseCode = http.POST(jsonString); // POST is usually safer than GET
  
  if (httpResponseCode > 0) {
    Serial.print("[SCAN] Server status: ");
    Serial.println(httpResponseCode);
    // Optionally print response or check JSON if server returns user data
  } else {
    Serial.print("[SCAN] Error: ");
    Serial.println(http.errorToString(httpResponseCode));
  }
  http.end();
  return (httpResponseCode > 0);
}



bool connectWiFi(int timeout) {
  // Check if already connected
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

bool sendCardToServer(String cardId, int coins, String machine_id) {
  float backoff = BACKOFF_BASE;
  int maxRetries = 3;
  int retryCount = 0;
  
  while (retryCount < maxRetries) {
    HTTPClient http;
    
    http.begin(SCAN_CARD_URL);
    http.addHeader("Content-Type", "application/json");
    
    // Create JSON payload with coins
    StaticJsonDocument<256> doc;
    doc["card_id"] = cardId;
    doc["coins"] = coins;
    doc["machine_id"] = machine_id;
    doc["rapid_scan_count"] = rapidScanCount;
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    Serial.println("Sending to server: " + jsonString);
    
    int httpResponseCode = http.POST(jsonString);
    
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      Serial.print("Server response: ");
      Serial.println(response);
      
      // Parse JSON response
      StaticJsonDocument<512> responseDoc;
      DeserializationError error = deserializeJson(responseDoc, response);
      
      if (!error) {
        bool success = responseDoc["success"] | false;
        bool userExists = responseDoc["user_exists"] | false;
        bool shouldActivate = responseDoc["activate_machine"] | false;
        const char* message = responseDoc["message"] | "No message";
        
        Serial.print("Success: ");
        Serial.println(success ? "YES" : "NO");
        Serial.print("User exists: ");
        Serial.println(userExists ? "YES" : "NO");
        Serial.print("Activate machine: ");
        Serial.println(shouldActivate ? "YES" : "NO");
        Serial.print("Message: ");
        Serial.println(message);
        
        if (success && shouldActivate && userExists) {
          activateRelay();
          if (responseDoc.containsKey("balance")) {
            int balance = responseDoc["balance"];
            Serial.print("Balance: ");
            Serial.println(balance);
          }
          if (responseDoc.containsKey("coins_used")) {
            int coinsUsed = responseDoc["coins_used"];
            Serial.print("Coins used: ");
            Serial.println(coinsUsed);
          }
          
          // Activate relay if authorized
          // if (shouldActivate) {
          //   Serial.println("Activating laundry machine...");
          //   activateRelay();
          // }
        }
      } else {
        Serial.print("JSON parse error: ");
        Serial.println(error.c_str());
      }
      
      http.end();
      return true;
    } else {
      Serial.print("HTTP Error code: ");
      Serial.println(httpResponseCode);
      Serial.println("Error: " + http.errorToString(httpResponseCode));
      http.end();
      
      // Exponential backoff with cap
      unsigned long sleepTime = min((unsigned long)(backoff * 1000), BACKOFF_MAX_MS);
      Serial.print("Retrying in ");
      Serial.print(sleepTime / 1000);
      Serial.println(" seconds...");
      
      delay(sleepTime);
      backoff = min(backoff * 2.0f, (float)BACKOFF_MAX_MS / 1000);
      retryCount++;
    }
  }
  
  Serial.println("Max retries reached, giving up");
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
    Serial.println("Relay already active, skipping");
    return;
  }
  
  Serial.println("=== ACTIVATING RELAY ===");
  digitalWrite(RELAY_PIN, LOW);   // Relay ON (active-low)
  relayActive = true;
  relayStartTime = millis();
  
  Serial.print("Relay ON for ");
  Serial.print(RELAY_PULSE_MS);
  Serial.println(" ms");
  Serial.println("Laundry machine starting...");
}

void updateRelay() {
  if (relayActive) {
    unsigned long now = millis();
    if (now - relayStartTime >= RELAY_PULSE_MS) {
      digitalWrite(RELAY_PIN, HIGH);  // Relay OFF
      relayActive = false;
      Serial.println("Relay pulse completed");
    }
  }
}
