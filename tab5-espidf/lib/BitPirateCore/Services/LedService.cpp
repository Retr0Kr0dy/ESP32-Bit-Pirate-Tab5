#include "LedService.h"
#include <FastLED.h>
#include <Enums/LedProtocolEnum.h>
#include <Enums/LedChipsetEnum.h>

extern CFastLED FastLED; // declare FastLED as global

LedService::LedService() {}

void LedService::configure(uint8_t dataPin, uint8_t clockPin, uint16_t length,
                           const std::string& protocol, uint8_t brightness) {
    if (length > MAX_LEDS) {
        length = MAX_LEDS;
    }

    // Clear full
    if (leds) {
        FastLED.clear(true);
        delay(20);
    } else {
        // Allocate one time
        leds = new CRGB[MAX_LEDS]();
    }

    // Reset FastLED 
    FastLED = CFastLED();

    ledCount = length;

    // Clean leds buffer
    memset(leds, 0, sizeof(CRGB) * MAX_LEDS);
    FastLED.clear();
    FastLED.clearData();

    /*
    Fastled need defined pins and protocol at compile time, 
    cant find another way to do it than using switch for each one 
    This can't compile on Windows, define this only if you build on Linux:
    -D ENABLE_FASTLED_PROTOCOL_SWITCHES
    */

    // --- Only DATA Pin ---

    #ifdef ENABLE_FASTLED_PROTOCOL_SWITCHES

    auto proto = LedProtocolEnumMapper::fromString(protocol);
    if (proto != LedProtocolEnum::UNKNOWN) {
        switch (proto) {
            case LedProtocolEnum::NEOPIXEL:
                FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(leds, MAX_LEDS); break;
            case LedProtocolEnum::WS2812:
                FastLED.addLeds<WS2812, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::WS2812B:
                FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::WS2811:
                FastLED.addLeds<WS2811, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::WS2811_400:
                FastLED.addLeds<WS2811_400, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::WS2813:
                FastLED.addLeds<WS2813, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::WS2815:
                FastLED.addLeds<WS2815, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::WS2816:
                FastLED.addLeds<WS2816, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::GS1903:
                FastLED.addLeds<GS1903, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::SK6812:
                FastLED.addLeds<SK6812, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::SK6822:
                FastLED.addLeds<SK6822, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::APA104:
                FastLED.addLeds<APA104, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::APA106:
                FastLED.addLeds<APA106, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::PL9823:
                FastLED.addLeds<PL9823, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::GE8822:
                FastLED.addLeds<GE8822, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::GW6205:
                FastLED.addLeds<GW6205, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::GW6205_400:
                FastLED.addLeds<GW6205_400, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::LPD1886:
                FastLED.addLeds<LPD1886, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::LPD1886_8BIT:
                FastLED.addLeds<LPD1886_8BIT, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::SM16703:
                FastLED.addLeds<SM16703, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::TM1829:
                FastLED.addLeds<TM1829, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::TM1812:
                FastLED.addLeds<TM1812, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::TM1809:
                FastLED.addLeds<TM1809, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::TM1804:
                FastLED.addLeds<TM1804, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::TM1803:
                FastLED.addLeds<TM1803, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::UCS1903:
                FastLED.addLeds<UCS1903, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::UCS1903B:
                FastLED.addLeds<UCS1903B, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::UCS1904:
                FastLED.addLeds<UCS1904, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::UCS2903:
                FastLED.addLeds<UCS2903, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            case LedProtocolEnum::UCS1912:
                FastLED.addLeds<UCS1912, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
            default:
                delete[] leds;
                leds = nullptr;
                return;
        }
        usesClock = false;
        FastLED.setBrightness(brightness);
        FastLED.show();
        return;
    }

    // --- Chipsets DATA + CLOCK ---

    auto chipset = LedChipsetMapper::fromString(protocol);
    switch (chipset) {
        case LPD6803:
            FastLED.addLeds<LPD6803, LED_DATA_PIN, LED_CLOCK_PIN>(leds, MAX_LEDS); break;
        case LPD8806:
            FastLED.addLeds<LPD8806, LED_DATA_PIN, LED_CLOCK_PIN>(leds, MAX_LEDS); break;
        case WS2801:
            FastLED.addLeds<WS2801, LED_DATA_PIN, LED_CLOCK_PIN>(leds, MAX_LEDS); break;
        case WS2803:
            FastLED.addLeds<WS2803, LED_DATA_PIN, LED_CLOCK_PIN>(leds, MAX_LEDS); break;
        case SM16716:
            FastLED.addLeds<SM16716, LED_DATA_PIN, LED_CLOCK_PIN>(leds, MAX_LEDS); break;
        case P9813:
            FastLED.addLeds<P9813, LED_DATA_PIN, LED_CLOCK_PIN>(leds, MAX_LEDS); break;
        case APA102:
            FastLED.addLeds<APA102, LED_DATA_PIN, LED_CLOCK_PIN, BGR>(leds, MAX_LEDS); break;
        case APA102HD:
            FastLED.addLeds<APA102, LED_DATA_PIN, LED_CLOCK_PIN, BGR>(leds, MAX_LEDS); break;
        case DOTSTAR:
            FastLED.addLeds<DOTSTAR, LED_DATA_PIN, LED_CLOCK_PIN, BGR>(leds, MAX_LEDS); break;
        case DOTSTARHD:
            FastLED.addLeds<DOTSTARHD, LED_DATA_PIN, LED_CLOCK_PIN, BGR>(leds, MAX_LEDS); break;
        case SK9822:
            FastLED.addLeds<SK9822, LED_DATA_PIN, LED_CLOCK_PIN, BGR>(leds, MAX_LEDS); break;
        case SK9822HD:
            FastLED.addLeds<SK9822HD, LED_DATA_PIN, LED_CLOCK_PIN, BGR>(leds, MAX_LEDS); break;
        case HD107:
            FastLED.addLeds<HD107, LED_DATA_PIN, LED_CLOCK_PIN, BGR>(leds, MAX_LEDS); break;
        case HD107HD:
            FastLED.addLeds<HD107HD, LED_DATA_PIN, LED_CLOCK_PIN, BGR>(leds, MAX_LEDS); break;
        default:
            delete[] leds;
            leds = nullptr;
            return;
    }
    #else
        Serial.println("\n\nFastLED protocol configuration is disabled on this build.");
        Serial.println("You're likely compiling this project on Windows.");
        Serial.println("The FastLED 'addLeds<>()' switch is not included due to compiler limitations on Windows.");
        Serial.println("To enable full LED support, build this project on Linux and define ENABLE_FASTLED_PROTOCOL_SWITCHES.\n\n");
        FastLED.addLeds<WS2812, LED_DATA_PIN, GRB>(leds, MAX_LEDS);
    #endif

    usesClock = true;
    FastLED.setBrightness(brightness);
    FastLED.show();
}

