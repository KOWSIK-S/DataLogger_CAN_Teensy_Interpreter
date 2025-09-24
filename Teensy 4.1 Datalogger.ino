/*
 * Teensy 4.1 SD Card Data Logger
 *
 * This sketch simulates logging CAN bus data to a CSV file on the
 * built-in SD card slot of a Teensy 4.1.
 *
 * Key Features:
 * - Logs continuously in the main loop.
 * - Creates a new, sequentially numbered log file on each startup
 * (e.g., LOG_001.CSV, LOG_002.CSV) to prevent overwriting data.
 * - Generates random data for parameters like RPM, TPS, etc., to
 * simulate a real-world vehicle.
 * - Flushes data to the SD card periodically to minimize data loss
 * in case of a sudden power-off.
 */

#include <SD.h>
#include <SPI.h>

// File object for the log file
File logfile;

// Counter to flush data to the SD card every N lines.
// This helps prevent data loss if the logger loses power.
int lineCounter = 0;
const int FLUSH_INTERVAL = 50; // Write to card every 50 log entries

void setup() {
  // Start serial communication for debugging
  Serial.begin(9600);
  // Optional: wait for serial port to connect. Needed for native USB only.
  // while (!Serial);

  Serial.println("Initializing SD card...");

  // Initialize the SD card on the built-in slot
  if (!SD.begin(BUILTIN_SDCARD)) {
    Serial.println("ERROR: SD card initialization failed!");
    Serial.println("Check card formatting (must be FAT16 or FAT32).");
    while (1); // Halt execution
  }
  Serial.println("SD card initialized.");

  // --- Find the next available log file name ---
  char filename[] = "LOG_000.CSV";
  for (int i = 0; i < 1000; i++) {
    filename[4] = i / 100 + '0';
    filename[5] = (i / 10) % 10 + '0';
    filename[6] = i % 10 + '0';
    if (!SD.exists(filename)) {
      // This filename does not exist, so we can use it.
      break;
    }
  }

  Serial.print("Opening new log file: ");
  Serial.println(filename);

  // Open the new log file in write mode
  logfile = SD.open(filename, FILE_WRITE);
  if (!logfile) {
    Serial.print("ERROR: Could not create file: ");
    Serial.println(filename);
    while (1); // Halt execution
  }

  Serial.println("Writing CSV header...");

  // Write the header row to the CSV file.
  // These headers match the format from the sample logs provided.
  logfile.println("Time(ms),RPM,TPS(%),Coolant(C),MAP(mBar),VBAT(V),Gear,Lambda1,CRC32");

  // Ensure the header is written to the card immediately
  logfile.flush();

  Serial.println("Setup complete. Starting data logging...");
}

void loop() {
  // --- Generate random data to simulate vehicle sensors ---
  unsigned long time = millis();
  int rpm = random(800, 8500);         // Random RPM between idle and redline
  float tps = random(0, 1000) / 10.0;  // Random throttle position (0-100%)
  float coolant = random(850, 950) / 10.0; // Random coolant temp (85.0-95.0 C)
  int map = random(250, 1013);         // Random manifold pressure (vacuum to atmospheric)
  float vbat = random(138, 144) / 10.0; // Random battery voltage (13.8-14.4 V)
  int gear = random(0, 6);             // Random gear (0 for neutral)
  float lambda = random(90, 150) / 100.0; // Random lambda value (rich to lean)
  int crc = random(0, 255);            // Fake CRC checksum

  // --- Write the data to the log file ---
  logfile.print(time);
  logfile.print(",");
  logfile.print(rpm);
  logfile.print(",");
  logfile.print(tps, 2); // Print float with 2 decimal places
  logfile.print(",");
  logfile.print(coolant, 1); // Print float with 1 decimal place
  logfile.print(",");
  logfile.print(map);
  logfile.print(",");
  logfile.print(vbat, 2);
  logfile.print(",");
  logfile.print(gear);
  logfile.print(",");
  logfile.print(lambda, 3);
  logfile.print(",");
  logfile.println(crc);

  // Also print to Serial Monitor for live debugging
  Serial.print("LOGGED: ");
  Serial.print(time);
  Serial.print(", ");
  Serial.println(rpm);


  // Periodically flush the data to the SD card to prevent data loss
  lineCounter++;
  if (lineCounter >= FLUSH_INTERVAL) {
    logfile.flush();
    lineCounter = 0; // Reset the counter
    Serial.println("--- Flushed data to SD card ---");
  }

  // Log data at a specific interval. delay(20) is 50Hz.
  delay(20);
}
