/*
 * SMART HELMET - SIM7000G SMS Test
 * LilyGO T-SIM7000G (ESP32)
 *
 * SMS-only test: modem connectivity + SMS memory check + SMS sending
 * Tests AT commands, SIM card detection, SMS storage capacity, and SMS transmission
 */

#include <HardwareSerial.h>

// SIM7000G UART pins (T-SIM7000G ESP32)
#define SIM_RX_PIN 26        // Modem TX (ESP32 RX input)
#define SIM_TX_PIN 27        // Modem RX (ESP32 TX output)
#define SIM_BAUD 115200

// Modem control pins
#define MODEM_POWER_PIN 4    // PWR_KEY (low pulse to power on)

HardwareSerial SimSerial(1);  // UART1 for modem
unsigned long lastATCheck = 0;
const unsigned long AT_CHECK_INTERVAL = 5000;  // Check modem every 5s
bool modemReady = false;
unsigned long smsTestCounter = 0;
bool showMenu = true;  // Show menu on startup

// Send AT command and wait for response
String sendAT(String cmd, unsigned long timeout = 1000) {
  // Clear input buffer before sending
  while (SimSerial.available()) {
    SimSerial.read();
  }
  delay(10);
  
  SimSerial.println(cmd);
  SimSerial.flush();  // Ensure data is sent
  
  unsigned long start = millis();
  String response = "";
  
  while (millis() - start < timeout) {
    if (SimSerial.available()) {
      char c = SimSerial.read();
      response += c;
    }
  }
  
  // Debug: print AT command and response (truncate long responses)
  Serial.print("[AT] > ");
  Serial.println(cmd);
  Serial.print("[AT] < ");
  if (response.length() > 200) {
    Serial.print(response.substring(0, 200));
    Serial.println("... [truncated]");
  } else {
    Serial.println(response);
  }
  Serial.println("---");
  
  return response;
}

// Check if modem responds to AT command
bool checkModemAlive() {
  String response = sendAT("AT", 1000);
  return response.indexOf("OK") != -1;
}

// Initialize modem for SMS
bool initModem() {
  Serial.println("\n[MODEM] Initializing SIM7000G...");
  Serial.print("[MODEM] Using UART pins: RX=");
  Serial.print(SIM_RX_PIN);
  Serial.print(" TX=");
  Serial.println(SIM_TX_PIN);
  
  // Clear any garbage in UART buffer
  while (SimSerial.available()) {
    SimSerial.read();
  }
  delay(100);
  
  // Pulse PWR_KEY to power on modem
  Serial.println("[MODEM] Powering on (PWR_KEY pulse)...");
  pinMode(MODEM_POWER_PIN, OUTPUT);
  digitalWrite(MODEM_POWER_PIN, LOW);
  delay(1200);  // Hold low for 1.2 seconds
  digitalWrite(MODEM_POWER_PIN, HIGH);
  Serial.println("[MODEM] PWR_KEY released, waiting for boot...");
  delay(2000);  // Wait 2 seconds after release
  
  // Wait for modem to respond with 30 second timeout
  Serial.print("[MODEM] Waiting for AT response");
  unsigned long bootTimeout = millis();
  while (!checkModemAlive() && millis() - bootTimeout < 30000) {
    delay(500);
    Serial.print(".");
  }
  
  if (!checkModemAlive()) {
    Serial.println("\n[MODEM] ERROR: Not responding to AT commands!");
    return false;
  }
  
  Serial.println("\n[MODEM] ✅ Responding to AT commands");
  
  // Disable echo
  sendAT("ATE0", 1000);
  delay(100);
  
  // Check SIM presence
  String simStatus = sendAT("AT+CCID", 1000);
  if (simStatus.indexOf("ERROR") != -1) {
    Serial.println("[MODEM] WARNING: SIM may not be detected");
  } else {
    Serial.println("[MODEM] ✅ SIM detected");
  }
  
  Serial.println("[MODEM] ✅ Modem ready for SMS");
  return true;
}

