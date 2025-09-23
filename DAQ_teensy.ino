/**
 * @file motostudent_logger_v4.ino
 * @brief Professional, High-Reliability CAN Logger for MotoStudent with Expanded Channel Set
 * @version 4.0
 * @details
 * This firmware is designed for a Teensy 4.1 to reliably log an expanded set of
 * CAN bus data from the AIM Taipan ECU. It is optimized for performance, data
 * integrity, and long-term reliability in a harsh motorsports environment.
 *
 * @version_history
 * v4.0 :
 * - EXPANDED DATASET: Now logs Oil Pressure, Oil Temp, Intake Air Temp, TC Slip %, and Ignition Angle.
 * - Updated CAN decoding logic to parse new messages and data points.
 * - Reworked CSV header and data logging functions to accommodate the new channels.
 * - Increased temporary line buffer to handle longer CSV rows safely.
 * v3.0:
 * - Implemented RTC-based filenames, decoupled SD flushing, event logging, and advanced LED status.
 * v2.0:
 * - Removed Arduino String class, using char buffers for deterministic performance.
 * v1.x:
 * - Initial prototype.
 */

#include <FlexCAN_T4.h>
#include <SdFat.h>
#include <WDT_T4.h>
#include <CRC32.h>
#include <TimeLib.h> // Required for RTC functionality

// -------------------- System Configuration --------------------
constexpr int LED_PIN = 13;
constexpr int CHIP_SELECT = BUILTIN_SDCARD;
constexpr int SD_CLK_MHZ = 50;
constexpr int CAN_BAUD_RATE = 500000; // 500 kbit/s for AIM CAN bus

#define CSV_HEADER "Type,Timestamp(ms),RPM,TPS(%%),Coolant(C),MAP(mBar),VBAT(V),Gear,Lambda1,OilPress(bar),OilTemp(C),IAT(C),Slip(%%),IgnAngle(deg),CRC32,Message\n"

// -------------------- Reliability & Performance Settings --------------------
constexpr uint32_t WDT_TIMEOUT_MS = 4000;         // Watchdog timeout (4 seconds)
constexpr uint32_t CAN_TIMEOUT_MS = 500;          // Time to wait for a CAN message before flagging a timeout
constexpr uint32_t LOG_INTERVAL_MS = 20;          // Log data to buffer every 20ms (50 Hz)
constexpr uint32_t FLUSH_INTERVAL_MS = 500;       // "Hard flush" SD card cache every 500ms
constexpr uint32_t FILE_SIZE_LIMIT = 25L * 1024L * 1024L; // 25MB limit per log file
constexpr size_t LOG_BUFFER_SIZE = 8192;          // 8KB RAM buffer for log lines

// -------------------- CAN Bus Message IDs (from AIM Protocol) --------------------
constexpr uint32_t ID_RPM_TPS = 0x5F0;
constexpr uint32_t ID_TEMPS = 0x5F2;
constexpr uint32_t ID_PRESSURES = 0x5F3;
constexpr uint32_t ID_VBAT_GEAR = 0x5F4;
constexpr uint32_t ID_LAMBDA = 0x5F6;
constexpr uint32_t ID_ADVANCED = 0x5FB; // Contains Slip and Ignition Angle

// -------------------- Global Objects & Variables --------------------
FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> Can0;
SdFat sd;
SdFile logfile;
WDT_T4<WDT1> wdt;

struct CAN_Data {
  int rpm = -1;
  float tps = -1.0f;
  float ect = -1.0f;
  float map_mbar = -1.0f;
  float vbat = -1.0f;
  int gear = -1;
  float lambda1 = -1.0f;
  float oil_press_bar = -1.0f;
  float oil_temp_c = -1.0f;
  float iat_c = -1.0f;
  float slip_percent = -1.0f;
  float ign_angle_deg = -99.0f;
};
CAN_Data currentData;

char logBuffer[LOG_BUFFER_SIZE];
size_t bufferIndex = 0;
char filename[32];
uint32_t lastLogTime = 0;
uint32_t lastFlushTime = 0;
uint32_t lastCANMessageTime = 0;
bool canTimeout = true;


// -------------------- Function Declarations --------------------
void setupRTC();
void newLogFile();
void flushBuffer();
void updateCANData(const CAN_message_t &msg);
void logDataPacket();
void logSystemEvent(const char* type, const char* message);
void updateLedStatus();
void safeDelay(uint32_t ms);
void systemHalt(const char* reason);


// -------------------- Setup Function --------------------
void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  safeDelay(2000); // Wait for serial connection
  Serial.println("\n--- MotoStudent Rugged CAN Logger v4.0 (Pro) ---");
  Serial.println("Initializing...");

  // 1. Initialize RTC
  setupRTC();

  // 2. Initialize CAN bus
  Can0.begin();
  Can0.setBaudRate(CAN_BAUD_RATE);
  Serial.println("CAN bus initialized.");

  // 3. Initialize SD card
  if (!sd.begin(CHIP_SELECT, SD_SCK_MHZ(SD_CLK_MHZ))) {
    systemHalt("SD Card init failed!");
  }
  Serial.println("SD card initialized.");

  // 4. Create the first log file
  newLogFile();
  
  // 5. Log the startup event
  logSystemEvent("INFO", "System Initialized; Logging Started");

  // 6. Initialize Watchdog Timer
  WDT_timings_t config;
  config.timeout = WDT_TIMEOUT_MS;
  wdt.begin(config);
  Serial.println("Watchdog timer enabled.");

  // 7. Initialize timers
  uint32_t now = millis();
  lastCANMessageTime = now;
  lastLogTime = now;
  lastFlushTime = now;

  Serial.println("System initialization complete.");
}


