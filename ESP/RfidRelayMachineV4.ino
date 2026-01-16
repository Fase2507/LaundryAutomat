#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>
#include "AdafruitIO_WiFi.h"
#include "secret.h"
// Add this line to fix the OTA update error
#include <Update.h>
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

WiFiMode currentMode = MODE_STATION;

// ============== STATION MODE CONFIG (DUAL SSID) ==============
// Primary WiFi (Try first - Your phone hotspot)
const char* STATION_SSID_PRIMARY = "CNO";
const char* STATION_PASSWORD_PRIMARY = "qazwsx12";
const char* STATION_SERVER_URL_PRIMARY = "http://10.114.xxx.xxx:5000/scan_card";

// Secondary WiFi (Fallback - Dormitory WiFi)
const char* STATION_SSID_SECONDARY = "YURDU";
const char* STATION_PASSWORD_SECONDARY = "!";
const char* STATION_SERVER_URL_SECONDARY = "http://172.xxx.xxx.xxx:5000/scan_card";
// Active configuration (set during connection)
String activeSSID = "";
String activeServerURL = "";

// ============== ACCESS POINT CONFIG ==============
const char* AP_SSID = "ESP32-RFID-Controller";
const char* AP_PASSWORD = "laundry123";
const char* AP_SERVER_URL = "http://192.168.4.4:5000/scan_card";

// ============== GITHUB CONFIGURATIONS ==============
const char* github_owner = "Fase2507";
const char* github_repo = "ESP32-projects-OTA-for-firmwares";
const char* firmware_asset_name = "LaundryMachineAdaFruit.ino.bin";
const char* currentFirmwareVersion = "1.0.3";

// ============== SYSTEM CONSTANTS ==============
const int MAX_COINS_PER_TRANSACTION = 10;
const int COINS_PER_SCAN = 1;
const unsigned long CARD_COOLDOWN_MS = 1000;
const unsigned long TRANSACTION_TIMEOUT_MS = 4000;
const unsigned long RELAY_PULSE_MS = 150;
const unsigned long RELAY_PAUSE_MS = 500;
const float BACKOFF_BASE = 1.5;
const unsigned long BACKOFF_MAX_MS = 10000;
const unsigned long WIFI_RETRY_INTERVAL = 30000;
const int WIFI_CONNECT_TIMEOUT = 20;
const unsigned long FIRMWARE_CHECK_INTERVAL = 3600000; // Check every hour

// ============== GLOBAL OBJECTS ==============
MFRC522 rfid(SDA_PIN, RST_PIN);
AdafruitIO_WiFi *io = nullptr;
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
unsigned long lastFirmwareCheck = 0;

// ============== FUNCTION DECLARATIONS ==============
void setupNetworking();
bool connectStationMode(const char* ssid, const char* password, const char* serverUrl);
bool setupAccessPoint();
void checkAndReconnectWiFi();
void initializeAdafruitIO();
void checkFirmwareForUpdate();
void downloadAndApplyFirmware(String url, int expectedSize);

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
  Serial.println("║  Dual WiFi + GitHub OTA Support        ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  Serial.println("ESP32 MAC Address: " + WiFi.macAddress());
  Serial.println("Current Firmware Version: " + String(currentFirmwareVersion));
  
  // Configure relay pin
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Relay OFF (active-low)
  
  // Initialize SPI and RFID
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SDA_PIN);
  rfid.PCD_Init();
  
  Serial.println("\n✓ RFID Reader initialized");
  rfid.PCD_DumpVersionToSerial();
  
  // Setup networking (tries Primary, then Secondary, then AP)
  setupNetworking();
  
  // Check for firmware updates (only if connected to internet)
  if (currentMode == MODE_STATION && WiFi.status() == WL_CONNECTED) {
    checkFirmwareForUpdate();
  }
  
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
  
  // Periodic firmware update check (only in Station mode with internet)
  if (currentMode == MODE_STATION && 
      WiFi.status() == WL_CONNECTED && 
      (now - lastFirmwareCheck >= FIRMWARE_CHECK_INTERVAL)) {
    Serial.println("\n⏰ Periodic firmware check...");
    checkFirmwareForUpdate();
    lastFirmwareCheck = now;
  }
  
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
  
  delay(50);
}