// Check SMS memory storage status
void checkSMSMemory() {
  Serial.println("\n[SMS] Checking message storage...");
  
  // AT+CPMS? - Get current selected memory and used/total counts
  String response = sendAT("AT+CPMS?", 1000);
  Serial.print("[SMS] Storage Response: ");
  Serial.println(response);
  
  // Set preferred SMS storage to SIM card (SM)
  sendAT("AT+CPMS=\"SM\",\"SM\",\"SR\"", 1000);
  delay(200);
  
  // Check again after setting
  Serial.println("\n[SMS] After setting SMS storage to SIM:");
  response = sendAT("AT+CPMS?", 1000);
  
  // List all SMS messages on SIM
  Serial.println("\n[SMS] Listing all messages on SIM:");
  sendAT("AT+CMGL=\"ALL\"", 2000);
}

// Count total SMS messages on SIM
int countSMSMessages() {
  Serial.println("\n[SMS] Counting total messages...");
  
  String response = sendAT("AT+CPMS?", 1000);
  
  // Extract used message count from response
  // Format: +CPMS: <used>,<total>,...
  int commaPos = response.indexOf(',');
  int usedCount = 0;
  
  if (commaPos > 0) {
    int startPos = response.lastIndexOf(' ', commaPos);
    if (startPos > 0) {
      String numStr = response.substring(startPos + 1, commaPos);
      usedCount = numStr.toInt();
    }
  }
  
  Serial.print("[SMS] Total messages stored: ");
  Serial.println(usedCount);
  
  return usedCount;
}

// Send SMS
bool sendSMS(const char *number, const char *message) {
  Serial.print("[SMS] Sending to ");
  Serial.print(number);
  Serial.print(": ");
  Serial.println(message);
  
  // Clear buffer
  while (SimSerial.available()) {
    SimSerial.read();
  }
  
  // Step 1: Set SMS text mode
  String response = sendAT("AT+CMGF=1", 1000);
  if (response.indexOf("OK") == -1) {
    Serial.println("[SMS] ❌ Failed to set text mode");
    return false;
  }
  delay(200);
  
  // Step 2: Send CMGS command to set recipient
  String cmd = "AT+CMGS=\"";
  cmd += number;
  cmd += "\"";
  
  Serial.print("[SMS] Setting recipient: ");
  Serial.println(cmd);
  SimSerial.println(cmd);
  SimSerial.flush();
  delay(100);
  
  // Step 3: Wait for ">" prompt from modem (max 2 seconds)
  Serial.println("[SMS] Waiting for > prompt...");
  String promptWait = "";
  unsigned long promptStart = millis();
  bool gotPrompt = false;
  
  while (millis() - promptStart < 2000) {
    if (SimSerial.available()) {
      char c = SimSerial.read();
      promptWait += c;
      if (promptWait.indexOf('>') != -1) {
        gotPrompt = true;
        break;
      }
    }
    delay(10);
  }
  
  if (!gotPrompt) {
    Serial.println("[SMS] ❌ No > prompt received");
    Serial.print("[SMS] Received: ");
    Serial.println(promptWait);
    return false;
  }
  
  Serial.println("[SMS] ✅ Got > prompt, sending message content...");
  delay(100);
  
  // Step 4: Send message text
  SimSerial.print(message);
  SimSerial.flush();
  delay(100);
  
  // Step 5: Send Ctrl+Z (ASCII 26) to submit
  SimSerial.write(26);
  SimSerial.flush();
  Serial.println("[SMS] Sent Ctrl+Z, waiting for response...");
  
  // Step 6: Wait for +CMGS: response (max 10 seconds for network)
  delay(1000);  // Give modem time to send
  String sendResponse = "";
  unsigned long responseStart = millis();
  
  while (millis() - responseStart < 10000) {
    if (SimSerial.available()) {
      char c = SimSerial.read();
      sendResponse += c;
    }
    delay(10);
  }
  
  Serial.print("[SMS] Final Response: ");
  Serial.println(sendResponse);
  
  // Check for success indicators
  bool success = (sendResponse.indexOf("+CMGS:") != -1) || 
                 (sendResponse.indexOf("OK") != -1 && sendResponse.indexOf("ERROR") == -1);
  
  if (success) {
    Serial.println("[SMS] ✅ SMS sent successfully!");
  } else {
    Serial.println("[SMS] ⚠️  SMS send status unclear");
    return false;
  }
  
  return true;
}

