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

  // Create a 10x10 matrix with example values
  int matrix[ROWS][COLS];
  for (int i = 0; i < ROWS; i++) {
    for (int j = 0; j < COLS; j++) {
      matrix[i][j] = i * COLS + j + 1; // Fill with numbers 1 to 100
    }
  }

  // Write matrix to CSV
  for (int i = 0; i < ROWS; i++) {
    for (int j = 0; j < COLS; j++) {
      logfile.print(matrix[i][j]);
      if (j < COLS - 1) {
        logfile.print(","); // Comma between values
      }
    }
    logfile.println(); // Newline at the end of each row
  }

  logfile.close();
  Serial.println("Matrix written to matrix.csv");
}

void loop() {
  // Nothing to do in loop
}
