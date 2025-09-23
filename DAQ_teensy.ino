Updated v1.1 

#include <FlexCAN_T4.h>
#include <SdFat.h>
#include <WDT_T4.h>
#include <CRC32.h>

// -------------------- Hardware & System Setup --------------------
FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> Can0;
SdFat sd;
SdFile logfile;
WDT_T4<WDT1> wdt;

const int LED_PIN = 13; // Onboard LED for status
const int CHIP_SELECT = BUILTIN_SDCARD;
char filename[32];
uint16_t fileIndex = 0;

// -------------------- Performance & Reliability Settings --------------------
String logBuffer = "";
const int BUFFER_SIZE = 50;       // Number of lines to buffer in RAM before writing
const uint32_t FILE_SIZE_LIMIT = 5L * 1024L * 1024L; // 5MB limit per log file
const uint32_t TIMEOUT_MS = 250;  // CAN bus timeout in milliseconds
uint32_t lastMessageTime = 0;
int lineCount = 0;

// -------------------- Global Decoded Data Variables --------------------
// Initialized to -1 to indicate no data received yet
int rpm = -1;
float tps = -1.0;
float ect = -1.0;
float map_mbar = -1.0;
float vbat = -1.0;
int gear = -1;
float lambda1 = -1.0;

// -------------------- Function to create a new log file --------------------
void newLogFile() {
  do {
    snprintf(filename, sizeof(filename), "LOG_%03d.CSV", fileIndex++);
  } while (sd.exists(filename));

  if (!logfile.open(filename, O_WRONLY | O_CREAT | O_APPEND)) {
    Serial.println("FATAL: Log file creation failed!");
    digitalWrite(LED_PIN, HIGH); // Solid LED indicates fatal error
    while (1); // Halt execution
  }

  // Write the header for the new file
  logfile.println("Time(ms),RPM,TPS(%),Coolant(C),MAP(mBar),VBAT(V),Gear,Lambda1,CRC32");
  logfile.flush();
  Serial.print("Logging to new file: ");
  Serial.println(filename);
}

// -------------------- Setup Function --------------------
void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  delay(2000); // Wait for serial to connect
  Serial.println("--- MotoStudent Rugged CAN Logger ---");

  // Initialize CAN bus
  Can0.begin();
  Can0.setBaudRate(500000); // Set to 500 kbit/s as per AIM spec

  // Initialize SD card
  if (!sd.begin(CHIP_SELECT, SD_SCK_MHZ(50))) { // Use a fast clock speed
    Serial.println("FATAL: SD card initialization failed!");
    digitalWrite(LED_PIN, HIGH);
    while (1);
  }

  // Create the first log file
  newLogFile();

  // Initialize Watchdog Timer (4-second timeout)
  WDT_timings_t config;
  config.timeout = 4000;
  wdt.begin(config);

  // Pre-allocate memory for the string buffer for better performance
  logBuffer.reserve(4096);

  Serial.println("System initialized. Logging started.");
  lastMessageTime = millis();
}

// -------------------- Main Loop --------------------
void loop() {
  wdt.feed(); // Feed the watchdog to prevent a reset

  CAN_message_t msg;
  bool messageReceived = false;

  if (Can0.read(msg)) {
    messageReceived = true;
    lastMessageTime = millis(); // Reset the CAN timeout timer

    // Filter and decode only the CAN IDs we care about
    switch (msg.id) {
      case 0x5F0:
        rpm = (msg.buf[0] | (msg.buf[1] << 8));
        tps = (msg.buf[2] | (msg.buf[3] << 8)) / 650.0;
        break;
      case 0x5F2:
        ect = (((msg.buf[2] | (msg.buf[3] << 8)) / 19.0) - 450.0) / 10.0;
        break;
      case 0x5F3:
        map_mbar = (msg.buf[0] | (msg.buf[1] << 8)) / 10.0;
        break;
      case 0x5F4:
        vbat = ((msg.buf[2] | (msg.buf[3] << 8)) / 32.0) / 100.0;
        gear = (int16_t)(msg.buf[6] | (msg.buf[7] << 8));
        break;
      case 0x5F6:
        lambda1 = ((msg.buf[0] | (msg.buf[1] << 8)) / 2.0) / 1000.0;
        break;
    }
  }

  // Check for CAN communication timeout
  if (millis() - lastMessageTime > TIMEOUT_MS) {
    rpm = -1; tps = -1.0; ect = -1.0; map_mbar = -1.0; vbat = -1.0; gear = -1; lambda1 = -1.0;
    messageReceived = true; // Force a log entry to record the timeout
  }

  // If we have new data (either real or a timeout), log it
  if (messageReceived) {
    // 1. Construct the data line string (without CRC)
    String dataLine = String(millis()) + "," + String(rpm) + "," + String(tps, 2) + "," +
                      String(ect, 1) + "," + String(map_mbar, 1) + "," + String(vbat, 2) + "," +
                      String(gear) + "," + String(lambda1, 3);

    // 2. Compute CRC32 on the data line
    uint32_t crc = CRC32::calculate(dataLine.c_str(), dataLine.length());

    // 3. Append the CRC and add to the buffer
    logBuffer += dataLine + "," + String(crc) + "\n";
    lineCount++;

    // 4. Write buffer to SD card when full
    if (lineCount >= BUFFER_SIZE) {
      logfile.print(logBuffer);
      logfile.flush(); // Commit the data to the card
      logBuffer = "";  // Clear the buffer
      lineCount = 0;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // Blink LED on successful write
    }

    // 5. Check if we need to rotate to a new log file
    if (logfile.size() > FILE_SIZE_LIMIT && fileIndex < 999) {
      logfile.close();
      newLogFile();
    }
  }
}