void LedService::release() {
    if (leds) {
        FastLED.clear(true);
        delete[] leds;
        leds = nullptr;
        ledCount = 0;
        usesClock = false;
        animationRunning = false;
    }
}

void LedService::fill(const CRGB& color) {
    if (!leds) return;
    FastLED.clear(true);
    for (uint16_t i = 0; i < ledCount; ++i) {
        leds[i] = color;
    }
    FastLED.show();
}

void LedService::set(uint16_t index, const CRGB& color) {
    if (!leds || index >= ledCount) return;

    // Clear the LED
    leds[index] = CRGB::Black;
    FastLED.show();

    // Show new color
    leds[index] = color;
    FastLED.show();
}

void LedService::resetLeds() {
    if (!leds) return;
    fill(CRGB::Black);
    FastLED.clear(true);
    animationRunning = false;
}

void LedService::runAnimation(const std::string& type) {
    if (!leds) return;
    animationRunning = true;
    FastLED.clear();

    if (type == "blink") {
        for (int i = 0; i < 3 && animationRunning; ++i) {
            fill(CRGB::White);
            delay(50);
            resetLeds();
            delay(50);
        }
    } else if (type == "rainbow") {
        for (int j = 0; j < 256 && animationRunning; ++j) {
            for (uint16_t i = 0; i < ledCount; ++i) {
                leds[i] = CHSV((i * 10 + j) % 255, 255, 255);
            }
            FastLED.show();
            delay(1);
        }
    } else if (type == "chase") {
        for (int i = 0; i < ledCount * 2 && animationRunning; ++i) {
            fill(CRGB::Black);
            leds[i % ledCount] = CRGB::Blue;
            FastLED.show();
            delay(100);
        }
    } else if (type == "cycle") {
        CRGB colors[] = {CRGB::Red, CRGB::Green, CRGB::Blue};
        for (int c = 0; c < 3 && animationRunning; ++c) {
            fill(colors[c]);
            delay(100);
        }
    } else if (type == "wave") {
        for (int t = 0; t < 256 && animationRunning; ++t) {
            for (uint16_t i = 0; i < ledCount; ++i) {
                uint8_t level = (sin8(i * 8 + t));
                leds[i] = CHSV(160, 255, level);
            }
            FastLED.show();
            delay(1);
        }
    }
    animationRunning = false;
}

bool LedService::isAnimationRunning() const {
    return animationRunning;
}

