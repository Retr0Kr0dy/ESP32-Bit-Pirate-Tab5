#include "Tab5DeviceView.h"

#include <SPI.h>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "lvgl.h"

#include "Enums/TerminalTypeEnum.h"
#include "display.h"
#include "Tab5Gui.h"
#include "Tab5Layout.h"

using namespace tab5;

// ---- Trace canvas: a real graphical surface for drawLogicTrace()/
// drawAnalogicTrace()/drawWaterfall(), sized to sit exactly where the
// output panel normally does (same region Tab5Layout.h reserves for it in
// the history-footer layout -- kOutputHHistory -- not the full screen,
// so it coexists with the persistent input bar/history strip below it,
// same as every other overlay in this GUI). Lazily created on first use,
// parented directly to lv_scr_act() (no need to touch Tab5Gui.cpp's own
// widget tree), shown via move-to-foreground + clear-hidden, same
// convention Tab5Gui.cpp's file-browser overlay already established.
namespace {
constexpr int kTraceW = kScreenW - 2 * kPad;   // 1260
constexpr int kTraceH = kOutputHHistory;        // 420 -- matches the output panel's own height

// drawLogicTrace()/drawAnalogicTrace()'s `step` parameter is pixels-per-
// sample, a "zoom" the original reference (UtilityController.cpp's step=1..4,
// "step of the trace display kind of a zoom") calibrated for the S3 Bit
// Pirate's small ST7789 TFT, where canvas width == buffer size (240 or 320
// samples, see SubGhzController.cpp/FmController.cpp/UtilityController.cpp's
// matching buffer.resize() caps) -- so step=1 there means exactly 1px/sample,
// filling the screen edge-to-edge. Taken literally on this canvas (1260px,
// 4-5x wider), step=1 only fills the leftmost 240-320px and leaves the rest
// blank. Rescale: treat the controller's `step` as a multiplier on the
// baseline spacing that would exactly fill kTraceW for THIS buffer's actual
// sample count, so step=1 still means "fit the whole buffer to the screen"
// (now correctly sized for 1260px) and step=2..4 still zoom in relative to
// that, same as the original intent.
int scaledStep(uint8_t step, size_t sampleCount) {
    if (sampleCount == 0) return step ? step : 1;
    int usableW = kTraceW - 12; // matches the x=6 start margin used on both sides
    int baseline = usableW / (int)sampleCount;
    if (baseline < 1) baseline = 1;
    int scaled = baseline * (step ? step : 1);
    return scaled < 1 ? 1 : scaled;
}
}

void Tab5DeviceView::ensureTraceCanvas() {
    if (traceCanvas_) return;

    tab5gui::lvglLockAcquire();
    const uint32_t bufBytes = LV_CANVAS_BUF_SIZE_TRUE_COLOR(kTraceW, kTraceH);
    traceCanvasBuf_ = heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);
    lv_obj_t* canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(canvas, traceCanvasBuf_, kTraceW, kTraceH, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(canvas, kPad, kPad);
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    traceCanvas_ = canvas;
    tab5gui::registerTraceCanvas(canvas);
    tab5gui::lvglLockRelease();
}

void Tab5DeviceView::showTraceCanvas() {
    ensureTraceCanvas();
    lv_obj_t* canvas = static_cast<lv_obj_t*>(traceCanvas_);
    lv_obj_move_foreground(canvas);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_HIDDEN);
}

void Tab5DeviceView::initialize() {
    // Display/touch bring-up and tab5gui::begin() already happened in
    // app_main()/bitpirate_task_entry() before any IDeviceView
    // implementation is constructed -- nothing to do here.
}

SPIClass& Tab5DeviceView::getSharedSpiInstance() {
    if (!sharedSpi_) {
        sharedSpi_ = new SPIClass();
        sharedSpi_->begin(/*sck*/ 5, /*miso*/ 19, /*mosi*/ 18, /*ss*/ -1);
    }
    return *sharedSpi_;
}

