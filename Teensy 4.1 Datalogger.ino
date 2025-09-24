#include <SD.h>
#include <SPI.h>

File logfile;

const int ROWS = 10;
const int COLS = 10;

void setup() {
  Serial.begin(9600);
  while (!Serial); // Wait for USB Serial

  Serial.println("Testing Teensy 4.1 native SD slot...");

  if (!SD.begin(BUILTIN_SDCARD)) {
    Serial.println("ERROR: SD init failed. Check card (must be FAT32).");
    while (1);
  }

  logfile = SD.open("matrix.csv", FILE_WRITE);
  if (!logfile) {
    Serial.println("ERROR: Could not open file!");
    while (1);
  }

  // Write CSV headers
  logfile.print("Timestamp(ms)");
  for (int j = 0; j < COLS; j++) {
    logfile.print(",Col");
    logfile.print(j + 1);
  }
  logfile.println();

  // Create and write a 10x10 matrix with timestamps
  for (int i = 0; i < ROWS; i++) {
    unsigned long timestamp = millis(); // Current time in milliseconds
    logfile.print(timestamp);

    for (int j = 0; j < COLS; j++) {
      int value = i * COLS + j + 1; // Example data: 1 to 100
      logfile.print(",");
      logfile.print(value);
    }
    logfile.println();
    delay(100); // Optional: small delay to show changing timestamps
  }

  logfile.close();
  Serial.println("Matrix with timestamps written to matrix.csv");
}

void loop() {
  // Nothing to do here
}