// -------------------- Main Loop --------------------
void loop() {
  wdt.feed();

  // Task 1: Read and decode all available CAN messages
  CAN_message_t msg;
  while (Can0.read(msg)) {
    if (canTimeout) {
      canTimeout = false;
      logSystemEvent("INFO", "CAN Bus Communication Resumed");
    }
    lastCANMessageTime = millis();
    updateCANData(msg);
  }

  // Task 2: Check for CAN communication timeout
  if (!canTimeout && (millis() - lastCANMessageTime > CAN_TIMEOUT_MS)) {
    canTimeout = true;
    logSystemEvent("ERROR", "CAN Bus Timeout");
    currentData = CAN_Data(); // Reset data to timeout values
  }

  // Task 3: Log data to the file buffer at a fixed interval
  if (millis() - lastLogTime >= LOG_INTERVAL_MS) {
    lastLogTime = millis();
    logDataPacket();
  }

  // Task 4: Flush the data buffer to the SD card at a fixed interval
  if (millis() - lastFlushTime >= FLUSH_INTERVAL_MS) {
    lastFlushTime = millis();
    flushBuffer();
  }

  // Task 5: Update the status LED
  updateLedStatus();
}


// -------------------- Core Functions --------------------

/**
 * @brief Decodes a CAN message and updates the global data struct.
 */
void updateCANData(const CAN_message_t &msg) {
  switch (msg.id) {
    case ID_RPM_TPS: // 0x5F0
      currentData.rpm = (msg.buf[1] << 8) | msg.buf[0];
      currentData.tps = ((msg.buf[3] << 8) | msg.buf[2]) / 650.0f; // val/65 -> %x10
      break;

    case ID_TEMPS: // 0x5F2
      // Formula for all temps: (((val / 19) - 450) / 10) -> Deg C
      currentData.iat_c = ((((msg.buf[1] << 8) | msg.buf[0]) / 19.0f) - 450.0f) / 10.0f;
      currentData.ect = ((((msg.buf[3] << 8) | msg.buf[2]) / 19.0f) - 450.0f) / 10.0f;
      currentData.oil_temp_c = ((((msg.buf[7] << 8) | msg.buf[6]) / 19.0f) - 450.0f) / 10.0f;
      break;

    case ID_PRESSURES: // 0x5F3
      currentData.map_mbar = ((msg.buf[1] << 8) | msg.buf[0]) / 10.0f; // val/10 -> mBar
      currentData.oil_press_bar = ((msg.buf[5] << 8) | msg.buf[4]) / 100.0f; // val/100 -> bar
      break;

    case ID_VBAT_GEAR: // 0x5F4
      currentData.vbat = (((msg.buf[3] << 8) | msg.buf[2]) / 32.0f) / 100.0f; // val/32 -> Vx100
      currentData.gear = (int16_t)((msg.buf[7] << 8) | msg.buf[6]);
      break;

    case ID_LAMBDA: // 0x5F6
      currentData.lambda1 = (((msg.buf[1] << 8) | msg.buf[0]) / 2.0f) / 1000.0f; // val/2 -> L*1000
      break;

    case ID_ADVANCED: // 0x5FB
      currentData.slip_percent = ((msg.buf[3] << 8) | msg.buf[2]) / 650.0f; // val/65 -> %x10
      currentData.ign_angle_deg = (int16_t)((msg.buf[5] << 8) | msg.buf[4]) / 3.0f / 100.0f; // val/3 -> deg*100
      break;
  }
}

/**
 * @brief Formats the current data packet and adds it to the log buffer.
 */
void logDataPacket() {
  char tempLine[256]; // Increased buffer size for the longer line
  
  // Create the data portion of the line first to calculate CRC
  int len = snprintf(tempLine, sizeof(tempLine), "DATA,%lu,%d,%.2f,%.1f,%.1f,%.2f,%d,%.3f,%.2f,%.1f,%.1f,%.2f,%.2f",
                     millis(), currentData.rpm, currentData.tps, currentData.ect,
                     currentData.map_mbar, currentData.vbat, currentData.gear, currentData.lambda1,
                     currentData.oil_press_bar, currentData.oil_temp_c, currentData.iat_c,
                     currentData.slip_percent, currentData.ign_angle_deg);

  uint32_t crc = CRC32::calculate(tempLine, len);

  // Append CRC and a newline. Note the empty field for the "Message" column.
  len += snprintf(tempLine + len, sizeof(tempLine) - len, ",%lu,\n", crc);

  if ((bufferIndex + len) >= LOG_BUFFER_SIZE) {
    flushBuffer();
  }
  memcpy(logBuffer + bufferIndex, tempLine, len);
  bufferIndex += len;

  // Check if we need to rotate to a new log file
  if (logfile.size() > FILE_SIZE_LIMIT) {
    flushBuffer();
    logfile.close();
    newLogFile();
  }
}