void* Tab5DeviceView::getScreen() {
    return orion_gfx();
}

// logo()/welcome(): provided through the upstream device-view interface
// (confirmed: setup() only prints banner text via ITerminalView, it never
// calls either of these) -- kept for IDeviceView interface compliance via
// the same output-panel text path everything else uses, rather than
// building a dedicated full-screen LVGL splash for genuinely dead code.
void Tab5DeviceView::logo() {
    tab5gui::appendOutput("ESP32 BIT PIRATE - Tab5 Edition\n");
}

void Tab5DeviceView::welcome(TerminalTypeEnum& terminalType, std::string& terminalInfos) {
    std::string label = TerminalTypeEnumMapper::toString(terminalType);
    tab5gui::appendOutput("ESP32 BIT PIRATE\n");
    tab5gui::appendOutput(label + "\n");
    if (!terminalInfos.empty()) tab5gui::appendOutput(terminalInfos + "\n");
}

// ---- The rest of IDeviceView's graphical surface is INERT (no-ops) in
// this build -- see this class's header comment for why. ------------------

void Tab5DeviceView::show(PinoutConfig& /*config*/) {}
void Tab5DeviceView::loading() {}
void Tab5DeviceView::adapterMode(const std::string& /*adapterName*/, const std::string& /*description*/, const std::vector<std::string>& /*details*/) {}
void Tab5DeviceView::clear() {}
// Digital step trace -- matches St7789SpiDeviceView.cpp's semantics exactly
// (axis-aligned segments only, never diagonal: a horizontal run at the
// previous level, then a vertical jump to the new one), scaled up from the
// original's small-TFT proportions to this canvas's much larger size.
void Tab5DeviceView::drawLogicTrace(uint8_t pin, const std::vector<uint8_t>& buffer, uint8_t step) {
    showTraceCanvas();
    lv_obj_t* canvas = static_cast<lv_obj_t*>(traceCanvas_);
    tab5gui::lvglLockAcquire();

    lv_canvas_fill_bg(canvas, lv_color_hex(kColorBg), LV_OPA_COVER);

    char hdr[32];
    snprintf(hdr, sizeof(hdr), "GPIO %u", (unsigned)pin);
    lv_draw_label_dsc_t labelDsc;
    lv_draw_label_dsc_init(&labelDsc);
    labelDsc.color = lv_color_hex(0xFFFFFF);
    lv_canvas_draw_text(canvas, 6, 4, kTraceW - 12, &labelDsc, hdr);

    const int traceTop = 40;
    const int traceBottom = kTraceH - 10;
    const int centerY = (traceTop + traceBottom) / 2;
    const int amp = (traceBottom - traceTop) / 2 - 5;

    lv_draw_line_dsc_t lineDsc;
    lv_draw_line_dsc_init(&lineDsc);
    lineDsc.width = 2;

    const int step_ = scaledStep(step, buffer.size());
    int x = 6;
    for (size_t i = 1; i < buffer.size(); ++i) {
        uint8_t prev = buffer[i - 1];
        uint8_t curr = buffer[i];
        int y1 = prev ? (centerY - amp) : (centerY + amp);
        int y2 = curr ? (centerY - amp) : (centerY + amp);

        if (curr != prev) {
            lineDsc.color = lv_color_hex(prev ? kColorGreen : 0xFFFFFF);
            lv_point_t seg1[2] = {{(lv_coord_t)x, (lv_coord_t)y1}, {(lv_coord_t)(x + step_), (lv_coord_t)y1}};
            lv_canvas_draw_line(canvas, seg1, 2, &lineDsc);
            lineDsc.color = lv_color_hex(curr ? kColorGreen : 0xFFFFFF);
            lv_point_t seg2[2] = {{(lv_coord_t)(x + step_), (lv_coord_t)y1}, {(lv_coord_t)(x + step_), (lv_coord_t)y2}};
            lv_canvas_draw_line(canvas, seg2, 2, &lineDsc);
        } else {
            lineDsc.color = lv_color_hex(curr ? kColorGreen : 0xFFFFFF);
            lv_point_t seg[2] = {{(lv_coord_t)x, (lv_coord_t)y1}, {(lv_coord_t)(x + step_), (lv_coord_t)y2}};
            lv_canvas_draw_line(canvas, seg, 2, &lineDsc);
        }

        x += step_;
        if (x > kTraceW - step_) break;
    }

    lv_obj_invalidate(canvas);
    tab5gui::lvglLockRelease();
}

