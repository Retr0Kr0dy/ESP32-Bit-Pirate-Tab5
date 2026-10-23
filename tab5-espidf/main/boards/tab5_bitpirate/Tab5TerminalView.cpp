#include "Tab5TerminalView.h"

#include <cstdio>

#include "lvgl.h"

#include "Enums/TerminalTypeEnum.h"
#include "States/GlobalState.h"
#include "Tab5Gui.h"

void Tab5TerminalView::initialize() {
    currentLine_.clear();
    cursorCol_ = 0;
    escState_ = EscState::None;
    pendingVerdict_ = LineVerdict::None;
    tab5gui::clearOutput();
    tab5gui::setInputLineText("");
}

// Same ASCII banner as SerialTerminalView::welcome(), plus a Tab5-specific
// terminal-type line. Needs termFont() == lv_font_pirate_mono (Tab5Gui.cpp).
void Tab5TerminalView::welcome(TerminalTypeEnum& terminalType, std::string& terminalInfos) {
    std::string label = TerminalTypeEnumMapper::toString(terminalType);
    GlobalState& state = GlobalState::getInstance();

    println("    ____  _ _     ____  _           _       ");
    println("   | __ )(_) |_  |  _ \\(_)_ __ __ _| |_ ___ ");
    println("   |  _ \\| | __| | |_) | | '__/ _` | __/ _ \\");
    println("   | |_) | | |_  |  __/| | | | (_| | ||  __/");
    println("   |____/|_|\\__| |_|   |_|_|  \\__,_|\\__\\___|");
    println("");
    println("             ESP32 SWISS ARMY KNIFE");
    println("");
    println("     Version " + std::string(state.getVersion()) + "           Ready to board");
    println("");
    println("Terminal: " + label);
    if (!terminalInfos.empty()) println(terminalInfos);
    println("");
    println(" Type 'mode' to start or 'help' for commands");
    println("");
}

namespace {
Tab5TerminalView::WebMirrorFn s_webMirror = nullptr;
Tab5TerminalView::WebMirrorFn s_cdcMirror = nullptr;
}

void Tab5TerminalView::setWebMirror(WebMirrorFn fn) { s_webMirror = fn; }
void Tab5TerminalView::setCdcMirror(WebMirrorFn fn) { s_cdcMirror = fn; }

// Mirrors everything this view renders to the USB-serial console, and to a
// web-UI/CDC-CLI client if one has registered via setWebMirror()/setCdcMirror().
void Tab5TerminalView::mirrorToStdoutLine(const std::string& text, bool withNewline) {
    fwrite(text.data(), 1, text.size(), stdout);
    if (withNewline) fwrite("\r\n", 1, 2, stdout);
    fflush(stdout);
    if (s_webMirror) s_webMirror(text.data(), text.size(), withNewline);
    if (s_cdcMirror) s_cdcMirror(text.data(), text.size(), withNewline);
}

// Recognizes the three UTF-8 status-emoji markers this codebase actually
// checks for programmatically (❌ U+274C = E2 9D 8C, ✅ U+2705 = E2 9C 85,
// ⚠ U+26A0 = E2 9A A0), converting each to the matching LVGL built-in
// symbol glyph (LV_SYMBOL_CLOSE/OK/WARNING -- confirmed present in the
// compiled-in Montserrat font's glyph table, not assumed), and reports the
// strongest verdict found so the caller can color the whole line via LVGL
// recolor markup. Every other multi-byte UTF-8 sequence is silently
// dropped rather than drawn as per-byte garbage.
std::string Tab5TerminalView::sanitizeUtf8(const std::string& in, LineVerdict& verdict) {
    verdict = LineVerdict::None;
    std::string out;
    out.reserve(in.size());

    for (size_t i = 0; i < in.size(); ) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0x80) { out.push_back((char)c); ++i; continue; }

        int len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;

        if (len == 3 && i + 2 < in.size()) {
            unsigned char b1 = (unsigned char)in[i + 1];
            unsigned char b2 = (unsigned char)in[i + 2];
            if (c == 0xE2 && b1 == 0x9D && b2 == 0x8C) { // ❌
                out += LV_SYMBOL_CLOSE;
                verdict = LineVerdict::Fail;
                i += 3; continue;
            }
            if (c == 0xE2 && b1 == 0x9C && b2 == 0x85) { // ✅
                if (verdict == LineVerdict::None) verdict = LineVerdict::Ok;
                out += LV_SYMBOL_OK;
                i += 3; continue;
            }
            if (c == 0xE2 && b1 == 0x9A && b2 == 0xA0) { // ⚠
                if (verdict != LineVerdict::Fail) verdict = LineVerdict::Warn;
                out += LV_SYMBOL_WARNING;
                i += 3; continue;
            }
        }

        i += (len > 0) ? len : 1; // drop any other multi-byte sequence silently
    }
    return out;
}

