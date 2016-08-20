#include <RH_ASK.h>

#include <Util.h>
#include <Util/Assertion.hpp>
#include <Util/Constants.hpp>
#include <Util/Logging.hpp>

#include <Common/SensorMessage.hpp>

namespace Constants = Util::Constants;

using Util::Logging::log;
using Common::SensorMessage;

namespace {

// RadioHead breaks PWM on the following pins because of timer usage:
//
//    Board       Timer  Unusable PWM
// Arduino Uno   Timer1         9, 10
// Arduino Mega  Timer1        11, 12
const int TRANSMITTER_SPEED = 1000;
const int TRANSMITTER_RX_PIN = 8;
RH_ASK_RECEIVER RECEIVER(TRANSMITTER_SPEED, TRANSMITTER_RX_PIN);

char bitsToHex(uint8_t bits) {
    UTIL_ASSERT(bits <= 15);
    return bits < 10 ? '0' + bits : 'A' + bits - 10;
}

// FIXME: add checksum
void sendMessageToServer(const void* data, size_t size) {
    char buf[size * 2 + 1];
    size_t bufPos = 0;

    for(size_t byteId = 0; byteId < size; byteId++) {
        uint8_t value = reinterpret_cast<const uint8_t*>(data)[byteId];
        buf[bufPos++] = bitsToHex(value >> 4);
        buf[bufPos++] = bitsToHex(value & 0b1111);
    }
    buf[bufPos] = '\0';

    log(F("> "), buf);
}

}

void setup() {
    Util::Logging::init();
    if(!RECEIVER.init())
        UTIL_LOGICAL_ERROR(F("Failed to initialize the receiver."));

    log(F("Listening to messages from sensors..."));

    TimeMillis lastStatsTime = millis();
    TimeMillis lastHeartbeatTime = millis();

    uint8_t sensorMessageBuf[sizeof(SensorMessage) + 1];
    uint8_t messageSize;

    while(true) {
        messageSize = sizeof sensorMessageBuf;

        if(!RECEIVER.recv(sensorMessageBuf, &messageSize)) {
            TimeMillis curTime = millis();
            bool noMessagesTimeoutExceeded = curTime - lastHeartbeatTime >= Constants::MINUTE_MILLIS;

            if(noMessagesTimeoutExceeded) {
                log(F("I'm alive but there are no messages from sensors."));
                lastHeartbeatTime = curTime;
            }

            if(noMessagesTimeoutExceeded || curTime - lastStatsTime >= Constants::MINUTE_MILLIS) {
                log(F("Received messages statistics: "), RECEIVER.rxGood(), F(" good, "), RECEIVER.rxBad(), F(" bad."));
                lastStatsTime = curTime;
            }

            continue;
        }

        lastHeartbeatTime = millis();

        if(messageSize != sizeof(SensorMessage)) {
            log(F("Got a message with invalid size."));
            continue;
        }

        SensorMessage* message = reinterpret_cast<SensorMessage*>(sensorMessageBuf);
        log(F("Got a message from #"), message->sensorId, F(" sensor."));
        message->co2Concentration = htons(message->co2Concentration);
        sendMessageToServer(message, sizeof(SensorMessage));
    }
}

void loop() {
    UTIL_LOGICAL_ERROR();
}
