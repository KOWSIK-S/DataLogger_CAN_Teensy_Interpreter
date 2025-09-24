/*
 * Advanced Teensy 4.1 SD Card Data Logger
 *
 * This sketch logs simulated CAN bus data with advanced features for
 * robustness and long-term reliability.
 *
 * Key Features:
 * - Uses the high-performance SdFat library.
 * - Automatic File Rotation: Creates a new log file when the current
 * file exceeds a defined size (MAX_FILE_SIZE).
 * - Efficient String Formatting: Uses snprintf() to format data without
 * heap allocation, ensuring high performance and stability.
 * - Buffered I/O: Writes data in 512-byte blocks for maximum
 * efficiency and minimal wear on the SD card.
 * - Periodic Flushes: Syncs data to the card every 2 seconds to
 * prevent data loss from sudden power failure.
 * - Onboard LED indicates critical errors.
 */

#include <SdFat.h>

// --- Configuration ---
#define SD_CONFIG SdioConfig(FIFO_SDIO)
const size_t BUFFER_SIZE = 512;
const unsigned long FLUSH_INTERVAL_MS = 2000;
const int ERROR_LED_PIN = LED_BUILTIN;
// Set the maximum size for each log file (0.5 MB)
const uint32_t MAX_FILE_SIZE = 524288;


// --- Global Objects ---
SdFat sd;
FsFile logfile;
char logBuffer[BUFFER_SIZE];
size_t bufferIndex = 0;
unsigned long lastFlushTime = 0;

// Keep track of the current log file number
int logFileNumber = 0;

// --- Function Prototypes ---
void errorHalt(const char* msg);
void openNextLogFile();

void setup() {
  pinMode(ERROR_LED_PIN, OUTPUT);
  digitalWrite(ERROR_LED_PIN, LOW);

  Serial.begin(9600);
  delay(1000); // Allow serial monitor to connect

  Serial.println("Initializing SD card with SdFat...");
  if (!sd.begin(SD_CONFIG)) {
    sd.initErrorHalt(&Serial);
    errorHalt("ERROR: SD card initialization failed!");
  }
  Serial.println("SD card initialized.");

  // Find the first available file and open it
  openNextLogFile();

  Serial.println("Setup complete. Starting data logging...");
  lastFlushTime = millis();
}

// Safely close the file on shutdown/reset
void teensy_shutdown() {
    if (logfile.isOpen()) {
        if (bufferIndex > 0) {
            logfile.write(logBuffer, bufferIndex);
        }
        logfile.close();
    }
}

void loop() {
  // --- 1. Check if it's time to rotate to a new log file ---
  if (logfile.isOpen() && logfile.size() > MAX_FILE_SIZE) {
    Serial.println("Max file size reached. Rotating to new log file.");
    openNextLogFile();
  }

  // --- 2. Generate random data to simulate vehicle sensors ---
  unsigned long time = millis();
  int rpm = random(800, 8500);
  float tps = random(0, 1000) / 10.0;
  float coolant = random(850, 950) / 10.0;
  int map = random(250, 1013);
  float vbat = random(138, 144) / 10.0;
  int gear = random(0, 6);
  float lambda = random(90, 150) / 100.0;
  int crc = random(0, 255);

  // --- 3. Format the data into a single line string (highly efficient) ---
  char dataLine[128];
  int len = snprintf(dataLine, sizeof(dataLine),
                     "%lu,%d,%.2f,%.1f,%d,%.2f,%d,%.3f,%d\n",
                     time, rpm, tps, coolant, map, vbat, gear, lambda, crc);

  // --- 4. Add the data to the buffer, flushing if it's full ---
  if (bufferIndex + len >= BUFFER_SIZE) {
    if (logfile.write(logBuffer, bufferIndex) != bufferIndex) {
        errorHalt("ERROR: Failed to write buffer to SD card!");
    }
    bufferIndex = 0;
  }
  memcpy(logBuffer + bufferIndex, dataLine, len);
  bufferIndex += len;

  // --- 5. Periodically flush the buffer based on time ---
  if (millis() - lastFlushTime >= FLUSH_INTERVAL_MS) {
    if (bufferIndex > 0) {
      if (logfile.write(logBuffer, bufferIndex) != bufferIndex || !logfile.sync()) {
          errorHalt("ERROR: Failed to sync buffer!");
      }
      bufferIndex = 0;
    }
    lastFlushTime = millis();
  }

  // Optional: Print to Serial Monitor for live debugging
  // Note: Printing every loop can slow down high-frequency logging.
  // Serial.println(dataLine);

  delay(20); // Logging rate of ~50Hz
}


/**
 * @brief Finds the next available log file, opens it, and writes the header.
 */
void openNextLogFile() {
  // First, close the current file if it's open.
  if (logfile.isOpen()) {
    logfile.sync();
    logfile.close();
    Serial.println("Closed previous log file.");
  }

  char filename[] = "LOG_000.CSV";
  // Increment logFileNumber until we find a name that doesn't exist.
  while (logFileNumber <= 999) {
    filename[4] = logFileNumber / 100 + '0';
    filename[5] = (logFileNumber / 10) % 10 + '0';
    filename[6] = logFileNumber % 10 + '0';
    if (!sd.exists(filename)) {
      break; // Found an unused name
    }
    logFileNumber++;
  }

  if (logFileNumber > 999) {
    errorHalt("ERROR: No available log file names left (0-999 are used)!");
  }

  Serial.print("Opening new log file: ");
  Serial.println(filename);

  if (!logfile.open(filename, FILE_WRITE)) {
    errorHalt("ERROR: Could not create new log file!");
  }

  // Write the header to the new file
  logfile.println("Time(ms),RPM,TPS(%),Coolant(C),MAP(mBar),VBAT(V),Gear,Lambda1,CRC32");
  logfile.sync(); // Ensure header is written immediately
  bufferIndex = 0; // Reset buffer
}

/**
 * @brief Handles a critical, unrecoverable error.
 */
void errorHalt(const char* msg) {
  Serial.println(msg);
  // Also try to write error to SD card if possible
  if (logfile.isOpen()) {
    logfile.println(msg);
    logfile.close();
  }
  digitalWrite(ERROR_LED_PIN, HIGH); // Turn on LED
  while (true) {
    // Infinite loop to halt execution
  }
}

