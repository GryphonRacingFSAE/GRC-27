#include <TaskFactory.h>
#include <Arduino.h>
#include <Imu/ImuTask.h>

// static QueueHandle_t dataQueueHandle;

// static TaskHandle_t dataTaskHandle = NULL;

// static DataAcqTaskParameters dataParams;

void createTasks() {
    // 1. Object
    // dataQueueHandle = xQueueCreate(10, sizeof(int)); // Update sizeof() later

    // 2. Params
    // dataParams.dataQueue = &dataQueueHandle;

    // 3. Tasks
    // xTaskCreate(DataAcqTask, "DataAcqTask", 4096, (void*)&dataParams, 1, &dataTaskHandle);

    // IMU: a missing sensor must not stop the other tasks being created.
    if (!imuTaskStart()) {
        Serial.println("ERROR: IMU task could not be created");
    }
}