// Print menu options
void printMenu() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   SMART HELMET - SIM7000G SMS Control  ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("1. LIST SMS MESSAGES");
  Serial.println("2. DELETE ALL SMS");
  Serial.println("3. SEND TEST SMS");
  Serial.println("4. CHECK SMS MEMORY STATUS");
  Serial.println("5. CHECK MODEM STATUS");
  Serial.println("6. CLEAR SCREEN");
  Serial.println("────────────────────────────────────────");
  Serial.print("Select option (1-6): ");
}

// Handle menu selection
void handleMenuChoice(char choice) {
  switch (choice) {
    case '1':
      Serial.println("\n📋 Listing all SMS messages...\n");
      checkSMSMemory();
      delay(1000);
      break;
      
    case '2':
      Serial.println("\n🗑️ Deleting all SMS messages...\n");
      sendAT("AT+CMGDA=\"DEL ALL\"", 3000);  // Delete all messages
      delay(500);
      Serial.println("\n✅ All messages deleted");
      delay(1000);
      break;
      
    case '3':
      Serial.println("\n📤 Sending test SMS to +97474480314...\n");
      sendSMS("+97474480314", "SMART HELMET - Manual SMS Test");
      delay(1000);
      break;
      
    case '4':
      Serial.println("\n💾 Checking SMS memory...\n");
      checkSMSMemory();
      delay(1000);
      break;
      
    case '5':
      Serial.println("\n📡 Modem Status:");
      Serial.print("   Connected: ");
      Serial.println(modemReady ? "✅ Yes" : "❌ No");
      if (checkModemAlive()) {
        Serial.println("   Responding to AT: ✅ Yes");
        sendAT("AT+CCID", 1000);  // Show SIM CCID
      } else {
        Serial.println("   Responding to AT: ❌ No");
      }
      delay(1000);
      break;
      
    case '6':
      // Clear screen (ANSI escape codes)
      Serial.write(27);       // ESC
      Serial.print("[2J");    // Clear screen
      Serial.write(27);       // ESC
      Serial.print("[H");     // Home cursor
      break;
      
    default:
      Serial.println("\n❌ Invalid option! Please select 1-6");
      delay(500);
      break;
  }
  
  showMenu = true;  // Show menu again after command
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n================================");
  Serial.println("[BOOT] SMART HELMET - SMS Test");
  Serial.println("================================\n");

  // Initialize modem serial
  Serial.println("[UART] Initializing modem serial (UART1)...");
  SimSerial.begin(SIM_BAUD, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);
  SimSerial.flush();
  delay(1500);  // Give UART time to stabilize
  Serial.println("[UART] ✅ UART1 initialized");
  delay(500);
  
  // Power down CPU to save power (not necessary for testing)
  // setCpuFrequencyMhz(80);

  // Initialize modem
  modemReady = initModem();
  
  if (modemReady) {
    Serial.println("\n[BOOT] ✅ Modem initialized successfully");
    delay(1000);
    
    // Check SMS memory and list messages
    checkSMSMemory();
    delay(1000);
    
    countSMSMessages();
    delay(2000);
  } else {
    Serial.println("\n[BOOT] ⚠️  Modem initialization failed");
  }
  
  Serial.println("\n[BOOT] System ready - Interactive menu mode");
  delay(1000);
}

void loop() {
  // Show menu on startup and after each command
  if (showMenu) {
    printMenu();
    showMenu = false;
  }

  // Read user input from Serial monitor
  if (Serial.available()) {
    char choice = Serial.read();
    
    // Skip newlines and carriage returns
    if (choice == '\n' || choice == '\r') {
      return;
    }
    
    Serial.println(choice);  // Echo the selection
    handleMenuChoice(choice);
  }

  // Silent background modem health check (every 10 seconds, no debug output)
  if (millis() - lastATCheck > 10000) {
    lastATCheck = millis();
    
    // Check modem but don't print anything (sendAT prints debug info)
    SimSerial.println("AT");
    SimSerial.flush();
    delay(100);
    
    // Just read response silently
    while (SimSerial.available()) {
      SimSerial.read();
    }
  }

  // Keep serial responsive - display any incoming modem unsolicited data
  if (SimSerial.available()) {
    String incoming = SimSerial.readStringUntil('\n');
    if (incoming.length() > 0) {
      Serial.print("[MODEM>] ");
      Serial.println(incoming);
    }
  }

  delay(100);  // Small delay to prevent CPU spinning
}
