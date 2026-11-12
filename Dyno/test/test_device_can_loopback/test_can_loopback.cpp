// ON-DEVICE test (#33): the REAL TwaiCanBus adapter + decoder + receiver on
// an ESP32-S3, with no ECU and no transceiver.
//
// Setup: bare ESP32-S3 devkit, ONE jumper wire from PIN_CAN_TX to PIN_CAN_RX
// (the GPIO numbers in include/Defines/pindefs.h). Nothing else connected.
// Run:   pio test -e esp32-s3-devboard -f test_device_can_loopback
// Build only (no hardware): add --without-uploading --without-testing
//
// The controller runs in self-test mode (NO_ACK) and receives its own frames
// through the wire. This proves the driver install/start, bit timing, the
// twai_message_t -> CanFrame translation (ID, extended/remote flags, length,
// bytes) and the bounded receive wait. It does NOT prove the transceiver,
// bus wiring, termination or the ECU: see docs/can-decoding.md.
#include <Arduino.h>
#include <unity.h>

#include <CanReceiver.h>
#include <DynoConfig.h>
#include <EcuDecoder.h>
#include <TwaiCanBus.h>
#include <pindefs.h>

static TwaiCanBus bus(PIN_CAN_TX, PIN_CAN_RX, CAN_BITRATE_KBPS, CAN_RX_QUEUE_LEN, TWAI_MODE_NO_ACK);

void setUp() {}
void tearDown() {}

// Send to ourselves and take it back out through the adapter.
static CanFrame sendToSelf(uint32_t id, bool extended, bool remote, uint8_t dlc, const uint8_t* data) {
    TEST_ASSERT_TRUE_MESSAGE(bus.transmitToSelf(id, extended, remote, dlc, data),
                             "transmit failed: is the TX->RX jumper fitted?");
    CanFrame f;
    TEST_ASSERT_EQUAL_MESSAGE(CanReceiveResult::Frame, bus.receive(f, 100),
                              "own frame not received: check the TX->RX jumper and pins");
    return f;
}

static void test_driver_installs_and_starts() {
    const bool started = bus.start();
    char msg[64];
    snprintf(msg, sizeof(msg), "twai_driver_install/twai_start failed: esp_err_t 0x%X", bus.lastError());
    TEST_ASSERT_TRUE_MESSAGE(started, msg);
    TEST_ASSERT_EQUAL(CanBusState::Running, bus.state());
}

// Issue #33 reference frame through the real driver: 6000 rpm, lambda 1.000.
static void test_reference_frame_through_real_driver() {
    const uint8_t data[8] = {0x70, 0x17, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x03};
    const uint32_t before = millis();
    const CanFrame f = sendToSelf(0x520, false, false, 8, data);

    TEST_ASSERT_EQUAL_HEX32(0x520, f.id);
    TEST_ASSERT_FALSE(f.extended);
    TEST_ASSERT_FALSE(f.remote);
    TEST_ASSERT_EQUAL_UINT8(8, f.dlc);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, f.data, 8);
    TEST_ASSERT_UINT32_WITHIN(100, before, f.rx_ms);

    EcuState state;
    TEST_ASSERT_EQUAL(DecodeResult::Accepted, decodeEcuFrame(f, state));
    TEST_ASSERT_EQUAL_INT32(6000, state.raw(EcuSignal::EngineSpeed));
    TEST_ASSERT_EQUAL_INT32(1000, state.raw(EcuSignal::LambdaAverage));
}