// Wraps `text` in LVGL recolor markup ("#RRGGBB text#") for the given
// verdict, escaping any literal '#' already in the text (doubled, per
// LVGL's own recolor-escape convention) so it isn't mistaken for a markup
// delimiter. No-op (returns `text` unchanged) when there's no verdict --
// the output panel's default text color already applies.
std::string Tab5TerminalView::wrapRecolor(const std::string& text, LineVerdict v) {
    if (v == LineVerdict::None) return text;

    std::string escaped;
    escaped.reserve(text.size());
    for (char c : text) {
        escaped.push_back(c);
        if (c == '#') escaped.push_back('#');
    }

    const char* hex = (v == LineVerdict::Fail) ? "ff5555"
                     : (v == LineVerdict::Warn) ? "ffdc7d"
                     : "00ff00";
    return std::string("#") + hex + " " + escaped + "#";
}

void Tab5TerminalView::print(const std::string& text) {
    LineVerdict v;
    std::string clean = sanitizeUtf8(text, v);
    pendingVerdict_ = v;
    for (char c : clean) putChar(c);
    tab5gui::setInputLineText(currentLine_);
    mirrorToStdoutLine(clean, false);
}

void Tab5TerminalView::print(const uint8_t data) {
    putChar((char)data);
    tab5gui::setInputLineText(currentLine_);
    mirrorToStdoutLine(std::string(1, (char)data), false);
}

void Tab5TerminalView::println(const std::string& text) {
    LineVerdict v;
    std::string clean = sanitizeUtf8(text, v);
    pendingVerdict_ = v;
    for (char c : clean) putChar(c);
    putChar('\n');
    tab5gui::setInputLineText(currentLine_);
    mirrorToStdoutLine(clean, true);
}

void Tab5TerminalView::printPrompt(const std::string& mode) {
    if (!mode.empty()) {
        print(mode);
    }
    print("> ");
}

void Tab5TerminalView::waitPress() {
    println("");
    println("Press any key to start...");
}

void Tab5TerminalView::clear() {
    currentLine_.clear();
    cursorCol_ = 0;
    escState_ = EscState::None;
    pendingVerdict_ = LineVerdict::None;
    tab5gui::clearOutput();
    tab5gui::setInputLineText("");
}

// ---- internals ----------------------------------------------------------

void Tab5TerminalView::putChar(char c) {
    // Minimal escape handling: `\x1B[C`/`\x1B[D` reposition the cursor
    // within the uncommitted line; any other sequence tail (e.g. `\x1B[K`)
    // is swallowed rather than drawn as garbage. (No visible blinking
    // caret is rendered for cursor position -- a minor, deliberate
    // simplification; CommandLineManager's own full-line redraw protocol
    // doesn't depend on one.)
    if (escState_ == EscState::None) {
        if (c == '\x1B') { escState_ = EscState::GotEsc; return; }
    } else if (escState_ == EscState::GotEsc) {
        escState_ = (c == '[') ? EscState::GotBracket : EscState::None;
        return;
    } else { // GotBracket
        escState_ = EscState::None;
        if (c == 'C') { if (cursorCol_ < (int)currentLine_.size()) ++cursorCol_; return; }
        if (c == 'D') { if (cursorCol_ > 0) --cursorCol_; return; }
        return;
    }

    if (c == '\n') { commitCurrentLine(); return; }
    if (c == '\r') { cursorCol_ = 0; return; }
    if (c == '\b' || c == 127) {
        // Overwrite-in-place, not erase/shift -- matches CommandLineManager's
        // own redraw protocol, which always re-sends the full corrected line
        // (plus a trailing blank to cover the one-character shrinkage) right
        // after this, so the stale tail never actually stays visible.
        if (cursorCol_ > 0) {
            --cursorCol_;
            if ((int)currentLine_.size() <= cursorCol_) currentLine_.resize(cursorCol_ + 1, ' ');
            currentLine_[(size_t)cursorCol_] = ' ';
        }
        return;
    }

    if ((int)currentLine_.size() <= cursorCol_) currentLine_.resize(cursorCol_ + 1, ' ');
    currentLine_[(size_t)cursorCol_] = c;
    ++cursorCol_;
}

void Tab5TerminalView::commitCurrentLine() {
    tab5gui::appendOutput(wrapRecolor(currentLine_, pendingVerdict_) + "\n");
    currentLine_.clear();
    cursorCol_ = 0;
    pendingVerdict_ = LineVerdict::None;
}