std::vector<std::string> LedService::getSingleWireProtocols() {
    return LedProtocolEnumMapper::getAllProtocols();
}

std::vector<std::string> LedService::getSpiChipsets() {
    return LedChipsetMapper::getAllChipsets();
}

std::vector<std::string> LedService::getSupportedProtocols() {
    std::vector<std::string> all = getSingleWireProtocols();
    std::vector<std::string> spi = getSpiChipsets();
    all.insert(all.end(), spi.begin(), spi.end());
    return all;
}

std::vector<std::string> LedService::getSupportedAnimations() {
    return {
        "blink", "rainbow", "chase", "cycle", "wave"
    };
}

int LedService::getMaxLeds() {
    return MAX_LEDS;
}

CRGB LedService::parseStringColor(const std::string& input) {
    auto toLower = [](const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    };

    std::string lowered = toLower(input);

    static const std::map<std::string, CRGB> namedColors = {
        {"black", CRGB(0x00, 0x00, 0x00)},
        {"off", CRGB(0x00, 0x00, 0x00)},
        {"white", CRGB(0xFF, 0xFF, 0xFF)},
        {"on", CRGB(0xFF, 0xFF, 0xFF)},
        {"red", CRGB(0xFF, 0x00, 0x00)},
        {"green", CRGB(0x00, 0x80, 0x00)},
        {"blue", CRGB(0x00, 0x00, 0xFF)},
        {"yellow", CRGB(0xFF, 0xFF, 0x00)},
        {"cyan", CRGB(0x00, 0xFF, 0xFF)},
        {"magenta", CRGB(0xFF, 0x00, 0xFF)},
        {"purple", CRGB(0x80, 0x00, 0x80)},
        {"orange", CRGB(0xFF, 0xA5, 0x00)},
        {"pink", CRGB(0xFF, 0xC0, 0xCB)},
        {"brown", CRGB(0xA5, 0x2A, 0x2A)},
        {"gray", CRGB(0x80, 0x80, 0x80)},
        {"navy", CRGB(0x00, 0x00, 0x80)},
        {"teal", CRGB(0x00, 0x80, 0x80)},
        {"olive", CRGB(0x80, 0x80, 0x00)},
        {"lime", CRGB(0x00, 0xFF, 0x00)},
        {"aqua", CRGB(0x00, 0xFF, 0xFF)},
        {"maroon", CRGB(0x80, 0x00, 0x00)},
        {"silver", CRGB(0xC0, 0xC0, 0xC0)},
        {"gold", CRGB(0xFF, 0xD7, 0x00)},
        {"skyblue", CRGB(0x87, 0xCE, 0xEB)},
        {"violet", CRGB(0xEE, 0x82, 0xEE)},
        {"turquoise", CRGB(0x40, 0xE0, 0xD0)},
        {"coral", CRGB(0xFF, 0x7F, 0x50)},
        {"indigo", CRGB(0x4B, 0x00, 0x82)},
        {"salmon", CRGB(0xFA, 0x80, 0x72)},
        {"beige", CRGB(0xF5, 0xF5, 0xDC)},
        {"khaki", CRGB(0xF0, 0xE6, 0x8C)},
        {"plum", CRGB(0xDD, 0xA0, 0xDD)},
        {"orchid", CRGB(0xDA, 0x70, 0xD6)},
        {"tan", CRGB(0xD2, 0xB4, 0x8C)},
        {"chocolate", CRGB(0xD2, 0x69, 0x1E)},
        {"crimson", CRGB(0xDC, 0x14, 0x3C)},
        {"tomato", CRGB(0xFF, 0x63, 0x47)},
        {"darkpink", CRGB(0xFF, 0x14, 0x93)},
        {"darkblue", CRGB(0x00, 0xBF, 0xFF)},
    };

    auto it = namedColors.find(lowered);
    if (it != namedColors.end())
        return it->second;

    return CRGB::White; // fallback
}

CRGB LedService::parseHtmlColor(const std::string& input) {
    auto toLower = [](const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    };

    std::string lowered = toLower(input);

    // #RRGGBB ou 0xRRGGBB
    std::string hex = lowered;
    if (hex.rfind("#", 0) == 0)
        hex = "0x" + hex.substr(1);  // convert # to 0x

    if (hex.rfind("0x", 0) == 0 && hex.length() == 8) {
        try {
            uint32_t value = std::stoul(hex, nullptr, 16);
            uint8_t r = (value >> 16) & 0xFF;
            uint8_t g = (value >> 8) & 0xFF;
            uint8_t b = value & 0xFF;
            return CRGB(r, g, b);
        } catch (...) {
            return CRGB::White;
        }
    }

    return CRGB::White; 
}
