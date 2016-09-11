#include <Util/Assertion.hpp>
#include <Util/Constants.hpp>
#include <Util/Core.hpp>
#include <Util/Logging.hpp>

#include "Indication.hpp"

namespace Constants = Util::Constants;

using Util::Logging::log;

ShiftRegisterLeds::ShiftRegisterLeds(uint8_t dataPin, uint8_t clockPin, uint8_t latchPin)
: dataPin_(dataPin), clockPin_(clockPin), latchPin_(latchPin), value_(0) {
    pinMode(dataPin, OUTPUT);
    pinMode(clockPin, OUTPUT);
    pinMode(latchPin, OUTPUT);
    this->set(value_);
}

void ShiftRegisterLeds::set(LedsValue leds) {
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        int curByte = sizeof leds - 1, lastByte = 0, incByte = -1;
    #else
        int curByte = 0, lastByte = sizeof leds - 1, incByte = 1;
    #endif
#else
    #error Unable to determine platform endianness
#endif

    auto bytes = reinterpret_cast<const byte*>(&leds);

    while(true) {
        shiftOut(dataPin_, clockPin_, MSBFIRST, bytes[curByte]);
        if(curByte == lastByte)
            break;
        curByte += incByte;
    }

    digitalWrite(latchPin_, HIGH);
    digitalWrite(latchPin_, LOW);

    value_ = leds;
}

void ShiftRegisterLeds::update(LedsValue leds, LedsValue mask) {
    auto valueWithoutGroup = value_ & ~mask;
    this->set(valueWithoutGroup | leds);
}


LedGroup::LedGroup(ShiftRegisterLeds* leds, uint8_t startBit, uint8_t ledsNum)
: ledsNum(ledsNum), leds_(leds), startBit_(startBit), mask_(0) {
    UTIL_ASSERT(startBit_ + ledsNum <= ShiftRegisterLeds::MAX_LEDS_NUM);
    for(uint8_t ledId = 0; ledId < ledsNum; ledId++)
        mask_ |= LedsValue(1) << (startBit_ + ledId);
}

void LedGroup::setLed(uint8_t ledNum) {
    UTIL_ASSERT(ledNum <= this->ledsNum);

    LedsValue value = ledNum
        ? LedsValue(1) << (startBit_ + ledNum - 1)
        : 0;

    leds_->update(value, mask_);
}


LedProgressTask::LedProgressTask(LedGroup* ledGroup)
: ledGroup_(ledGroup), curLedNum_(0) {
}

void LedProgressTask::execute() {
    curLedNum_ = curLedNum_ >= ledGroup_->ledsNum ? 0 : curLedNum_ + 1;
    ledGroup_->setLed(curLedNum_);
    this->scheduleAfter(100);
}

void LedProgressTask::pause() {
    Task::pause();
    curLedNum_ = 0;
}


LedBrightnessRegulator::LedBrightnessRegulator(
    uint8_t lightSensorPin, const uint8_t* transistorBasePins, uint8_t transistorBasePinsNum,
    Util::TaskScheduler* scheduler
): lightSensorPin_(lightSensorPin), transistorBasePins_(transistorBasePins),
   transistorBasePinsNum_(transistorBasePinsNum), pwmValue_(1) {
    #if UTIL_ENABLE_LOGGING
        lastLogTime_ = 0;
    #endif

    pinMode(lightSensorPin_, INPUT);

    for(uint8_t pinId = 0; pinId < transistorBasePinsNum_; pinId++) {
        auto pin = transistorBasePins_[pinId];
        pinMode(pin, OUTPUT);
        analogWrite(pin, pwmValue_);
    }

    scheduler->addTask(this);
}

void LedBrightnessRegulator::execute() {
    uint16_t brightness = Constants::ANALOG_HIGH - analogRead(lightSensorPin_);

    #if UTIL_ENABLE_LOGGING
        auto curTime = millis();
        if(curTime - lastLogTime_ >= 5 * Constants::SECOND_MILLIS) {
            log(F("Brightness: "), brightness, F("."));
            lastLogTime_ = curTime;
        }
    #endif

    // e-Exponential regression calculated by http://keisan.casio.com/exec/system/14059930754231
    // using the following data (determined experimentally):
    // 700 5
    // 800 10
    // 1000 100
    double A = 0.705156848;
    double B = 0.00396581331;
    double x = brightness;
    double y = A * pow(M_E, B * x);
    pwmValues_.add(constrain(y, Constants::PWM_LOW + 1, Constants::PWM_HIGH));

    if(pwmValues_.full()) {
        uint8_t pwmValue = pwmValues_.maxValue();

        if(pwmValue != pwmValue_) {
            pwmValue_ = pwmValue;
            for(uint8_t pinId = 0; pinId < transistorBasePinsNum_; pinId++)
                analogWrite(transistorBasePins_[pinId], pwmValue);
        }
    }

    this->incrementScheduledTime(50);
}
