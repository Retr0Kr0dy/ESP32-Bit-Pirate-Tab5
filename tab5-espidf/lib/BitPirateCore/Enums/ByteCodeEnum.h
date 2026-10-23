#pragma once
#include <string>

enum class ByteCodeEnum {
    Write,
    Read,
    Start,
    Stop,
    DelayUs,
    DelayMs,
    SetClkHigh,
    SetClkLow,
    SetDatHigh,
    SetDatLow,
    AuxHigh,
    AuxLow,
    AuxInput,
    Adc,
    ReadDat,
    TickClock,
    StartAlt,
    StopAlt,
    None
};

class ByteCodeEnumMapper {
public:
    static std::string toString(ByteCodeEnum code) {
        switch (code) {
            case ByteCodeEnum::Write:        return "Write     ";
            case ByteCodeEnum::Read:         return "Read      ";
            case ByteCodeEnum::Start:        return "Start     ";
            case ByteCodeEnum::Stop:         return "Stop      ";
            case ByteCodeEnum::DelayUs:      return "DelayUs   ";
            case ByteCodeEnum::DelayMs:      return "DelayMs   ";
            case ByteCodeEnum::SetClkHigh:   return "SetClkHigh";
            case ByteCodeEnum::SetClkLow:    return "SetClkLow ";
            case ByteCodeEnum::SetDatHigh:   return "SetDatHigh";
            case ByteCodeEnum::SetDatLow:    return "SetDatLow ";
            case ByteCodeEnum::AuxHigh:      return "AuxHigh   ";
            case ByteCodeEnum::AuxLow:       return "AuxLow    ";
            case ByteCodeEnum::AuxInput:     return "AuxInput  ";
            case ByteCodeEnum::Adc:          return "Adc       ";
            case ByteCodeEnum::ReadDat:      return "ReadDat   ";
            case ByteCodeEnum::TickClock:    return "TickClock ";
            case ByteCodeEnum::StartAlt:     return "StartAlt  ";
            case ByteCodeEnum::StopAlt:      return "StopAlt   ";
            case ByteCodeEnum::None:         return "None      ";
            default:                         return "Unknown   ";
        }
    }
};