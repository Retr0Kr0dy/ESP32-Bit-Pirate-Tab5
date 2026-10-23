#pragma once
#include <stdint.h>

struct AsciiHid {
    uint8_t keycode;
    bool requiresShift;
};

// Table ASCII (0x00 à 0x7F) to HID
static constexpr AsciiHid asciiHid[128] = {
    {0, false}, {0, false}, {0, false}, {0, false},  // 0x00 – 0x03
    {0, false}, {0, false}, {0, false}, {0, false},  // 0x04 – 0x07
    {0x2a, false}, {0x2b, false}, {0x28, false}, {0, false},  // BS, TAB, LF
    {0, false}, {0, false}, {0, false}, {0, false},
    {0, false}, {0, false}, {0, false}, {0, false}, {0, false}, {0, false}, {0, false}, {0, false},
    {0, false}, {0, false}, {0, false}, {0, false}, {0, false}, {0, false}, {0, false}, {0, false},

    {0x2c, false}, // ' '
    {0x1e, true},  // !
    {0x34, true},  // "
    {0x20, true},  // #
    {0x21, true},  // $
    {0x22, true},  // %
    {0x24, true},  // &
    {0x34, false}, // '
    {0x26, true},  // (
    {0x27, true},  // )
    {0x25, true},  // *
    {0x2e, true},  // +
    {0x36, false}, // ,
    {0x2d, false}, // -
    {0x37, false}, // .
    {0x38, false}, // /

    {0x27, false}, {0x1e, false}, {0x1f, false}, {0x20, false}, {0x21, false},  // 0–4
    {0x22, false}, {0x23, false}, {0x24, false}, {0x25, false}, {0x26, false},  // 5–9

    {0x33, true},  // :
    {0x33, false}, // ;
    {0x36, true},  // <
    {0x2e, false}, // =
    {0x37, true},  // >
    {0x38, true},  // ?
    {0x1f, true},  // @

    // A–Z
    {0x04, true}, {0x05, true}, {0x06, true}, {0x07, true}, {0x08, true},
    {0x09, true}, {0x0a, true}, {0x0b, true}, {0x0c, true}, {0x0d, true},
    {0x0e, true}, {0x0f, true}, {0x10, true}, {0x11, true}, {0x12, true},
    {0x13, true}, {0x14, true}, {0x15, true}, {0x16, true}, {0x17, true},
    {0x18, true}, {0x19, true}, {0x1a, true}, {0x1b, true}, {0x1c, true},
    {0x1d, true},

    {0x2f, false}, // [
    {0x31, false}, // '\'
    {0x30, false}, // ]
    {0x23, true},  // ^
    {0x2d, true},  // _

    {0x35, false}, // `
    
    // a–z
    {0x04, false}, {0x05, false}, {0x06, false}, {0x07, false}, {0x08, false},
    {0x09, false}, {0x0a, false}, {0x0b, false}, {0x0c, false}, {0x0d, false},
    {0x0e, false}, {0x0f, false}, {0x10, false}, {0x11, false}, {0x12, false},
    {0x13, false}, {0x14, false}, {0x15, false}, {0x16, false}, {0x17, false},
    {0x18, false}, {0x19, false}, {0x1a, false}, {0x1b, false}, {0x1c, false},
    {0x1d, false},

    // { | } ~
    {0x2f, true}, {0x31, true}, {0x30, true}, {0x35, true},

    {0, false} // DEL
};
