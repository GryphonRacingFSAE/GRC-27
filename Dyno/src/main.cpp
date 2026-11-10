#include <Arduino.h>
#include <TaskFactory.h>
#include <pindefs.h>

void setup() {
  Serial.begin(115200);

  // Startup diagnostics use the "# " prefix so CSV parsers skip them.
#if !DYNO_PINS_CONFIRMED
  Serial.print("# WARNING: pin assignments are unconfirmed placeholders (see include/Defines/pindefs.h)\r\n");
#endif

  createTasks();
}

void loop() {
  vTaskDelete(NULL); // kill arduino loop, using RTOS scheduler
}
