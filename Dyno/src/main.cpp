#include <API.h>
#include <Arduino.h>
#include <DynoConfig.h>
#include <TaskFactory.h>

void setup() {
  // Room for a whole line in the USB serial transmit buffer (default 256 bytes;
  // the CSV header is ~420). Must be set before begin().
  Serial.setTxBufferSize(1024);
  Serial.begin(DYNO_SERIAL_BAUD);

  // Creates the tasks. From then on OutputTask is the only serial writer:
  // CsvTask queues the CSV header first, then the startup warnings.
  createTasks();
}

void loop() {
  vTaskDelete(NULL); // kill arduino loop, using RTOS scheduler
}
