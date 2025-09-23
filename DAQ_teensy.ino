/**
 * @file motostudent_logger_v2.ino
 * @brief Rugged and High-Performance CAN Bus Logger for MotoStudent
 * @version 2.0
 *
 * @details
 * This firmware is designed for a Teensy 4.1 to reliably log CAN bus data from an
 * AIM ECU to an SD card. It is optimized for performance, stability, and data integrity
 * in a harsh electrical environment.
 * Key Improvements in v2.0:
 * - Complete removal of the Arduino String class for logging to prevent memory
 * fragmentation and ensure deterministic performance.
 * - Data is organized into a central struct (CAN_Data) for clean management.
 * - Logging is decoupled from CAN message arrival; data is logged at a fixed interval.
 * - Highly optimized buffering system writes large chunks to the SD card, reducing
 * wear and improving performance.
 * - Enhanced status LED feedback for quick diagnostics.
 * - Improved code structure with helper functions for readability and maintenance.
 * - Added more constants for clarity and easy configuration.
 */

#include <FlexCAN_T4.h>
#include <SdFat.h>
#include <WDT_T4.h>
#include <CRC32.h>

// -------------------- System Configuration --------------------
const int LED_PIN = 13;
const int CHIP_SELECT = BUILTIN_SDCARD;
const int SD_CLK_MHZ = 50;
const int BAUD_RATE = 500000; // 500 kbit/s for AIM CAN bus

// -------------------- Reliability & Performance Settings --------------------
const uint32_t WDT_TIMEOUT_MS = 4000;         // Watchdog timeout (4 seconds)
const uint32_t CAN_TIMEOUT_MS = 500;          // Time to wait for a CAN message before flagging a timeout
const uint32_t LOG_INTERVAL_MS = 20;          // Log data to buffer every 20ms (50 Hz)
const uint32_t FILE_SIZE_LIMIT = 10L * 1024L * 1024L; // 10MB limit per log file
const size_t LOG_BUFFER_SIZE = 8192;          // 8KB RAM buffer for log lines before writing to SD

// -------------------- CAN Bus Message IDs (from AIM Protocol) --------------------
const uint32_t ID_RPM_TPS = 0x5F0;
const uint32_t ID_TEMPS = 0x5F2;
const uint32_t ID_PRESSURES = 0x5F3;
const uint32_t ID_VBAT_GEAR = 0x5F4;
const uint32_t ID_LAMBDA = 0x5F6;

// -------------------- Global Objects & Variables --------------------
FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> Can0;
SdFat sd;
SdFile logfile;
WDT_T4<WDT1> wdt;

// A struct to hold the latest state of all decoded data
struct CAN_Data {
  int rpm = -1;
  float tps = -1.0f;
  float ect = -1.0f;
  float map_mbar = -1.0f;
  float vbat = -1.0f;
  int gear = -1;
  float lambda1 = -1.0f;
};

CAN_Data currentData;

// Buffer for storing log data before writing to SD card
char logBuffer[LOG_BUFFER_SIZE];
size_t bufferIndex = 0;
char filename[32];
uint16_t fileIndex = 0;
uint32_t lastLogTime = 0;
uint32_t lastCANMessageTime = 0;
bool canTimeout = true;


// -------------------- Function Declarations --------------------
void newLogFile();
void flushBuffer();
void updateCANData(const CAN_message_t &msg);
void logDataToFile();
void safeDelay(uint32_t ms);
void systemHalt(const char* reason);


// -------------------- Setup Function --------------------
void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  safeDelay(2000); // Wait for serial connection
  Serial.println("\n--- MotoStudent Rugged CAN Logger v2.0 ---");
  Serial.println("Initializing...");

  // 1. Initialize CAN bus
  Can0.begin();
  Can0.setBaudRate(BAUD_RATE);
  Serial.println("CAN bus initialized.");

  // 2. Initialize SD card
  if (!sd.begin(CHIP_SELECT, SD_SCK_MHZ(SD_CLK_MHZ))) {
    systemHalt("SD card initialization failed!");
  }
  Serial.println("SD card initialized.");

  // 3. Create the first log file
  newLogFile();

  // 4. Initialize Watchdog Timer
  WDT_timings_t config;
  config.timeout = WDT_TIMEOUT_MS;
  wdt.begin(config);
  Serial.println("Watchdog timer enabled.");

  // 5. Initialize timers
  lastCANMessageTime = millis();
  lastLogTime = millis();

  Serial.println("System initialization complete. Logging started.");
}


// -------------------- Main Loop --------------------
void loop() {
  wdt.feed(); // Pet the watchdog

  // Task 1: Read and decode all available CAN messages
  CAN_message_t msg;
  while (Can0.read(msg)) {
    if (canTimeout) {
      Serial.println("CAN bus communication resumed.");
      digitalWrite(LED_PIN, LOW); // Turn off timeout LED
    }
    canTimeout = false;
    lastCANMessageTime = millis();
    updateCANData(msg);
  }

  // Task 2: Check for CAN communication timeout
  if (!canTimeout && (millis() - lastCANMessageTime > CAN_TIMEOUT_MS)) {
    canTimeout = true;
    Serial.println("ERROR: CAN bus timeout!");
    digitalWrite(LED_PIN, HIGH); // Solid LED indicates CAN timeout
    // Reset data to default timeout values
    currentData = CAN_Data();
  }

  // Task 3: Log data to the file buffer at a fixed interval
  if (millis() - lastLogTime >= LOG_INTERVAL_MS) {
    lastLogTime = millis();
    logDataToFile();
  }
}