// ============================================
//          NETWORKING FUNCTIONS
// ============================================
void setupNetworking() {
  Serial.println("\n═══ Network Setup (Dual SSID Mode) ═══");
  
  // Try Primary WiFi first (TECNO - Phone Hotspot)
  if (connectStationMode(STATION_SSID_PRIMARY, STATION_PASSWORD_PRIMARY, STATION_SERVER_URL_PRIMARY)) {
    currentMode = MODE_STATION;
    Serial.println("✓ Connected to PRIMARY WiFi: " + String(STATION_SSID_PRIMARY));
    activeSSID = STATION_SSID_PRIMARY;
    activeServerURL = STATION_SERVER_URL_PRIMARY;
    initializeAdafruitIO();
    return;
  }
  
  Serial.println("⚠ Primary WiFi failed, trying secondary...");
  
  // Try Secondary WiFi (DUZCEYURDU - Dormitory)
  if (connectStationMode(STATION_SSID_SECONDARY, STATION_PASSWORD_SECONDARY, STATION_SERVER_URL_SECONDARY)) {
    currentMode = MODE_STATION;
    Serial.println("✓ Connected to SECONDARY WiFi: " + String(STATION_SSID_SECONDARY));
    activeSSID = STATION_SSID_SECONDARY;
    activeServerURL = STATION_SERVER_URL_SECONDARY;
    initializeAdafruitIO();
    return;
  }
  
  Serial.println("⚠ Both Station modes failed, switching to AP MODE");
  
  // Fallback to AP mode
  currentMode = MODE_AP_ONLY;
  if (setupAccessPoint()) {
    Serial.println("✓ Operating in AP-ONLY MODE (Local network)");
    activeSSID = AP_SSID;
    activeServerURL = AP_SERVER_URL;
  } else {
    Serial.println("✗ CRITICAL: All connection modes failed!");
  }
}

bool connectStationMode(const char* ssid, const char* password, const char* serverUrl) {
  Serial.println("Attempting connection to: " + String(ssid));
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < WIFI_CONNECT_TIMEOUT) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✓ WiFi connected!");
    Serial.println("  SSID: " + String(ssid));
    Serial.println("  IP address: " + WiFi.localIP().toString());
    Serial.println("  Signal strength: " + String(WiFi.RSSI()) + " dBm");
    Serial.println("  Server URL: " + String(serverUrl));
    return true;
  } else {
    Serial.println("✗ Connection to " + String(ssid) + " failed");
    WiFi.disconnect(true);
    delay(1000);
    return false;
  }
}

bool setupAccessPoint() {
  Serial.println("Setting up Access Point...");
  Serial.println("SSID: " + String(AP_SSID));
  
  WiFi.mode(WIFI_AP);
  
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("✗ Failed to create Access Point!");
    return false;
  }
  
  delay(1000);
  
  Serial.println("✓ Access Point created successfully!");
  Serial.println("  AP IP address: " + WiFi.softAPIP().toString());
  Serial.println("  AP MAC address: " + WiFi.softAPmacAddress());
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
  
  if (now - lastWiFiCheck < WIFI_RETRY_INTERVAL) {
    return;
  }
  lastWiFiCheck = now;
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n⚠ WiFi disconnected! Attempting reconnection...");
    
    // Try primary first
    if (connectStationMode(STATION_SSID_PRIMARY, STATION_PASSWORD_PRIMARY, STATION_SERVER_URL_PRIMARY)) {
      Serial.println("✓ Reconnected to PRIMARY WiFi");
      activeSSID = STATION_SSID_PRIMARY;
      activeServerURL = STATION_SERVER_URL_PRIMARY;
      currentMode = MODE_STATION;
      if (!adafruitConnected) initializeAdafruitIO();
      return;
    }
    
    // Try secondary
    if (connectStationMode(STATION_SSID_SECONDARY, STATION_PASSWORD_SECONDARY, STATION_SERVER_URL_SECONDARY)) {
      Serial.println("✓ Reconnected to SECONDARY WiFi");
      activeSSID = STATION_SSID_SECONDARY;
      activeServerURL = STATION_SERVER_URL_SECONDARY;
      currentMode = MODE_STATION;
      if (!adafruitConnected) initializeAdafruitIO();
      return;
    }
    
    // Both failed, switch to AP
    Serial.println("✗ Reconnection failed, switching to AP mode");
    currentMode = MODE_AP_ONLY;
    activeSSID = AP_SSID;
    activeServerURL = AP_SERVER_URL;
    setupAccessPoint();
  }
}