static void test_unsigned_and_signed_through_real_driver() {
    const uint8_t rpm[8] = {0x60, 0xEA, 0, 0, 0, 0, 0, 0};
    const uint8_t target[8] = {0, 0, 0, 0, 0, 0, 0x18, 0xFC};
    EcuState state;
    TEST_ASSERT_EQUAL(DecodeResult::Accepted, decodeEcuFrame(sendToSelf(0x520, false, false, 8, rpm), state));
    TEST_ASSERT_EQUAL(DecodeResult::Accepted, decodeEcuFrame(sendToSelf(0x527, false, false, 8, target), state));
    TEST_ASSERT_EQUAL_INT32(60000, state.raw(EcuSignal::EngineSpeed));
    TEST_ASSERT_EQUAL_INT32(-1000, state.raw(EcuSignal::LambdaTarget));
}

// The driver's flag bits must reach CanFrame, or the decoder can't reject.
static void test_extended_remote_and_short_frames_keep_their_flags() {
    const uint8_t data[8] = {0x60, 0xEA, 0, 0, 0, 0, 0, 0};
    EcuState state;

    const CanFrame ext = sendToSelf(0x520, true, false, 8, data);
    TEST_ASSERT_TRUE(ext.extended);
    TEST_ASSERT_EQUAL(DecodeResult::Extended, decodeEcuFrame(ext, state));

    const CanFrame rtr = sendToSelf(0x520, false, true, 8, nullptr);
    TEST_ASSERT_TRUE(rtr.remote);
    TEST_ASSERT_EQUAL(DecodeResult::Remote, decodeEcuFrame(rtr, state));

    const CanFrame shrt = sendToSelf(0x520, false, false, 4, data);
    TEST_ASSERT_EQUAL_UINT8(4, shrt.dlc);
    TEST_ASSERT_EQUAL(DecodeResult::BadLength, decodeEcuFrame(shrt, state));

    TEST_ASSERT_EQUAL_HEX8(0x00, state.receivedMask());
}

// With nothing sent, receive() returns Timeout after about the asked wait.
static void test_receive_wait_is_bounded() {
    CanFrame f;
    const uint32_t t0 = millis();
    TEST_ASSERT_EQUAL(CanReceiveResult::Timeout, bus.receive(f, CAN_RX_TIMEOUT_MS));
    const uint32_t waited = millis() - t0;
    TEST_ASSERT_UINT32_WITHIN(5, CAN_RX_TIMEOUT_MS, waited);
}

// All 7 messages, end to end through the real CanReceiver on the real driver.
static void test_receiver_end_to_end_all_messages() {
    CanReceiver rx(bus, {CAN_RX_TIMEOUT_MS, CAN_RESTART_INTERVAL_MS});
    const uint8_t zeros[8] = {};
    for (const EcuMessageDef& msg : kEcuMessages) {
        TEST_ASSERT_TRUE(bus.transmitToSelf(msg.id, false, false, 8, zeros));
        delay(2);
    }
    for (int i = 0; i < 20 && rx.ecu().receivedMask() != 0x7F; ++i) {
        rx.step(millis());
    }
    TEST_ASSERT_EQUAL_HEX8(0x7F, rx.ecu().receivedMask());
    TEST_ASSERT_EQUAL_UINT32(7, rx.status().frames_decoded);
    TEST_ASSERT_EQUAL_UINT32(0, rx.status().rx_errors);
    TEST_ASSERT_EQUAL_UINT32(0, rx.status().rx_missed);
    TEST_ASSERT_EQUAL_UINT32(0, rx.status().bus_errors); // clean wire, right timing
    TEST_ASSERT_FALSE(rx.status().error_passive);
}

void setup() {
    delay(2000); // let USB serial connect
    UNITY_BEGIN();
    RUN_TEST(test_driver_installs_and_starts);
    RUN_TEST(test_reference_frame_through_real_driver);
    RUN_TEST(test_unsigned_and_signed_through_real_driver);
    RUN_TEST(test_extended_remote_and_short_frames_keep_their_flags);
    RUN_TEST(test_receive_wait_is_bounded);
    RUN_TEST(test_receiver_end_to_end_all_messages);
    UNITY_END();
}

void loop() {
    vTaskDelete(NULL); // tests are done; don't spin (same as src/main.cpp)
}
