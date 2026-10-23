#pragma once

/*
 * Tab5TerminalView — ITerminalView rendered through Tab5Gui's LVGL
 * widgets.
 *
 * ITerminalView is pure output -- print/println/printPrompt/clear --
 * Bit Pirate's own UserInputManager/CommandLineManager do line editing
 * themselves, char by char, via a *separate* IInput (Tab5TouchInput), and
 * always redraw the whole uncommitted line from scratch on every edit
 * (CommandLineManager's own redraw protocol: `\r` + the full current
 * line + `\033[K`, never insert/shift) -- this class just needs to track
 * what that protocol is currently drawing and mirror it to Tab5Gui, not
 * reimplement line editing.
 *
 * LVGL migration: committed lines (anything ending in '\n') go straight
 * to Tab5Gui's output lv_textarea (tab5gui::appendOutput()) -- LVGL owns
 * real scrollback natively now, so the hand-rolled ring buffer the
 * previous SGFX-based version of this class carried is gone entirely.
 * The one uncommitted "currently being typed" line renders in the input
 * bar's label instead (tab5gui::setInputLineText()) -- same split as
 * before, just a thinner implementation since LVGL does the actual
 * drawing/wrapping.
 *
 * UTF-8/emoji handling: sanitizeUtf8() recognizes the three status-emoji
 * markers this codebase actually checks for programmatically (❌/✅/⚠)
 * and turns each into the matching LVGL built-in symbol glyph
 * (LV_SYMBOL_CLOSE/OK/WARNING -- confirmed present in the compiled-in
 * Montserrat font's glyph table), wrapped in LVGL recolor markup
 * ("#RRGGBB text#") for the verdict color. Every other multi-byte UTF-8
 * sequence (the other 80+ decorative emoji upstream uses) is dropped
 * cleanly rather than drawn as per-byte garbage -- true 1:1 color emoji
 * isn't achievable with LVGL's single-color glyph rendering regardless.
 */

#include <string>

#include "Interfaces/ITerminalView.h"

class Tab5TerminalView : public ITerminalView {
public:
    Tab5TerminalView() = default;
    ~Tab5TerminalView() override = default;

    void initialize() override;
    void welcome(TerminalTypeEnum& terminalType, std::string& terminalInfos) override;
    void print(const std::string& text) override;
    void print(const uint8_t data) override;
    void println(const std::string& text) override;
    void printPrompt(const std::string& mode = "HIZ") override;
    void waitPress() override;
    void clear() override;

    // Lets WebUi.cpp receive everything this view renders, the same way
    // mirrorToStdoutLine() already mirrors it to the USB-serial console --
    // so a connected web-UI client sees exactly what's on the touchscreen,
    // in real time, without this class knowing anything about WebSockets.
    // nullptr (the default) means "no web client mirroring right now".
    using WebMirrorFn = void(*)(const char* data, size_t len, bool withNewline);
    static void setWebMirror(WebMirrorFn fn);

    // Same idea for the USB-CDC CLI -- a separate slot since both can be connected at once.
    static void setCdcMirror(WebMirrorFn fn);

private:
    enum class EscState { None, GotEsc, GotBracket };
    enum class LineVerdict { None, Ok, Warn, Fail };

    std::string currentLine_;      // uncommitted line being built/edited (shown in the input bar)
    int cursorCol_ = 0;
    EscState escState_ = EscState::None;
    LineVerdict pendingVerdict_ = LineVerdict::None; // this print()/println() call's verdict, for commitCurrentLine()

    void putChar(char c);
    void commitCurrentLine();
    static std::string sanitizeUtf8(const std::string& in, LineVerdict& verdict);
    static std::string wrapRecolor(const std::string& text, LineVerdict v);
    static void mirrorToStdoutLine(const std::string& text, bool withNewline);
};