void initializeAdafruitIO() {
  Serial.println("\n═══ Initializing Adafruit IO ═══");
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠ Not connected to WiFi, skipping Adafruit IO");
    adafruitConnected = false;
    return;
  }
  
  const char* currentSSID = activeSSID.c_str();
  const char* currentPassword = "";
  
  if (activeSSID == STATION_SSID_PRIMARY) {
    currentPassword = STATION_PASSWORD_PRIMARY;
  } else if (activeSSID == STATION_SSID_SECONDARY) {
    currentPassword = STATION_PASSWORD_SECONDARY;
  } else {
    Serial.println("⚠ Unknown SSID, skipping Adafruit IO");
    adafruitConnected = false;
    return;
  }
  
  io = new AdafruitIO_WiFi(IO_USERNAME, IO_KEY, currentSSID, currentPassword);
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
    coin_feed = io->feed("laundry-machine");
    adafruitConnected = true;
  } else {
    Serial.println("⚠ Adafruit IO connection failed (likely captive portal)");
    Serial.println("  Continuing without cloud features");
    adafruitConnected = false;
  }
}

// ============================================
//      GITHUB FIRMWARE UPDATE FUNCTIONS
// ============================================
void checkFirmwareForUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠ WiFi not connected. Skipping firmware update check.");
    return;
  }

  String apiUrl = "https://api.github.com/repos/" + String(github_owner) + "/" + 
                  String(github_repo) + "/releases/latest";

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║    CHECKING FOR FIRMWARE UPDATE        ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("Current version: " + String(currentFirmwareVersion));
  Serial.println("Repository: " + String(github_owner) + "/" + String(github_repo));
  Serial.println("Fetching: " + apiUrl);

  WiFiClientSecure client;
  client.setInsecure();
  
  HTTPClient http;
  http.begin(client, apiUrl);
  http.setTimeout(15000);
  http.addHeader("Accept", "application/vnd.github.v3+json");
  http.addHeader("User-Agent", "ESP32-OTA-Client");
  
  if (strlen(git_tkn) > 0) {
    http.addHeader("Authorization", "token " + String(git_tkn));
    Serial.println("✓ Using GitHub token for authentication");
  } else {
    Serial.println("⚠ No GitHub token - using public API (rate limited)");
  }

  Serial.println("→ Sending API request...");
  int httpCode = http.GET();
  Serial.printf("← Received HTTP code: %d\n", httpCode);

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("✗ API request failed. HTTP code: %d\n", httpCode);
    
    switch(httpCode) {
      case -1:
        Serial.println("  Error: Connection failed (SSL/TLS or network issue)");
        break;
      case 401:
        Serial.println("  Error: Unauthorized - Check GitHub token");
        break;
      case 403:
        Serial.println("  Error: Forbidden - Token may lack permissions");
        break;
      case 404:
        Serial.println("  Error: Repository or release not found");
        break;
      default:
        if (httpCode > 0) {
          Serial.println("  Response: " + http.getString());
        }
    }
    http.end();
    return;
  }
  
  Serial.printf("✓ API request successful (HTTP %d)\n", httpCode);

  StaticJsonDocument<200> filter;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["id"] = true;
  filter["assets"][0]["size"] = true;

  DynamicJsonDocument doc(4096);
  
  Serial.println("→ Parsing JSON response...");
  DeserializationError error = deserializeJson(doc, http.getStream(), 
                                               DeserializationOption::Filter(filter));

  if (error) {
    Serial.print("✗ JSON parsing failed: ");
    Serial.println(error.c_str());
    http.end();
    return;
  }

  String latestVersion = doc["tag_name"].as<String>();
  if (latestVersion.isEmpty() || latestVersion == "null") {
    Serial.println("✗ Could not find 'tag_name' in response");
    http.end();
    return;
  }
  
  Serial.println("Latest version: " + latestVersion);

  if (latestVersion == currentFirmwareVersion) {
    Serial.println("✓ Firmware is up to date!");
    Serial.println("════════════════════════════════════════\n");
    http.end();
    return;
  }

  Serial.println(">>> NEW FIRMWARE AVAILABLE <<<");
  Serial.println("Searching for asset: " + String(firmware_asset_name));
  
  String firmwareUrl = "";
  int assetSize = 0;
  
  JsonArray assets = doc["assets"].as<JsonArray>();
  if (assets.isNull() || assets.size() == 0) {
    Serial.println("✗ No assets found in release");
    http.end();
    return;
  }

  for (JsonObject asset : assets) {
    String assetName = asset["name"].as<String>();
    Serial.println("  Found asset: " + assetName);

    if (assetName == String(firmware_asset_name)) {
      String assetId = asset["id"].as<String>();
      assetSize = asset["size"] | 0;
      
      firmwareUrl = "https://api.github.com/repos/" + String(github_owner) + "/" + 
                    String(github_repo) + "/releases/assets/" + assetId;
      
      Serial.println("✓ Matching asset found!");
      Serial.println("  Asset ID: " + assetId);
      Serial.println("  Size: " + String(assetSize) + " bytes");
      break;
    }
  }
  
  http.end();

  if (firmwareUrl.isEmpty()) {
    Serial.println("✗ Firmware asset not found: " + String(firmware_asset_name));
    Serial.println("════════════════════════════════════════\n");
    return;
  }
  
  Serial.println("════════════════════════════════════════");
  downloadAndApplyFirmware(firmwareUrl, assetSize);
}