// Continuous analog plot -- same value-to-height mapping formula as the
// reference implementation (buffer value >>1, scaled over the available
// height), drawn as genuine point-to-point diagonal lines (the reference
// implementation does this too for the analog case, unlike the logic
// trace's axis-aligned-only segments).
void Tab5DeviceView::drawAnalogicTrace(uint8_t pin, const std::vector<uint8_t>& buffer, uint8_t step) {
    showTraceCanvas();
    lv_obj_t* canvas = static_cast<lv_obj_t*>(traceCanvas_);
    tab5gui::lvglLockAcquire();

    lv_canvas_fill_bg(canvas, lv_color_hex(kColorBg), LV_OPA_COVER);

    char hdr[32];
    snprintf(hdr, sizeof(hdr), "GPIO %u", (unsigned)pin);
    lv_draw_label_dsc_t labelDsc;
    lv_draw_label_dsc_init(&labelDsc);
    labelDsc.color = lv_color_hex(0xFFFFFF);
    lv_canvas_draw_text(canvas, 6, 4, kTraceW - 12, &labelDsc, hdr);

    const int topY = 40;
    const int h = kTraceH - topY - 10;

    lv_draw_line_dsc_t lineDsc;
    lv_draw_line_dsc_init(&lineDsc);
    lineDsc.width = 2;
    lineDsc.color = lv_color_hex(kColorGreen);

    const int step_ = scaledStep(step, buffer.size());
    int x = 6;
    for (size_t i = 1; i < buffer.size(); ++i) {
        int prev = topY + (h - 1) - (((buffer[i - 1] >> 1) * (h - 1)) / 134);
        int curr = topY + (h - 1) - (((buffer[i] >> 1) * (h - 1)) / 134);
        lv_point_t seg[2] = {{(lv_coord_t)x, (lv_coord_t)prev}, {(lv_coord_t)(x + step_), (lv_coord_t)curr}};
        lv_canvas_draw_line(canvas, seg, 2, &lineDsc);
        x += step_;
        if (x > kTraceW - step_) break;
    }

    lv_obj_invalidate(canvas);
    tab5gui::lvglLockRelease();
}

