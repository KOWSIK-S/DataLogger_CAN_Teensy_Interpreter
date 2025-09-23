#include <FlexCAN_T4.h>
#include <SD.h>
#include <SPI.h>

// CAN on Teensy 4.1
FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> Can0;
File logfile;

// --- BUFFERING VARIABLES ---
String logBuffer = "";
const int BUFFER_SIZE = 50;
int lineCount = 0;

// --- ERROR HANDLING VARIABLES ---
uint32_t lastMessageTime = 0; // Tracks the time of the last received message
const uint32_t TIMEOUT_MS = 250; // Timeout period in milliseconds

// Global variables for all decoded values
int rpm = 0;
float tps = 0.0;
float ect = 0.0;
float map_mbar = 0.0;
float vbat = 0.0;
int gear = 0;
float lambda1 = 0.0;

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  Can0.begin();
  Can0.setBaudRate(500000);

  if (!SD.begin(BUILTIN_SDCARD)) {
    Serial.println("SD init failed!"); while(1);
  }

  logfile = SD.open("rugged_log.csv", FILE_WRITE);
  if (!logfile) {
    Serial.println("Log file creation failed!"); while(1);
  }

  logBuffer.reserve(4096); 
  logfile.println("Time(ms),RPM,TPS(%),Coolant(C),MAP(mBar),VBAT(V),Gear,Lambda1");
  logfile.flush();

  Serial.println("Ruggedized logging started...");
  lastMessageTime = millis(); // Initialize the timer
}

void loop() {
  CAN_message_t msg;
  bool messageReceived = false;

  if (Can0.read(msg)) {
    messageReceived = true;
    lastMessageTime = millis(); // Reset timer on any valid message

    // This switch statement inherently acts as our filter.
    // It only processes the CAN IDs we care about and ignores all others.
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

  // --- ERROR HANDLING: Check for timeout ---
  if (millis() - lastMessageTime > TIMEOUT_MS) {
    // If no message is received for 250ms, assume connection is lost.
    // Log invalid data (-1) to make the error visible in the data.
    rpm = -1; tps = -1; ect = -1; map_mbar = -1; vbat = -1; gear = -1; lambda1 = -1;
    messageReceived = true; // Force a log entry to show the error
  }
  
  // --- BUFFERING LOGIC ---
  if (messageReceived) {
    String dataLine = String(millis()) + "," + String(rpm) + "," + String(tps, 2) + "," + 
                      String(ect, 1) + "," + String(map_mbar, 1) + "," + String(vbat, 2) + "," + 
                      String(gear) + "," + String(lambda1, 3);

    logBuffer += dataLine + "\n";
    lineCount++;

    if (lineCount >= BUFFER_SIZE) {
      logfile.print(logBuffer);
      logfile.flush();
      logBuffer = "";
      lineCount = 0;
    }
  }
}