// -------------------- Helper Functions --------------------

/**
 * @brief Decodes a CAN message and updates the global data struct.
 */
void updateCANData(const CAN_message_t &msg) {
  switch (msg.id) {
    case ID_RPM_TPS: // 0x5F0
      currentData.rpm = (msg.buf[1] << 8) | msg.buf[0];
      // Formula: (value / 65) -> %x10. To get %, divide by another 10.
      currentData.tps = ((msg.buf[3] << 8) | msg.buf[2]) / 650.0f;
      break;

    case ID_TEMPS: // 0x5F2
      // Formula: ((value / 19) - 450) -> Cx10. To get C, divide by 10.
      currentData.ect = ((((msg.buf[3] << 8) | msg.buf[2]) / 19.0f) - 450.0f) / 10.0f;
      break;

    case ID_PRESSURES: // 0x5F3
      // Formula: (value / 10) -> mBar
      currentData.map_mbar = ((msg.buf[1] << 8) | msg.buf[0]) / 10.0f;
      break;

    case ID_VBAT_GEAR: // 0x5F4
      // Formula: (value / 32) -> Vx100. To get V, divide by 100.
      currentData.vbat = (((msg.buf[3] << 8) | msg.buf[2]) / 32.0f) / 100.0f;
      currentData.gear = (int16_t)((msg.buf[7] << 8) | msg.buf[6]);
      break;

    case ID_LAMBDA: // 0x5F6
      // Formula: (value / 2) -> lambdaX1000. To get lambda, divide by 1000.
      currentData.lambda1 = (((msg.buf[1] << 8) | msg.buf[0]) / 2.0f) / 1000.0f;
      break;
  }
}

/**
 * @brief Formats the current data and adds it to the log buffer.
 * Flushes the buffer to the SD card if it's full.
 */
void logDataToFile() {
  char tempLine[128]; // A temporary buffer for the formatted line

  // 1. Construct the data line string (without CRC)
  int len = snprintf(tempLine, sizeof(tempLine), "%lu,%d,%.2f,%.1f,%.1f,%.2f,%d,%.3f",
                     millis(),
                     currentData.rpm,
                     currentData.tps,
                     currentData.ect,
                     currentData.map_mbar,
                     currentData.vbat,
                     currentData.gear,
                     currentData.lambda1);

  // 2. Compute CRC32 on the data line
  uint32_t crc = CRC32::calculate(tempLine, len);

  // 3. Append the CRC to the temp line
  len += snprintf(tempLine + len, sizeof(tempLine) - len, ",%lu\n", crc);

  // 4. If the new line doesn't fit in the buffer, flush the buffer first
  if ((bufferIndex + len) >= LOG_BUFFER_SIZE) {
    flushBuffer();
  }

  // 5. Copy the new line into the main log buffer
  memcpy(logBuffer + bufferIndex, tempLine, len);
  bufferIndex += len;

  // 6. Check if we need to rotate to a new log file
  if (logfile.size() > FILE_SIZE_LIMIT && fileIndex < 999) {
    flushBuffer(); // Write any remaining data to the old file
    logfile.close();
    newLogFile();
  }
}

/**
 * @brief Writes the contents of the log buffer to the SD card and resets it.
 */
void flushBuffer() {
  if (bufferIndex > 0) {
    digitalWrite(LED_PIN, HIGH); // Turn on LED to indicate write activity
    logfile.write(logBuffer, bufferIndex);
    logfile.flush(); // Ensure data is physically written
    bufferIndex = 0;
    digitalWrite(LED_PIN, LOW); // Turn off LED
  }
}

/**
 * @brief Creates a new, uniquely named log file.
 */
void newLogFile() {
  do {
    snprintf(filename, sizeof(filename), "LOG_%03d.CSV", fileIndex++);
  } while (sd.exists(filename));

  if (!logfile.open(filename, O_WRONLY | O_CREAT | O_APPEND)) {
    systemHalt("Log file creation failed!");
  }

  Serial.print("Logging to new file: ");
  Serial.println(filename);

  // Use snprintf to avoid String class at all costs
  bufferIndex = snprintf(logBuffer, LOG_BUFFER_SIZE, "Time(ms),RPM,TPS(%%),Coolant(C),MAP(mBar),VBAT(V),Gear,Lambda1,CRC32\n");
  flushBuffer();
}

/**
 * @brief A non-blocking delay that feeds the watchdog timer.
 */
void safeDelay(uint32_t ms) {
    uint32_t start = millis();
    while (millis() - start < ms) {
        wdt.feed();
    }
}

/**
 * @brief Prints a fatal error to Serial and halts the system.
 */
void systemHalt(const char* reason) {
  Serial.print("FATAL ERROR: ");
  Serial.println(reason);
  // Blink the LED rapidly to indicate a fatal error
  while (1) {
    digitalWrite(LED_PIN, HIGH);
    safeDelay(150);
    digitalWrite(LED_PIN, LOW);
    safeDelay(150);
  }
}