/**
 * @brief Logs a system event (like a timeout) to the buffer.
 */
void logSystemEvent(const char* type, const char* message) {
  char tempLine[256];
  // Format: Type,Timestamp,,,,,,,,,,,,CRC,Message
  int len = snprintf(tempLine, sizeof(tempLine), "%s,%lu,,,,,,,,,,,,,,%s\n", type, millis(), message);

  if ((bufferIndex + len) >= LOG_BUFFER_SIZE) {
    flushBuffer();
  }
  memcpy(logBuffer + bufferIndex, tempLine, len);
  bufferIndex += len;
}


// -------------------- Utility Functions --------------------

/**
 * @brief Sets up the Teensy's RTC using the compile time.
 */
void setupRTC() {
  // A helper function to parse the compile-time string
  auto parseMonth = [](const char* month) -> int {
    if (strcmp(month, "Jan") == 0) return 1; if (strcmp(month, "Feb") == 0) return 2;
    if (strcmp(month, "Mar") == 0) return 3; if (strcmp(month, "Apr") == 0) return 4;
    if (strcmp(month, "May") == 0) return 5; if (strcmp(month, "Jun") == 0) return 6;
    if (strcmp(month, "Jul") == 0) return 7; if (strcmp(month, "Aug") == 0) return 8;
    if (strcmp(month, "Sep") == 0) return 9; if (strcmp(month, "Oct") == 0) return 10;
    if (strcmp(month, "Nov") == 0) return 11; if (strcmp(month, "Dec") == 0) return 12;
    return 0;
  };

  char dateStr[] = __DATE__; // "Mmm dd yyyy"
  char timeStr[] = __TIME__; // "hh:mm:ss"
  
  char monthStr[4];
  int day, year;
  sscanf(dateStr, "%s %d %d", monthStr, &day, &year);

  tmElements_t tm;
  tm.Year = CalendarYrToTm(year);
  tm.Month = parseMonth(monthStr);
  tm.Day = day;
  sscanf(timeStr, "%d:%d:%d", &tm.Hour, &tm.Minute, &tm.Second);

  Teensy3Clock.set(makeTime(tm));
  setSyncProvider(Teensy3Clock.get);
  
  Serial.print("RTC initialized to: ");
  Serial.printf("%d-%02d-%02d %02d:%02d:%02d\n", year, tm.Month, day, tm.Hour, tm.Minute, tm.Second);
}

/**
 * @brief Creates a new log file with a timestamp-based name.
 */
void newLogFile() {
  // Format filename as YYYYMMDD_HHMMSS.csv
  snprintf(filename, sizeof(filename), "%d%02d%02d_%02d%02d%02d.csv",
           year(), month(), day(), hour(), minute(), second());

  if (!logfile.open(filename, O_WRONLY | O_CREAT | O_APPEND)) {
    systemHalt("Log file creation failed!");
  }

  Serial.print("Logging to new file: ");
  Serial.println(filename);

  // Write the header to the new file
  bufferIndex = snprintf(logBuffer, LOG_BUFFER_SIZE, CSV_HEADER);
  flushBuffer();
}

/**
 * @brief Writes the RAM buffer to the SdFat cache and flashes the LED.
 */
void flushBuffer() {
  if (bufferIndex > 0) {
    digitalWrite(LED_PIN, HIGH); // Quick flash to indicate write activity
    logfile.write(logBuffer, bufferIndex);
    bufferIndex = 0;
    // Note: We do NOT call logfile.flush() here for performance.
    // The main loop calls it on a timer.
    logfile.sync(); // sync() is a lighter version of flush()
    digitalWrite(LED_PIN, LOW);
  }
}

/**
 * @brief Manages the status LED patterns. Called continuously from loop().
 */
void updateLedStatus() {
  static uint32_t lastBlinkTime = 0;
  static bool ledState = false;

  if (canTimeout) {
    digitalWrite(LED_PIN, HIGH); // Solid ON for CAN Timeout
    return;
  }

  // Heartbeat blink for normal operation
  if (millis() - lastBlinkTime > (ledState ? 150 : 2000)) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
    lastBlinkTime = millis();
  }
}

/** @brief A watchdog-safe delay. */
void safeDelay(uint32_t ms) {
    uint32_t start = millis();
    while (millis() - start < ms) {
        wdt.feed();
        yield();
    }
}

/** @brief Prints a fatal error and halts the system with a flashing LED. */
void systemHalt(const char* reason) {
  Serial.print("FATAL ERROR: ");
  Serial.println(reason);
  while (1) {
    digitalWrite(LED_PIN, HIGH);
    safeDelay(150);
    digitalWrite(LED_PIN, LOW);
    safeDelay(150);
  }
}
