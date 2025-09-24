#include <SD.h>
#include <SPI.h>

File logfile;

void setup() {
  Serial.begin(9600);
  while (!Serial) ; // Wait for USB Serial

  Serial.println("Testing Teensy 4.1 native SD slot...");

  if (!SD.begin(BUILTIN_SDCARD)) {
    Serial.println("ERROR: SD init failed. Check card (must be FAT32).");
    while (1);
  }

  logfile = SD.open("test.txt", FILE_WRITE);
  if (!logfile) {
    Serial.println("ERROR: Could not open file!");
    while (1);
  }

  logfile.println("Hello from Teensy 4.1!");
  logfile.close();

  Serial.println("Write complete. Check SD card for test.txt");
}

void loop() {


}
ChatGPT said:
