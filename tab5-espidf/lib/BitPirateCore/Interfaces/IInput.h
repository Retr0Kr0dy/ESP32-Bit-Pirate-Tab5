#pragma once

#include <string>
#include "Data/InputKeys.h"
#include <cstdint>

// Interface for terminal input
// This is the interface expected by the ActionDispatcher to handle user input.
// It abstracts how input is received (from keyboard, serial, web, etc.).

class IInput {
public:
    virtual ~IInput() = default;

    // Blocking read. Concrete classes implement handlerImpl(); this wrapper
    // transparently swallows a CRLF pair's second byte -- see
    // expectLineEndingComplement() below.
    char handler() {
        if (pendingComplement_ != KEY_NONE) {
            char expected = pendingComplement_;
            pendingComplement_ = KEY_NONE;
            char c = handlerImpl();
            if (c == expected) return handler();
            return c;
        }
        return handlerImpl();
    }

    // Non blocking read. Same CRLF-pair handling as handler(), but since
    // this must not block, a "nothing available yet" result re-arms the
    // expectation instead of dropping it -- the complement byte may still
    // show up on a later poll.
    char readChar() {
        if (pendingComplement_ != KEY_NONE) {
            char expected = pendingComplement_;
            pendingComplement_ = KEY_NONE;
            char c = readCharImpl();
            if (c == KEY_NONE) {
                pendingComplement_ = expected;
                return KEY_NONE;
            }
            if (c == expected) return readChar();
            return c;
        }
        return readCharImpl();
    }

    // Wait an inpout
    virtual void waitPress(uint32_t timeoutMs = 0) = 0;

    // Call right after consuming a '\r' or '\n' as a line ending, so the
    // very next read on this same input (whichever call site that is --
    // state lives here, not per-caller) transparently discards that pair's
    // complementary byte if and only if it actually shows up, instead of
    // ever blindly discarding whatever byte comes next. A client that ends
    // lines with a bare '\r' or '\n' (no pair) is unaffected: the next real
    // byte simply won't match and is returned untouched.
    void expectLineEndingComplement(char justConsumed) {
        pendingComplement_ = (justConsumed == '\r') ? '\n' : '\r';
    }

protected:
    // Concrete classes implement these instead of handler()/readChar()
    // directly, so the CRLF-pair handling above applies uniformly to every
    // IInput caller.
    virtual char handlerImpl() = 0;
    virtual char readCharImpl() = 0;

private:
    char pendingComplement_ = KEY_NONE;
};