// Scrolling RF spectrum/level waterfall -- mirrors the reference
// implementation's row-by-row sweep: rowIndex==0 draws the header/labels/
// center line once, every call draws (or redraws) one horizontal energy
// bar at the row position rowIndex maps to within [0, rowCount).
void Tab5DeviceView::drawWaterfall(
    const std::string& title,
    float startValue,
    float endValue,
    const char* unit,
    int rowIndex,
    int rowCount,
    int level
) {
    showTraceCanvas();
    lv_obj_t* canvas = static_cast<lv_obj_t*>(traceCanvas_);
    tab5gui::lvglLockAcquire();

    const int midX = kTraceW / 2;
    const int headerH = 24;
    const int footerH = 24;
    const int graphY = headerH;
    const int graphH = kTraceH - headerH - footerH;
    const int barMaxPixels = midX - 4;

    if (level < 0) level = 0;
    if (level > 100) level = 100;
    int barPixels = (level * barMaxPixels) / 100;

    lv_draw_rect_dsc_t rectDsc;
    lv_draw_rect_dsc_init(&rectDsc);
    lv_draw_label_dsc_t labelDsc;
    lv_draw_label_dsc_init(&labelDsc);
    labelDsc.color = lv_color_hex(0xFFFFFF);
    lv_draw_line_dsc_t lineDsc;
    lv_draw_line_dsc_init(&lineDsc);
    lineDsc.width = 2;
    lineDsc.color = lv_color_hex(kColorChrome);

    if (rowIndex == 0) {
        lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_COVER);
        lv_canvas_draw_text(canvas, 4, 2, kTraceW - 8, &labelDsc, title.c_str());

        char bufStart[24];
        char bufEnd[24];
        if (unit && unit[0]) {
            snprintf(bufStart, sizeof(bufStart), "%.2f%s", startValue, unit);
            snprintf(bufEnd,   sizeof(bufEnd),   "%.2f%s", endValue,   unit);
        } else {
            snprintf(bufStart, sizeof(bufStart), "%.2f", startValue);
            snprintf(bufEnd,   sizeof(bufEnd),   "%.2f", endValue);
        }
        lv_point_t sz;
        lv_txt_get_size(&sz, bufStart, LV_FONT_DEFAULT, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_canvas_draw_text(canvas, kTraceW - sz.x - 4, 2, sz.x + 2, &labelDsc, bufStart);
        lv_txt_get_size(&sz, bufEnd, LV_FONT_DEFAULT, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_canvas_draw_text(canvas, kTraceW - sz.x - 4, kTraceH - footerH + 2, sz.x + 2, &labelDsc, bufEnd);

        rectDsc.bg_color = lv_color_hex(kColorBg);
        rectDsc.bg_opa = LV_OPA_COVER;
        lv_canvas_draw_rect(canvas, 0, graphY, kTraceW, graphH, &rectDsc);
        lv_point_t centerLine[2] = {{(lv_coord_t)midX, (lv_coord_t)graphY}, {(lv_coord_t)midX, (lv_coord_t)(graphY + graphH)}};
        lv_canvas_draw_line(canvas, centerLine, 2, &lineDsc);
    }

    if (rowCount > 1) {
        if (rowIndex < 0) rowIndex = 0;
        if (rowIndex > rowCount - 1) rowIndex = rowCount - 1;
        int y = graphY + (int)((int64_t)rowIndex * (graphH - 1) / (rowCount - 1));

        rectDsc.bg_color = lv_color_hex(kColorBg);
        rectDsc.bg_opa = LV_OPA_COVER;
        lv_canvas_draw_rect(canvas, 0, y, kTraceW, 2, &rectDsc);

        rectDsc.bg_color = lv_color_hex(kColorChrome);
        lv_canvas_draw_rect(canvas, midX - 1, y, 2, 2, &rectDsc);

        if (barPixels > 0) {
            int x0 = midX - barPixels;
            int w = barPixels * 2;
            if (x0 < 0) { w += x0; x0 = 0; }
            if (x0 + w > kTraceW) w = kTraceW - x0;
            if (w > 0) {
                rectDsc.bg_color = lv_color_hex(kColorGreen);
                lv_canvas_draw_rect(canvas, x0, y, w, 2, &rectDsc);
            }
        }
    }

    lv_obj_invalidate(canvas);
    tab5gui::lvglLockRelease();
}
void Tab5DeviceView::topBar(const std::string& /*title*/, bool, bool) {}
void Tab5DeviceView::horizontalSelection(const std::vector<std::string>& /*options*/, uint16_t, const std::string&, const std::string&) {}

void Tab5DeviceView::setRotation(uint8_t rotation) {
    // This GUI's own layout (Tab5Layout.h) is landscape-only, fixed at
    // rotation=1 by main.c once at boot. Passed through regardless, since
    // IDeviceView's contract requires it.
    orion_display_set_rotation(rotation);
}

void Tab5DeviceView::setBrightness(uint8_t brightness) {
    orion_backlight_set(brightness);
}

uint8_t Tab5DeviceView::getBrightness() {
    int pct = orion_backlight_get();
    return pct < 0 ? 0 : (uint8_t)pct;
}
