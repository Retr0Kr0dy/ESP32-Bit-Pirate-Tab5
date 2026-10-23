#pragma once

#include <stddef.h>
#include <string>
#include <vector>
#include <Enums/SubGhzProtocolEnum.h>

struct SubGhzFileCommand {
    uint32_t            frequency_hz {0};   // ex: 433920000
    uint16_t            te_us {0};          // pulse length 
    std::string         preset {};
    SubGhzProtocolEnum  protocol {SubGhzProtocolEnum::Unknown};

    // RcSwitch / Princeton
    uint16_t            bits {0};           // number of bits
    uint64_t            key {0};            // value already decoded

    // RAW
    std::vector<int32_t> raw_timings;       // us

    // BinRAW
    std::vector<uint8_t> bitstream_bytes;   // byte sequence

    std::string         source_file;
};