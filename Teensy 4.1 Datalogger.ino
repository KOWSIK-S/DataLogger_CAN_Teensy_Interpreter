#ifndef BUILTIN_SDCARD
  #define BUILTIN_SDCARD 254   // Teensy 4.1 onboard SD CS pin
#endif
// Step 1: SD write test - write numbers 1..100 to onboard SD (Teensy 4.1)
// Save as sd_write_test.ino

#include <SdFat.h>

constexpr int CHIP_SELECT = BUILTIN_SDCARD;   // Teensy 4.1 native SD slot
constexpr int SD_CLK_MHZ = 50;                // SD clock (works well on Teensy)
SdFat sd;
SdFile logfile;


char filename[32];

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); } // wait for USB serial

  Serial.println("\nSD Write Test: writing numbers 1..100 to onboard SD");

  // initialize SD card
  if (!sd.begin(CHIP_SELECT, SD_SCK_MHZ(SD_CLK_MHZ))) {
    Serial.println("ERROR: SD begin failed. Check card, slot, and formatting (FAT32).");
    while (1) { delay(1000); }
  }
  Serial.println("SD initialized.");

  // Create a filename that does not overwrite existing files (simple index search)
  uint16_t idx = 0;
  for (;;) {
    snprintf(filename, sizeof(filename), "test_%03u.csv", idx);
    if (!sd.exists(filename)) break;
    idx++;
    if (idx >= 1000) { Serial.println("No free filename slot!"); while (1) delay(1000); }
  }

  // Open file for writing (create/truncate)
  if (!logfile.open(filename, O_CREAT | O_TRUNC | O_WRONLY)) {
    Serial.print("ERROR: opening "); Serial.println(filename);
    while (1) { delay(1000); }
  }

  Serial.print("Writing to file: "); Serial.println(filename);

  // Write numbers 1..100, each on its own line
  for (int i = 1; i <= 100; ++i) {
    char line[16];
    int len = snprintf(line, sizeof(line), "%d\n", i);
    // write raw bytes
    if (logfile.write(line, len) != len) {
      Serial.print("Write error at i="); Serial.println(i);
      logfile.close();
      while (1) { delay(1000); }
    }
    // Print progress occasionally
    if (i % 10 == 0) {
      Serial.print("Wrote up to: "); Serial.println(i);
      // flush to ensure data is on card
      logfile.sync();
    }
  }

  // final flush & close
  logfile.sync();
  logfile.close();

  Serial.println("Write complete. File closed.");
  Serial.println("Remove SD card and inspect the file (or use Teensy to read it).");
}

void loop() {
  // Test finished; blink LED or idle
  static uint32_t t = 0;
  if (millis() - t > 500) {
    t = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
}
