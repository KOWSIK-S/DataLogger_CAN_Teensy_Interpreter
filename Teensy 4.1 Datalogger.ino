/*
 * Rugged Teensy 4.1 SD Card Data Logger
 *
 * This sketch logs simulated CAN bus data to a CSV file on the
 * built-in SD card slot of a Teensy 4.1. It has been optimized
 * for reliability and performance.
 *
 * Key Features:
 * - Uses the high-performance SdFat library.
 * - Buffers data in RAM (512 bytes) to write in efficient chunks,
 * reducing SD card wear and improving logging speed.
 * - Periodically flushes the buffer to the SD card (every 2 seconds)
 * to minimize data loss in case of a sudden power-off.
 * - Creates a new, sequentially numbered log file on each startup
 * (e.g., LOG_001.CSV, LOG_002.CSV).
 * - Uses the onboard LED to signal critical errors (e.g., SD init fail).
 * - Safely syncs and closes the file on shutdown/reset.
 */

#include <SdFat.h>

// --- Configuration ---
// Use Teensy 4.1's fast, built-in SDIO port.
#define SD_CONFIG SdioConfig(FIFO_SDIO)

// Define the size of our data buffer in bytes.
// 512 is a good choice as it matches a standard SD card block size.
const size_t BUFFER_SIZE = 512;

// How often to force-write the buffer to the SD card, in milliseconds.
const unsigned long FLUSH_INTERVAL_MS = 2000;

// Set the pin for the onboard LED for error indication.
const int ERROR_LED_PIN = LED_BUILTIN;


// --- Global Objects ---
SdFat sd;
FsFile logfile;

// The data buffer and a pointer to its current position.
char logBuffer[BUFFER_SIZE];
size_t bufferIndex = 0;

// Timer for periodic buffer flushing.
unsigned long lastFlushTime = 0;

// Helper function to handle critical errors.
void errorHalt(const char* msg) {
  Serial.println(msg);
  digitalWrite(ERROR_LED_PIN, HIGH); // Turn on LED to indicate error
  while (true) {
    // Halt execution
  }
}

void setup() {
  pinMode(ERROR_LED_PIN, OUTPUT);
  digitalWrite(ERROR_LED_PIN, LOW);

  Serial.begin(9600);
  // A small delay to allow the serial monitor to connect.
  delay(1000);

  Serial.println("Initializing SD card with SdFat...");

  // Initialize the SD card.
  if (!sd.begin(SD_CONFIG)) {
    sd.initErrorHalt(&Serial); // SdFat's detailed error reporting
    errorHalt("ERROR: SD card initialization failed!");
  }
  Serial.println("SD card initialized.");

  // --- Find the next available log file name ---
  char filename[] = "LOG_000.CSV";
  for (int i = 0; i < 1000; i++) {
    filename[4] = i / 100 + '0';
    filename[5] = (i / 10) % 10 + '0';
    filename[6] = i % 10 + '0';
    if (!sd.exists(filename)) {
      break; // Found an unused name
    }
  }

  Serial.print("Opening new log file: ");
  Serial.println(filename);

  // Open the new log file.
  if (!logfile.open(filename, FILE_WRITE)) {
    errorHalt("ERROR: Could not create file!");
  }

  Serial.println("Writing CSV header...");
  logfile.println("Time(ms),RPM,TPS(%),Coolant(C),MAP(mBar),VBAT(V),Gear,Lambda1,CRC32");

  // A manual first flush ensures the header is written immediately.
  logfile.sync();

  Serial.println("Setup complete. Starting data logging...");
  lastFlushTime = millis();
}


// This function is called when the Teensy is about to shut down or reboot.
// It ensures that any remaining data in the buffer is saved.
void teensy_shutdown() {
    if (logfile.isOpen()) {
        if (bufferIndex > 0) {
            logfile.write(logBuffer, bufferIndex);
        }
        logfile.close();
    }
}


void loop() {
  // --- 1. Generate random data to simulate vehicle sensors ---
  unsigned long time = millis();
  int rpm = random(800, 8500);
  float tps = random(0, 1000) / 10.0;
  float coolant = random(850, 950) / 10.0;
  int map = random(250, 1013);
  float vbat = random(138, 144) / 10.0;
  int gear = random(0, 6);
  float lambda = random(90, 150) / 100.0;
  int crc = random(0, 255);

  // --- 2. Format the data into a single line string ---
  char dataLine[128];
  // snprintf is a safe way to format strings without buffer overflows.
  int len = snprintf(dataLine, sizeof(dataLine),
                     "%lu,%d,%.2f,%.1f,%d,%.2f,%d,%.3f,%d\n",
                     time, rpm, tps, coolant, map, vbat, gear, lambda, crc);

  // --- 3. Add the data to the buffer, flushing if it's full ---
  // Check if the new data will overflow the buffer.
  if (bufferIndex + len >= BUFFER_SIZE) {
    // Buffer is full, write it to the SD card.
    if (logfile.write(logBuffer, bufferIndex) != bufferIndex) {
        errorHalt("ERROR: Failed to write buffer to SD card!");
    }
    bufferIndex = 0; // Reset buffer index.
  }

  // Copy the new data line into the buffer.
  memcpy(logBuffer + bufferIndex, dataLine, len);
  bufferIndex += len;

  // --- 4. Periodically flush the buffer based on time ---
  if (millis() - lastFlushTime >= FLUSH_INTERVAL_MS) {
    if (bufferIndex > 0) { // Only flush if there's new data
      // Write the current buffer contents to the card.
      if (logfile.write(logBuffer, bufferIndex) != bufferIndex) {
          errorHalt("ERROR: Failed to write buffer on periodic flush!");
      }
      // Force the write to the card's physical media.
      if (!logfile.sync()) {
          errorHalt("ERROR: Failed to sync file!");
      }
      bufferIndex = 0; // Reset buffer.
    }
    lastFlushTime = millis(); // Reset the timer.
  }
  
  // Also print to Serial Monitor for live debugging (optional)
  Serial.print("LOGGED: ");
  Serial.print(time);
  Serial.print(", ");
  Serial.println(rpm);

  // Log data at a specific interval. delay(20) is approx 50Hz.
  delay(20);
}

