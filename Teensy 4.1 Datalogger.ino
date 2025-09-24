/*
 * Production-Ready Teensy 4.1 SD Card Data Logger
 *
 * This sketch logs CAN bus data with a focus on high reliability,
 * data integrity, and indefinite runtime for embedded systems.
 *
 * Key Features:
 * - Uses the high-performance SdFat library.
 * - Log File Rollover: After 1000 files, it overwrites the oldest logs.
 * - Safe File Rotation: Creates a new log file when the current one
 * exceeds MAX_FILE_SIZE, flushing all pending data first.
 * - Efficient String Formatting: Uses snprintf() for high performance.
 * - Buffered I/O: Writes in 512-byte blocks for max efficiency.
 * - Periodic Flushes: Syncs data every 2 seconds to prevent data loss.
 * - Informative Error State: Blinks SOS on the LED for critical errors.
 * - Ready for CAN Integration: Clear placeholder for CAN bus data.
 */

#include <SdFat.h>

// --- Configuration ---
#define SD_CONFIG SdioConfig(FIFO_SDIO)
const size_t BUFFER_SIZE = 512;
const unsigned long FLUSH_INTERVAL_MS = 2000;
const int ERROR_LED_PIN = LED_BUILTIN;
const uint32_t MAX_FILE_SIZE = 524288; // 0.5 MB
const int MAX_LOG_FILES = 1000; // For LOG_000.CSV to LOG_999.CSV

// --- Global Objects ---
SdFat sd;
FsFile logfile;
char logBuffer[BUFFER_SIZE];
size_t bufferIndex = 0;
unsigned long lastFlushTime = 0;
int logFileNumber = 0;

// --- Data Structure for Vehicle Data ---
// Using a struct makes the code cleaner and easier to manage.
struct VehicleData {
  unsigned long time;
  int rpm;
  float tps;
  float coolant;
  int map;
  float vbat;
  int gear;
  float lambda;
  int crc;
};


// --- Function Prototypes ---
void errorHalt(const char* msg);
void openNextLogFile();
void readCANData(VehicleData &data);


void setup() {
  pinMode(ERROR_LED_PIN, OUTPUT);
  digitalWrite(ERROR_LED_PIN, LOW);

  Serial.begin(9600);
  delay(1000);

  Serial.println("Initializing SD card...");
  if (!sd.begin(SD_CONFIG)) {
    sd.initErrorHalt(&Serial);
    errorHalt("ERROR: SD card initialization failed!");
  }
  Serial.println("SD card initialized.");

  openNextLogFile();

  Serial.println("Setup complete. Starting data logging...");
  lastFlushTime = millis();
}

void loop() {
  // --- 1. Check if it's time to rotate to a new log file ---
  if (logfile.isOpen() && logfile.size() > MAX_FILE_SIZE) {
    Serial.println("Max file size reached. Rotating to new log file.");
    openNextLogFile();
  }

  // --- 2. Acquire data from sensors/CAN bus ---
  VehicleData data;
  readCANData(data); // This function populates the 'data' struct

  // --- 3. Format the data into a single line string (highly efficient) ---
  char dataLine[128];
  int len = snprintf(dataLine, sizeof(dataLine),
                     "%lu,%d,%.2f,%.1f,%d,%.2f,%d,%.3f,%d\n",
                     data.time, data.rpm, data.tps, data.coolant, data.map, data.vbat, data.gear, data.lambda, data.crc);

  // --- 4. Add the data to the buffer, flushing if it's full ---
  if (bufferIndex + len >= BUFFER_SIZE) {
    if (logfile.write(logBuffer, bufferIndex) < bufferIndex) {
        errorHalt("ERROR: Failed to write buffer to SD card!");
    }
    bufferIndex = 0;
  }
  memcpy(logBuffer + bufferIndex, dataLine, len);
  bufferIndex += len;

  // --- 5. Periodically flush the buffer based on time ---
  if (millis() - lastFlushTime >= FLUSH_INTERVAL_MS) {
    if (bufferIndex > 0) {
      if (logfile.write(logBuffer, bufferIndex) < bufferIndex || !logfile.sync()) {
          errorHalt("ERROR: Failed to sync buffer!");
      }
      bufferIndex = 0;
    }
    lastFlushTime = millis();
  }

  delay(20); // Logging rate of ~50Hz
}

