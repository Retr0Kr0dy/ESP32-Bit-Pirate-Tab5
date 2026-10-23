#include "FmService.h"
#include <Wire.h>

FmService::~FmService() {
    if (radio_) {
        delete radio_;
        radio_ = nullptr;
    }
}

bool FmService::configure(int8_t resetPin,
                          int8_t sdaPin,
                          int8_t sclPin,
                          uint32_t i2cFreqHz)
{
    Wire.end();
    delay(10);
    Wire.begin(sdaPin, sclPin, i2cFreqHz);

    if (radio_) {
        delete radio_;
        radio_ = nullptr;
    }

    radio_ = new Adafruit_Si4713(resetPin);
    if (!radio_) return false;

    isInitialized_ = true;
    isRunning_ = false;
    frequency10kHz_ = 0;
    txPower_ = 0;
    isRdsEnabled_ = false;
    isTaEnabled_ = false;

    return true;
}

bool FmService::begin() {
    if (!isInitialized_ || !radio_) return false;

    if (!radio_->begin()) {
        isRunning_ = false;
        return false;
    }
    return true;
}

void FmService::stop() {
    if (!isInitialized_ || !radio_) return;

    radio_->setTXpower(0);
    txPower_ = 0;

    // Reset soft
    radio_->reset();

    isRunning_ = false;
}

void FmService::reset(uint8_t pin) {
    // Reset state
    isRunning_ = false;
    frequency10kHz_ = 0;
    txPower_ = 0;
    isRdsEnabled_ = false;
    isTaEnabled_ = false;

    // Reset hardware via pin
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    delay(10);
    digitalWrite(pin, LOW);
    delay(10);
    digitalWrite(pin, HIGH);
}

bool FmService::setTxPower(uint8_t dbuV, uint8_t antCap) {
    if (!isInitialized_ || !radio_) return false;
    radio_->setTXpower(dbuV, antCap);
    txPower_ = dbuV;
    isRunning_ = (dbuV > 0);
    return true;
}

bool FmService::tune(uint16_t freq10kHz) {
    if (!isInitialized_ || !radio_) return false;
    radio_->tuneFM(freq10kHz);
    frequency10kHz_ = freq10kHz;
    return true;
}

bool FmService::beginRds() {
    if (!isInitialized_ || !radio_) return false;
    radio_->beginRDS();
    isRdsEnabled_ = true;
    return true;
}

bool FmService::setRdsStation(const char* ps8) {
    if (!isInitialized_ || !radio_) return false;
    if (!isRdsEnabled_) {
        if (!beginRds()) return false;
    }
    radio_->setRDSstation(ps8);
    return true;
}

bool FmService::setRdsText(const char* radiotext) {
    if (!isInitialized_ || !radio_) return false;
    if (!isRdsEnabled_) {
        if (!beginRds()) return false;
    }
    radio_->setRDSbuffer(radiotext);
    return true;
}

bool FmService::setTrafficAnnouncement(bool enabled) {
    if (!isInitialized_ || !radio_) return false;

    // SI4713_PROP_TX_RDS_PS_MISC : TA bit
    // 0x1018 (TA on) ; 0x1008 (TA off)
    const uint16_t val = enabled ? 0x1018 : 0x1008;
    radio_->setProperty(SI4713_PROP_TX_RDS_PS_MISC, val);
    isTaEnabled_ = enabled;
    return true;
}

bool FmService::measureAt(uint16_t freq10kHz, uint8_t& noiseLevel) {
    if (!isInitialized_ || !radio_) return false;

    radio_->readTuneMeasure(freq10kHz);
    radio_->readTuneStatus();

    noiseLevel = radio_->currNoiseLevel;
    return true;
}

uint16_t FmService::scanBestFrequency(uint16_t start10kHz,
                                      uint16_t end10kHz,
                                      uint16_t step10kHz) {
    if (!isInitialized_ || !radio_) return 0;
    if (start10kHz >= end10kHz || step10kHz == 0) return 0;

    uint8_t noise = 0;
    uint8_t minNoise = 255;
    uint16_t best = start10kHz;

    for (uint16_t f = start10kHz; f < end10kHz; f = (uint16_t)(f + step10kHz)) {
        if (!measureAt(f, noise)) continue;
        if (noise < minNoise) {
            minNoise = noise;
            best = f;
        }
    }

    return best;
}

size_t FmService::sweepActivity(std::vector<uint16_t>& freqs,
                                std::vector<uint8_t>& levels,
                                uint16_t start10kHz,
                                uint16_t end10kHz,
                                uint16_t step10kHz,
                                uint8_t samplesPerFreq,
                                uint16_t settleMs)
{
    freqs.clear();
    levels.clear();

    if (!isInitialized_ || !radio_) return 0;
    if (start10kHz >= end10kHz || step10kHz == 0) return 0;
    if (samplesPerFreq == 0) samplesPerFreq = 1;

    size_t count = (end10kHz - start10kHz) / step10kHz;
    freqs.reserve(count + 1);
    levels.reserve(count + 1);

    for (uint16_t f = start10kHz; f < end10kHz; f += step10kHz) {
        uint32_t acc = 0;

        for (uint8_t s = 0; s < samplesPerFreq; ++s) {
            radio_->readTuneMeasure(f);
            radio_->readTuneStatus();
            acc += radio_->currNoiseLevel;

            if (settleMs) delay(settleMs);
        }

        uint8_t avg = acc / samplesPerFreq;

        freqs.push_back(f);
        levels.push_back(avg);
    }

    return freqs.size();
}