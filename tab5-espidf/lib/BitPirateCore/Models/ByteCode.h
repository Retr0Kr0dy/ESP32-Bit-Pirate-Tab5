#pragma once

#include "Enums/ByteCodeEnum.h"
#include <cstdint>

class ByteCode {
public:
    ByteCode(ByteCodeEnum command = ByteCodeEnum::None, uint32_t data = 0)
        : command(command), data(data), bits(8), repeat(1), hasBits(false), hasRepeat(false) {}

    ByteCode(ByteCodeEnum command, uint32_t data, uint8_t bits, uint32_t repeat)
        : command(command), data(data), bits(bits), repeat(repeat), hasBits(true), hasRepeat(true) {}


    ByteCodeEnum getCommand() const { return command; }
    void setCommand(ByteCodeEnum cmd) { command = cmd; }

    uint32_t getData() const { return data; }
    void setData(uint32_t value) { data = value; }

    uint8_t getBits() const { return bits; }
    void setBits(uint8_t b) { bits = b; }

    uint32_t getRepeat() const { return repeat; }
    void setRepeat(uint32_t r) { repeat = r; }

    bool hasBitsSet() const { return hasBits; }
    void setHasBits(bool flag) { hasBits = flag; }

    bool hasRepeatSet() const { return hasRepeat; }
    void setHasRepeat(bool flag) { hasRepeat = flag; }

private:
    ByteCodeEnum command;
    uint32_t data;
    uint8_t bits;
    uint32_t repeat;
    bool hasBits;
    bool hasRepeat;
};