void downloadAndApplyFirmware(String url, int expectedSize) {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║     DOWNLOADING FIRMWARE UPDATE        ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("URL: " + url);
  if (expectedSize > 0) {
    Serial.println("Expected size: " + String(expectedSize) + " bytes");
  }

  WiFiClientSecure client;
  client.setInsecure();
  
  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(30000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("ESP32-OTA-Client");
  http.addHeader("Accept", "application/octet-stream");
  
  if (strlen(git_tkn) > 0) {
    http.addHeader("Authorization", "token " + String(git_tkn));
  }

  Serial.println("→ Requesting firmware file...");
  int httpCode = http.GET();
  
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("✗ Download request failed. HTTP code: %d\n", httpCode);
    if (httpCode > 0) {
      Serial.println("Response: " + http.getString());
    }
    http.end();
    return;
  }

  int contentLength = http.getSize();
  Serial.printf("✓ Download started. Size: %d bytes\n", contentLength);

  if (contentLength <= 0) {
    Serial.println("✗ Invalid content length");
    http.end();
    return;
  }

  if (contentLength > 1310720) {
    Serial.println("✗ Firmware too large for ESP32 flash");
    http.end();
    return;
  }

  digitalWrite(RELAY_PIN, HIGH);
  relayActive = false;
  relayPulsesRemaining = 0;
  Serial.println("⚠ Relay disabled for update safety");

  Serial.println("→ Initializing OTA update...");
  if (!Update.begin(contentLength)) {
    Serial.printf("✗ Update.begin() failed: %s\n", Update.errorString());
    http.end();
    return;
  }

  Serial.println("✓ OTA initialized. Writing firmware...");
  Serial.println("┌────────────────────────────────────────┐");
  
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buff[512];
  size_t totalWritten = 0;
  int lastProgress = -1;
  unsigned long startTime = millis();

  while (totalWritten < contentLength) {
    if (millis() - startTime > 120000) {
      Serial.println("\n✗ Download timeout!");
      Update.abort();
      http.end();
      return;
    }

    int available = stream->available();
    if (available > 0) {
      int readLen = stream->readBytes(buff, min((size_t)available, sizeof(buff)));
      
      if (readLen <= 0) {
        Serial.println("\n✗ Error reading from stream");
        Update.abort();
        http.end();
        return;
      }

      size_t written = Update.write(buff, readLen);
      if (written != readLen) {
        Serial.printf("\n✗ Write failed: %s\n", Update.errorString());
        Update.abort();
        http.end();
        return;
      }

      totalWritten += written;

      int progress = (totalWritten * 100) / contentLength;
      if (progress != lastProgress && (progress % 5 == 0 || progress == 100)) {
        Serial.printf("│ Progress: %3d%% [%d/%d bytes]", 
                      progress, totalWritten, contentLength);
        
        unsigned long elapsed = millis() - startTime;
        if (elapsed > 0) {
          float speed = (totalWritten / 1024.0) / (elapsed / 1000.0);
          Serial.printf(" %.1f KB/s", speed);
        }
        Serial.println();
        
        lastProgress = progress;
      }
    } else {
      delay(1);
    }
    
    yield();
  }

  Serial.println("└────────────────────────────────────────┘");

  if (totalWritten != contentLength) {
    Serial.printf("✗ Write incomplete! Wrote %d of %d bytes\n", 
                  totalWritten, contentLength);
    Update.abort();
    http.end();
    return;
  }

  Serial.println("→ Finalizing update...");
  if (!Update.end(true)) {
    Serial.printf("✗ Update.end() failed: %s\n", Update.errorString());
    http.end();
    return;
  }

  if (!Update.isFinished()) {
    Serial.println("✗ Update not finished!");
    http.end();
    return;
  }

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   FIRMWARE UPDATE SUCCESSFUL! ✓        ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("Total bytes written: " + String(totalWritten));
  Serial.println("Update time: " + String((millis() - startTime) / 1000) + " seconds");
  Serial.println("\nRebooting in 3 seconds...");
  
  http.end();
  delay(3000);
  ESP.restart();
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
  
  if (sendTransactionToServer(currentCard, totalCoins, "laundry_machine_1")) {
    Serial.println("✓ Transaction successful");
  } else {
    Serial.println("✗ Transaction failed!");
  }
  
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
    String serverURL = getServerURL();
    
    http.begin(serverURL);
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");
    
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
  
  digitalWrite(RELAY_PIN, LOW);
  relayActive = true;
  relayStartTime = millis();
  
  Serial.print("⚡ Relay ON for ");
  Serial.print(RELAY_PULSE_MS);
  Serial.println(" ms");
}

void updateRelay() {
  unsigned long now = millis();
  
  if (relayActive) {
    if (now - relayStartTime >= RELAY_PULSE_MS) {
      digitalWrite(RELAY_PIN, HIGH);
      relayActive = false;
      relayPulsesRemaining--;
      
      Serial.print("⚡ Relay pulse completed. Remaining: ");
      Serial.println(relayPulsesRemaining);
      
      if (relayPulsesRemaining > 0) {
        relayPauseTime = now;
      }
    }
  } else if (relayPulsesRemaining > 0) {
    if (now - relayPauseTime >= RELAY_PAUSE_MS) {
      activateRelay();
    }
  }
}

// ============================================
//          UTILITY FUNCTIONS
// ============================================
String getServerURL() {
  return activeServerURL;
}

String getModeString() {
  switch (currentMode) {
    case MODE_STATION:
      return "STATION (" + activeSSID + ")";
    case MODE_AP_ONLY:
      return "AP-ONLY (Local)";
    default:
      return "UNKNOWN";
  }
}