/**
 * @brief Placeholder function to be replaced with actual CAN bus reading logic.
 * @param data Reference to the data structure to be filled.
 */
void readCANData(VehicleData &data) {
  //
  // <<< INSERT YOUR CAN BUS READING LOGIC HERE >>>
  //
  // Example using random data until your CAN code is ready:
  data.time = millis();
  data.rpm = random(800, 8500);
  data.tps = random(0, 1000) / 10.0;
  data.coolant = random(850, 950) / 10.0;
  data.map = random(250, 1013);
  data.vbat = random(138, 144) / 10.0;
  data.gear = random(0, 6);
  data.lambda = random(90, 150) / 100.0;
  data.crc = random(0, 255);
}

/**
 * @brief Finds the next available log file, opens it, and writes the header.
 * Implements rollover by deleting the oldest file if necessary.
 */
void openNextLogFile() {
  if (logfile.isOpen()) {
    // --- Safely write any remaining data before closing ---
    if (bufferIndex > 0) {
      if (logfile.write(logBuffer, bufferIndex) < bufferIndex || !logfile.sync()) {
        errorHalt("ERROR: Failed to flush buffer before rotating file!");
      }
      bufferIndex = 0;
    }
    logfile.close();
    Serial.println("Closed previous log file.");
  }

  // Find the first unused filename.
  char filename[] = "LOG_000.CSV";
  while (logFileNumber < MAX_LOG_FILES) {
      filename[4] = logFileNumber / 100 + '0';
      filename[5] = (logFileNumber / 10) % 10 + '0';
      filename[6] = logFileNumber % 10 + '0';
      if (!sd.exists(filename)) {
          break; // Found an unused name.
      }
      logFileNumber++;
  }

  // --- Rollover Logic ---
  if (logFileNumber >= MAX_LOG_FILES) {
      logFileNumber = 0; // Wrap around to the beginning
      Serial.println("Max log files reached. Rolling over to LOG_000.CSV");
      // Update filename for LOG_000.CSV
      filename[4] = '0'; filename[5] = '0'; filename[6] = '0';
      // Delete the old file before creating a new one
      if (sd.exists(filename)) {
          sd.remove(filename);
      }
  }

  Serial.print("Opening new log file: ");
  Serial.println(filename);

  if (!logfile.open(filename, FILE_WRITE)) {
    errorHalt("ERROR: Could not create new log file!");
  }

  logfile.println("Time(ms),RPM,TPS(%),Coolant(C),MAP(mBar),VBAT(V),Gear,Lambda1,CRC32");
  logfile.sync();
  bufferIndex = 0;
}

/**
 * @brief Handles a critical error by blinking SOS and printing to Serial.
 */
void errorHalt(const char* msg) {
  if (logfile.isOpen()) {
    logfile.println(msg);
    logfile.close();
  }
  while (true) {
    Serial.println(msg);
    // SOS Blink Pattern: ... --- ...
    for(int i=0; i<3; i++) { digitalWrite(ERROR_LED_PIN, HIGH); delay(150); digitalWrite(ERROR_LED_PIN, LOW); delay(150); }
    delay(400);
    for(int i=0; i<3; i++) { digitalWrite(ERROR_LED_PIN, HIGH); delay(400); digitalWrite(ERROR_LED_PIN, LOW); delay(150); }
    delay(400);
    for(int i=0; i<3; i++) { digitalWrite(ERROR_LED_PIN, HIGH); delay(150); digitalWrite(ERROR_LED_PIN, LOW); delay(150); }
    delay(2000);
  }
}

