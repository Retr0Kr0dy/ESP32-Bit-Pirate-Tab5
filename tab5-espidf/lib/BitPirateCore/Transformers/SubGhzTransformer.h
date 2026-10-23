#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <cctype>
#include "driver/rmt_types.h"

#include "Enums/SubGhzProtocolEnum.h"
#include "Models/SubghzFileCommand.h"

class SubGhzTransformer {
public:

    // Validate sub file
    bool isValidSubGhzFile(const std::string& fileContent);

    // Transform from .sub file content to commands
    std::vector<SubGhzFileCommand> transformFromFileFormat(const std::string& fileContent, const std::string& sourcePath = {});

    // Transform from commands to .sub file content
    std::string transformToFileFormat(const SubGhzFileCommand& cmd);

    // Extract readable summaries of commands
    std::vector<std::string> extractSummaries(const std::vector<SubGhzFileCommand>& cmds);

    // Convert RMT symbols to signed timings (for RAW saving)
    std::vector<int32_t> symbolsToSignedTimings(const std::vector<rmt_symbol_word_t>& items, uint32_t rx_tick_per_us) const;

    // Repeat a frame with a gap, for better reception on some protocols
    std::vector<rmt_symbol_word_t> repeatFrameWithGap(
        const std::vector<rmt_symbol_word_t>& frame,
        uint32_t rx_tick_per_us,
        int repeatCount = 3,
        uint32_t gap_us = 10'000,
        size_t maxFrameSymbolsForRepeat = 256
    ) const;
private:
    // Helpers
    static void trim(std::string& s);
    static std::string trim_copy(const std::string& s);
    static bool startsWith(const std::string& s, const char* prefix);
    static bool iequals(const std::string& a, const std::string& b);

    static bool parseKeyValueLine(const std::string& line, std::string& keyOut, std::string& valOut);
    static bool parseUint32(const std::string& s, uint32_t& out);
    static bool parseUint16(const std::string& s, uint16_t& out);
    static bool parseInt(const std::string& s, int& out);
    static bool parseHexToU64(const std::string& hex, uint64_t& out);
    std::string mapPreset(const std::string& presetStr);

    // RAW: “123 456 789 …”
    std::vector<int32_t> parseRawTimingsJoined(const std::vector<std::string>& rawLines);

    // BinRAW: "00 01 02 FF" → octets
    std::vector<uint8_t> parseHexByteStream(const std::string& hexLine);

    // Finalizer
    void flushAccumulated(
        const std::string& protocolStr,
        const std::string& presetStr,
        uint32_t frequency,
        uint16_t te,
        const std::vector<int>& bitList,
        const std::vector<int>& bitRawList,
        const std::vector<uint64_t>& keyList,
        const std::vector<std::string>& rawDataLines,
        const std::vector<uint8_t>&     binRawBytes,
        std::vector<SubGhzFileCommand>& out,
        const std::string& sourcePath
    );

    SubGhzProtocolEnum detectProtocol(const std::string& protocolStr);
};
