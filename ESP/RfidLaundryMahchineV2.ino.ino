#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>

// Access Point configuration
const char* AP_SSID = "ESP32-RFID-Controller";
const char* AP_PASSWORD = "laundry123";  // Minimum 8 characters

// Server configuration - Now the Pi will connect to ESP32's network
// Use the ESP32's default gateway IP as server address
const char* SERVER_URL = "http://192.168.4.4:5000/scan_card";  // Pi should have this static IP

// RFID reader pins
#define RST_PIN 22
#define SS_PIN 5
#define SCK_PIN 18
#define MOSI_PIN 23
#define MISO_PIN 19

// Timing constants
const unsigned long CARD_COOLDOWN_MS = 5000;  // 5 seconds
const float BACKOFF_BASE = 1.5;
const unsigned long BACKOFF_MAX_MS = 30000;   // 30 seconds

// Global objects
MFRC522 rfid(SS_PIN, RST_PIN);

// State variables
String lastCard = "";
unsigned long lastCardTime = 0;

// Function declarations
bool setupAccessPoint();
bool sendCardToServer(String cardId);
String readCardOnce();

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=== ESP32 RFID Reader Starting ===");
  Serial.println("Mode: Access Point");
  
  // Initialize SPI with custom pins
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  
  // Initialize RFID reader
  rfid.PCD_Init();
  Serial.println("MFRC522 initialized");
  
  // Show RFID reader details
  rfid.PCD_DumpVersionToSerial();
  
  // Set up Access Point
  if (!setupAccessPoint()) {
    Serial.println("Failed to setup Access Point!");
    while(1) { delay(1000); }  // Halt if AP fails
  }
  
  Serial.println("Setup complete. Ready to scan cards.");
  Serial.println("Clients can connect to: " + String(AP_SSID));
  Serial.println("Server URL: " + String(SERVER_URL));
}

void loop() {
  // In AP mode, we don't need to check WiFi connection
  // The ESP32 maintains the AP regardless of connected clients
  
  // Try to read a card
  String card = readCardOnce();
  unsigned long now = millis();
  
  // Check if we have a new card or cooldown period has passed
  if (card.length() > 0 && 
      (card != lastCard || (now - lastCardTime) >= CARD_COOLDOWN_MS)) {
    
    Serial.println("Card detected: " + card);
    
    // Send card to server
    if (sendCardToServer(card)) {
      Serial.println("Card sent successfully");
      lastCard = card;
      lastCardTime = now;
    } else {
      Serial.println("Failed to send card to server");
    }
  }
  
  delay(500);  // 500ms delay between scans
}

bool setupAccessPoint() {
  Serial.print("Setting up Access Point: ");
  Serial.println(AP_SSID);
  
  // Configure as Access Point
  WiFi.mode(WIFI_AP);
  
  // Set up the Access Point
  // IP: 192.168.4.1, Gateway: 192.168.4.1, Subnet: 255.255.255.0
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

bool sendCardToServer(String cardId) {
  float backoff = BACKOFF_BASE;
  int maxRetries = 5;
  int retryCount = 0;
  
  while (retryCount < maxRetries) {
    HTTPClient http;
    
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");
    
    // Create JSON payload
    StaticJsonDocument<200> doc;
    doc["card_id"] = cardId;
    
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
        bool userExists = responseDoc["user_exists"] | false;
        const char* message = responseDoc["message"] | "No message";
        
        Serial.print("User exists: ");
        Serial.println(userExists ? "YES" : "NO");
        Serial.print("Message: ");
        Serial.println(message);
        
        if (userExists) {
          int balance = responseDoc["balance"] | 0;
          Serial.print("Balance: ");
          Serial.println(balance);
        }
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