// Receives and decodes the ECU's CAN frames (or the fake ECU, with
// DYNO_FAKE_INPUTS) and publishes the latest EcuState and CanStatus into
// 1-slot mailboxes. See docs/can-decoding.md.
#pragma once

#include <Arduino.h>
#include <CanReceiver.h>

struct CanTaskParams {
    QueueHandle_t ecuMailbox;    // holds 1 EcuState; read it with xQueuePeek
    QueueHandle_t statusMailbox; // holds 1 CanStatus (link state + counters)
};

void CanTask(void* pvParameters);
