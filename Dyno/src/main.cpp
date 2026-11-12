#include <API.h>
#include <Arduino.h>
#include <TaskFactory.h>

void setup() {
  Serial.begin(115200);

  printDiagnosticWarnings();

  createTasks();
}

void loop() {
  vTaskDelete(NULL); // kill arduino loop, using RTOS scheduler
}
