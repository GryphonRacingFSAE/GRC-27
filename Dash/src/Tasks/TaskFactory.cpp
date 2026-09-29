#include <TaskFactory.h>
#include <Arduino.h>

// Static Handles (Private)
// static SemaphoreHandle_t guiMutexHandle;

// static TaskHandle_t guiTaskHandle = NULL;

// Static Parameters
// static GuiTaskParameters      guiParams;

void createTasks() {
    Serial.println("[Factory] Creating Tasks...");
    // 1. Objects
    // guiMutexHandle = xSemaphoreCreateMutex();

    // 2. Params
    // guiParams.guiMutex = &guiMutexHandle;

    // 3. Tasks
    // GUI on Core 1 (App Core) is best for Rendering
    // xTaskCreatePinnedToCore(GuiTask, "GUI", 8192, (void*)&guiParams, 2, &guiTaskHandle, 1);

    // Logic on Core 0 or 1
    // xTaskCreate(DataAcqTask, "Data", 4096, (void*)&dataParams, 1, &dataTaskHandle);
    // xTaskCreate(PeripheralTask, "Periph", 2048, (void*)&periphParams, 1, &periphTaskHandle);
}