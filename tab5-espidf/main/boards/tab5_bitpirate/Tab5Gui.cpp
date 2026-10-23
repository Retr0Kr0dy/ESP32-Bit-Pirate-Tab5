#include "Tab5Gui.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>

#include "lvgl.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// TEMPORARY diagnostic, used to verify files-overlay geometry and the
// LittleFS mount fix with real logged coordinates -- left disabled by
// default, same pattern as this project's other BITPIRATE_RUN_* harnesses.
#define BITPIRATE_RUN_FILES_LAYOUT_DIAG 0

#include "display.h"
#include "sgfx.h"
#include "touch_bridge.h"

#include "Tab5Layout.h"

// Files overlay backing stores: LittleFS (same class Infrared/SubGHz
// captures and WebUi.cpp's /littlefs/* endpoints already use -- a
// separate instance here, same pattern WebUi.cpp itself uses, since
// LittleFsService has no shared/singleton accessor) and Tab5's own
// onboard SD card, mounted at /sdcard by the `sdcard` command
// (GenericBusDispatcher.cpp) via sic_sd(0) -- a plain POSIX VFS mount
// once that's happened, so opendir/stat/fopen on "/sdcard/..." is all
// this needs, no SIC-specific API beyond the presence check below.
#include "Services/LittleFsService.h"
#include "sic/storage/sd.h"

using namespace tab5;

namespace tab5gui { void forceDirectModeSyncCatchUp(lv_obj_t* obj); } // defined below

namespace {

const char* TAG = "tab5gui";

// ---- LVGL custom allocator: PSRAM, not LVGL's internal static pool -----
// (CONFIG_LV_MEM_CUSTOM=y in sdkconfig.defaults expects exactly these three
// C symbols; verified working in the earlier feasibility spike.)

lv_disp_draw_buf_t s_drawBuf;
lv_disp_drv_t      s_dispDrv;
lv_indev_drv_t     s_indevDrv;
lv_color_t*        s_fb1 = nullptr;
lv_color_t*        s_fb2 = nullptr;

QueueHandle_t s_charQueue = nullptr;

// Set by appendOutput()/clearOutput() instead of forcing the catch-up
// flush inline (see forceDirectModeSyncCatchUp()'s header comment and
// drainPendingSyncCatchUp() below) -- a burst of N appends (e.g. a long
// `help` listing, dozens of println() calls) used to pay 2 forced
// full-frame present()s EACH, serially, which measured out to minutes for
// a routine help listing. One flag check per append is free; the actual
// 2-present flush now happens at most once, after the burst ends.
static volatile bool s_pendingSyncCatchUp = false;

// LVGL is not thread-safe: every call into its API must be serialized
// against the task driving lv_timer_handler() (lvTimerTask, below). This
// project calls into LVGL from a second task too (bitpirate_task, via
// appendOutput/clearOutput/setInputLineText/setHistoryEntries) -- without
// a lock, lvTimerTask's internal object/event-list walk can run
// concurrently with e.g. setHistoryEntries()'s lv_obj_clean() on the other
// task, corrupting LVGL's internal linked lists. Confirmed on real
// hardware: a Load access fault in _lv_event_mark_deleted (lv_event.c:157,
// `e = e->prev`) triggered by setHistoryEntries() racing lvTimerTask.
// Recursive because historyBtnClickedCb() (an LVGL event callback, already
// running ON lvTimerTask while holding this lock -- see lvTimerTask below)
// calls pushChar(), which does not touch LVGL itself, but keeping this
// recursive costs nothing and avoids a deadlock if any future callback
// path calls back into one of the guarded functions below.
SemaphoreHandle_t s_lvglLock = nullptr;

struct LvglGuard {
    LvglGuard()  { xSemaphoreTakeRecursive(s_lvglLock, portMAX_DELAY); }
    ~LvglGuard() { xSemaphoreGiveRecursive(s_lvglLock); }
};

// Widgets
// ---- Terminal output: custom line-windowed lv_canvas renderer, NOT a plain
// lv_textarea -- see termRender()'s header comment for the full root-cause
// writeup (lv_textarea_add_text() recomputes line-break layout for the
// ENTIRE accumulated text on every single append, confirmed in LVGL's own
// lv_label.c, which made big dumps like "help all" progressively slower as
// the buffer grew). This renderer only ever does wrap/draw work proportional
// to the VISIBLE rows, regardless of how much scrollback exists.
struct TermLine {
    std::string text;  // one logical line's markup (recolor spans intact), no trailing '\n'
    // 0, NOT 1, despite an empty line always occupying 1 VISUAL row (see
    // termWrapLine()'s do-while, which correctly returns 1 for ""). This
    // field tracks what's CURRENTLY reflected in s_termTotalRows, and a
    // freshly push_back({})'d line hasn't been added to that total yet --
    // appendOutput()'s "s_termTotalRows -= last.rows; ... += newRows"
    // pattern assumes `rows` is the PRIOR contribution to subtract before
    // adding the new one. Defaulting this to 1 double-counted every new
    // line (subtracted a contribution that was never added), permanently
    // undercounting the total -- confirmed on real hardware: after any
    // amount of output, including a full "help all" dump, drag-to-scroll's
    // maxScroll stayed stuck at 0, since s_termTotalRows never grew past
    // viewRows no matter how much was actually printed.
    int rows = 0;
};
std::deque<TermLine> s_termLines;
constexpr size_t kTermMaxLines = 4000; // logical lines -- cheap to keep this high now; cost is O(visible), not O(total)
int  s_termTotalRows   = 0;  // sum of s_termLines[*].rows, maintained incrementally (O(1) per update)
int  s_termScrollRows  = 0;  // 0 = pinned to the newest line; >0 = scrolled back N visual rows
int  s_termScrollFracPx = 0; // 0..rowH-1 sub-row offset beyond s_termScrollRows -- pixel-smooth scroll instead of row-snapped
int  s_termRowH        = 0;  // cached row advance (font line height + gap), computed once
lv_obj_t* s_termCanvas    = nullptr;
void*     s_termCanvasBuf = nullptr;
constexpr int kTermCanvasBufH = kOutputHHistory; // buffer is allocated once at the TALLEST layout needs (history footer); the keyboard-footer layout just uses a cropped top portion via lv_obj_set_height()
// Named constant, not an inline "kScreenW - 2*kPad" expression, deliberately
// -- LV_CANVAS_BUF_SIZE_TRUE_COLOR(w, h) expands to "(.. * w * h)" with NO
// parens around its own w/h parameters, so passing an unparenthesized
// expression breaks operator precedence (confirmed the hard way: produced
// a huge wrapped uint32_t and a NULL malloc, which then hung forever inside
// LVGL's LV_ASSERT_NULL for the zero buffer). Tab5DeviceView.cpp's own
// trace canvas already uses this exact same named-constant pattern for the
// same macro, for the same reason.
constexpr int kTermCanvasW = kScreenW - 2 * kPad;

bool      s_termDragging = false;
lv_coord_t s_termDragStartY = 0;
int       s_termDragStartScrollPx = 0; // drag-start scroll position in pixels (rows*rowH + frac)
// Fling, modeled on LVGL's own scroll-throw (lv_indev_scroll.c,
// _lv_indev_scroll_throw_handler): EMA of per-event pixel deltas while
// dragging, then an lv_timer decays and applies that velocity after release.
// Hand-rolled because LVGL's own version drives lv_obj scrolling, and this
// canvas has no scrollable content area for it to attach to.
float     s_termVelPxPerTick = 0.0f;
lv_coord_t s_termLastDragY = 0;
lv_timer_t* s_termMomentumTimer = nullptr;
constexpr float kTermMomentumDecay      = 0.92f; // per-tick velocity retention; ~30 ticks to decay a fast flick to a stop
constexpr float kTermMomentumStopPxTick = 0.5f;  // below this |velocity|, momentum timer deletes itself
constexpr float kTermMomentumMinStartPxTick = 1.0f; // a release slower than this doesn't start a fling at all -- matches a deliberate slow drag-and-lift, not a flick

lv_obj_t* s_inputBar      = nullptr;
lv_obj_t* s_textZone      = nullptr;
lv_obj_t* s_inputLabel    = nullptr;
lv_obj_t* s_historyCont   = nullptr;
lv_obj_t* s_kb            = nullptr;
lv_obj_t* s_kbScratchTa   = nullptr;

bool s_keyboardVisible = false;
int  s_scratchLastLen  = 0;

std::vector<std::string> s_historyTexts;

// Tab5DeviceView's trace canvas (registered via registerTraceCanvas()) --
// see that function's declaration in Tab5Gui.h for why appendOutput()/
// clearOutput() need to know about it.
lv_obj_t* s_traceCanvas = nullptr;

// ---- Files overlay state -------------------------------------------------
lv_obj_t* s_filesOverlay  = nullptr;
lv_obj_t* s_filesListCont = nullptr;
lv_obj_t* s_filesTitleLbl = nullptr;

LittleFsService s_filesLfs;
bool s_filesSourceIsSd = false;
// Current directory being listed -- "/sdcard" (SD root) or "/" (LittleFS
// root) until a directory row is tapped. Reset to that source's root every
// time the overlay is (re)opened or the source is switched, so browsing
// never carries stale state across sessions.
std::string s_filesCurrentDir = "/";
// Parallel to each row actually built in the list container (creation
// order == index), same "store the real data next to the row, keep only
// an index in the row's user_data" pattern historyBtnClickedCb already
// uses for s_historyTexts.
std::vector<std::string> s_filesEntryPaths;
std::vector<bool>        s_filesEntryIsDir;

void pushChar(char c) {
    if (s_charQueue) xQueueSend(s_charQueue, &c, 0);
}

// ---- Display flush / touch read bridge, into the existing proven
// SIC/display.c pixel path -- unchanged from the LVGL feasibility spike
// (lvgl_spike.c, now retired; see this file's header comment). ----------

// Perf counters, logged periodically by lvTimerTask -- concrete evidence
// of the full_refresh=0 + flush_is_last gating below actually reducing
// present() calls (the expensive one: orion_dsi_present() does a real
// vsync-synced hardware buffer flip, sgfx_hal_dsi_flip(), a blocking
// wait), not just an assertion that it "feels smoother."
volatile uint32_t s_flushCalls   = 0;
volatile uint32_t s_presentCalls = 0;

// Given by dispFlushCb right after a successful orion_dsi_present_kick(),
// consumed by dsiFlipWaitTask -- see that task and the long comment below
// for the full design. Binary: at most one kick is ever outstanding at a
// time (LVGL does not call dispFlushCb again for a new cycle until
// lv_disp_flush_ready() has been called, which only happens after the
// matching wait completes -- see dsiFlipWaitTask).
SemaphoreHandle_t s_flushKickedSem = nullptr;

// --- Dirty-region blit ---------------------------------------------------
// LVGL calls flush_cb once per dirty area per cycle, acking non-last calls
// without presenting; s_curCycleArea unions every area in the current
// cycle, consumed on the last call.
//
// That alone isn't correct under direct_mode: lv_refr.c's refr_sync_areas()
// silently copies the PREVIOUS cycle's areas into buf_act before this
// cycle's own drawing, with no flush_cb call for that copy. Our physical
// back buffer (last written 2 presents ago) needs catching up on BOTH the
// previous cycle's area and this cycle's -- blitting only the current one
// left the previous cycle's area stale (confirmed via a byte-compare of the
// two physical buffers: ~500K/1.8M bytes differing before this fix, 0
// after). s_prevCycleArea carries last cycle's area forward one flush, the
// same idea LVGL applies to buf1/buf2 themselves, one layer down.
static lv_area_t s_curCycleArea;
static bool      s_curCycleAreaValid = false;
// Starts as the full screen so the very first flush after boot still forces
// a full-screen catch-up rather than assuming nothing needs it.
static lv_area_t s_prevCycleArea = {0, 0, (lv_coord_t)(kScreenW - 1), (lv_coord_t)(kScreenH - 1)};

void dispFlushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    (void)color_p;
    if (!s_curCycleAreaValid) {
        s_curCycleArea = *area;
        s_curCycleAreaValid = true;
    } else {
        _lv_area_join(&s_curCycleArea, &s_curCycleArea, area);
    }
    if (lv_disp_flush_is_last(drv)) {
        sgfx_device_t* d = orion_gfx();
        if (d) {
            lv_color_t* full = (lv_color_t*)drv->draw_buf->buf_act;
            lv_area_t blitArea = s_curCycleArea;
            _lv_area_join(&blitArea, &blitArea, &s_prevCycleArea);
            s_prevCycleArea = s_curCycleArea; // this cycle's area becomes "previous" for the NEXT flush
            s_curCycleAreaValid = false;      // consumed -- next cycle starts a fresh union
            const int ax = blitArea.x1;
            const int ay = blitArea.y1;
            const int aw = blitArea.x2 - blitArea.x1 + 1;
            const int ah = blitArea.y2 - blitArea.y1 + 1;
            int64_t tBlit0 = esp_timer_get_time();
            // PPA hardware rotation first (measured ~57ms -> ~26-42ms for a
            // full-screen blit vs the CPU per-pixel path -- see display.h's
            // comment on orion_dsi_blit_rotated_ppa()), CPU sgfx_blit() as
            // the fallback. Both blit just (ax,ay,aw,ah), not the full screen.
            if (orion_dsi_blit_rotated_ppa(full, kScreenW, kScreenH, ax, ay, aw, ah) != SGFX_OK) {
                const uint8_t* src = (const uint8_t*)full
                    + (size_t)ay * (size_t)kScreenW * sizeof(lv_color_t)
                    + (size_t)ax * sizeof(lv_color_t);
                sgfx_blit(d, ax, ay, aw, ah, SGFX_FMT_RGB565, src,
                          (size_t)kScreenW * sizeof(lv_color_t));
            }
            int64_t tBlit1 = esp_timer_get_time();
            s_flushCalls++;
            // Request the hardware buffer swap and return WITHOUT waiting
            // for it (orion_dsi_present_kick(), not the old blocking
            // sgfx_present()) -- the real fix for this file's "scroll is
            // laggy / multi-line output takes forever" history. The removed
            // forced-catch-up-per-append regression (see
            // forceDirectModeSyncCatchUp()'s header comment) fixed one
            // instance of excessive flush *frequency*; this fixes the
            // underlying reason each flush was so expensive to SHARE a lock
            // over in the first place: lvTimerTask used to hold s_lvglLock
            // for the ENTIRE duration of lv_timer_handler(), including
            // sgfx_present()'s real, vsync-synced wait (confirmed up to a
            // 200-tick/"200ms worst case" block) -- every other task's
            // appendOutput()/etc. (cheap LVGL mutations) had to queue up
            // behind that one task's hardware wait, one flush at a time.
            // Measured: 80 rapid appends took ~23s, ~287ms each, matching
            // "one full vsync wait per append" almost exactly.
            //
            // Splitting kick (fast: a cache sync + a non-blocking
            // draw_bitmap request, confirmed by reading
            // esp_lcd_panel_draw_bitmap()'s real semantics for the
            // buffer-switch case in espidf_dsi.c) from wait (the actual
            // vsync-bounded block) means lvTimerTask's lock hold for THIS
            // flush is now just the kick's cost, not the wait's -- the wait
            // itself happens entirely in dsiFlipWaitTask, a separate task
            // that holds s_lvglLock only for the few LVGL-API instructions
            // around calling lv_disp_flush_ready(), not across the wait.
            // REVERTED (attempt #6 -> back to #4's synchronous shape),
            // 2026-10-19: the kick/dsiFlipWaitTask split below deadlocked
            // on real hardware under BITPIRATE_RUN_ASYNC_FLUSH_STRESS_TEST
            // within ~15s (task_wdt: lv_timer stuck in LVGL's own
            // refr_area_part, lv_refr.c:702/711 -- `while(draw_buf->flushing)
            // { if(wait_cb) wait_cb(...); }`, no yield since no wait_cb was
            // configured, with IDLE1 on CPU1 never scheduled again).
            // Root cause: lvTimerTask holds s_lvglLock for the WHOLE
            // lv_timer_handler() call (unchanged by this split -- the split
            // only shortened what dispFlushCb itself does, not LVGL's own
            // internal re-entry into refr_area_part on a later area/tick,
            // which also spins on draw_buf->flushing). dsiFlipWaitTask can
            // only clear `flushing` by calling lv_disp_flush_ready(), which
            // needs s_lvglLock -- held by the very task spinning on
            // `flushing` never releasing it. Classic AB-BA deadlock. A real
            // fix needs lv_timer_handler() to NOT hold the lock across its
            // own internal flushing-wait, which reopens the cross-task
            // LVGL corruption risk s_lvglLock exists to prevent -- not a
            // redesign to attempt under time pressure with a hung device
            // in front of you. Reverted to synchronous kick+wait (identical
            // ordering to the original pre-split orion_dsi_present(), per
            // that function's own refactor-to-compose comment) so the lock
            // is held across the full flush again, exactly as attempt #4
            // (proven correct, just slow) did. dsiFlipWaitTask/the
            // semaphore handoff are disabled, not deleted, for whoever
            // next attempts the lock-scoping redesign this actually needs.
            int64_t tKick0 = esp_timer_get_time();
            int kickRc = orion_dsi_present_kick();
            int64_t tKick1 = esp_timer_get_time();
            if (kickRc == 0) {
                orion_dsi_present_wait(200);
                s_presentCalls++;
            } else if (kickRc != 1) {
                ESP_LOGE(TAG, "orion_dsi_present_kick failed: %d", kickRc);
            }
            int64_t tWait1 = esp_timer_get_time();
            ESP_LOGI(TAG, "dispFlushCb timing: blit=%lldus kick=%lldus wait=%lldus total=%lldus",
                     (long long)(tBlit1 - tBlit0), (long long)(tKick1 - tKick0),
                     (long long)(tWait1 - tKick1), (long long)(tWait1 - tBlit0));
            lv_disp_flush_ready(drv);
            return;
        }
    }
    lv_disp_flush_ready(drv);
}

// Waits for a kick dispFlushCb just made, blocks on the real vsync-bounded
// hardware confirmation WITHOUT holding s_lvglLock across that wait (the
// whole point -- see dispFlushCb's comment), then takes the lock only for
// the brief, non-blocking lv_disp_flush_ready() call LVGL needs to proceed
// to its next cycle. orion_dsi_present_wait() itself touches no LVGL state
// at all (it's display.c's own front/back buffer bookkeeping), so it is
// deliberately called OUTSIDE the guard below, not inside it.
void dsiFlipWaitTask(void*) {
    for (;;) {
        xSemaphoreTake(s_flushKickedSem, portMAX_DELAY);
        orion_dsi_present_wait(200);
        s_presentCalls++;
        {
            LvglGuard guard;
            lv_disp_flush_ready(&s_dispDrv);
        }
    }
}

void indevReadCb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    (void)drv;
    bp_touch_point_t pts[4];
    int n = bp_touch_read_points(pts, 4);
    if (n > 0 && pts[0].pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = pts[0].x;
        data->point.y = pts[0].y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void lvTickTask(void*) {
    const uint32_t tick_ms = 5;
    for (;;) {
        lv_tick_inc(tick_ms);
        vTaskDelay(pdMS_TO_TICKS(tick_ms));
    }
}

void lvTimerTask(void*) {
    uint32_t statsMs = 0;
    for (;;) {
        {
            LvglGuard guard;
            lv_timer_handler();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        statsMs += 10;
        if (statsMs >= 2000) {
            statsMs = 0;
            uint32_t flushes  = s_flushCalls;
            uint32_t presents = s_presentCalls;
            s_flushCalls = 0;
            s_presentCalls = 0;
            ESP_LOGI(TAG, "perf: flush_cb=%u/2s (%.1f/s)  present=%u/2s (%.1f/s)  ratio=%.2f",
                     (unsigned)flushes, flushes / 2.0f,
                     (unsigned)presents, presents / 2.0f,
                     flushes ? (float)presents / (float)flushes : 0.0f);
        }
    }
}

// ---- Terminal renderer helpers ---------------------------------------

// PATCHED (not upstream): was LV_FONT_DEFAULT (Montserrat, proportional --
// garbles the ASCII banner and any fixed-width columns). This font is
// generated by fonts/gen_font.py from DejaVu Sans Mono -- real monospace
// glyph shapes, unlike LVGL's own unscii_16 (also tried, still garbled the
// banner: its retro pixel-art glyphs don't form FIGlet letters right).
extern "C" const lv_font_t lv_font_pirate_mono;
const lv_font_t* termFont() { return &lv_font_pirate_mono; }

int termRowH() {
    if (s_termRowH == 0) s_termRowH = lv_font_get_line_height(termFont()) + 4;
    return s_termRowH;
}

// Splits one logical line's markup text into wrapped visual rows, the same
// way LVGL's own lv_label_refr_text() does internally (same primitive,
// _lv_txt_get_next_line(), recolor-aware via LV_TEXT_FLAG_RECOLOR so
// "#RRGGBB text#" tag bytes don't count toward visual width) -- matches
// whatever lv_textarea used to produce for the same text/width. Returns the
// row count; if outByteStarts is given, fills it with each row's starting
// byte offset into `text` (the row's end is the next entry, or text.size()
// for the last row).
int termWrapLine(const std::string& text, const lv_font_t* font, lv_coord_t maxW,
                  std::vector<uint32_t>* outByteStarts) {
    if (outByteStarts) outByteStarts->clear();
    uint32_t pos = 0;
    const uint32_t len = (uint32_t)text.size();
    int rows = 0;
    do {
        if (outByteStarts) outByteStarts->push_back(pos);
        uint32_t next = _lv_txt_get_next_line(text.c_str() + pos, font, 0, maxW, nullptr, LV_TEXT_FLAG_RECOLOR);
        if (next == 0) next = 1; // defensive: never allowed to stall on real content
        pos += next;
        rows++;
    } while (pos < len);
    return rows;
}

// Redraws the terminal canvas from s_termLines, doing wrap/draw work only
// for the rows actually visible (bounded by the viewport height, typically
// ~15-20 rows) -- regardless of how many thousand lines of scrollback exist.
// This is the actual fix for "big help dump gets unusably slow": the widget
// it replaces, lv_textarea (built on lv_label), recomputes line-break
// layout for its ENTIRE accumulated text on every single
// lv_textarea_add_text() call -- confirmed by reading LVGL's own source
// (lv_textarea_add_text() -> lv_label_ins_text() -> lv_label_set_text(NULL)
// -> lv_label_refr_text(), which explicitly throws away its own long-text
// layout cache every time: "label->hint.line_start = -1 /*invalid if text
// changes*/" -- then rescans the whole buffer for line breaks and recolor
// tags). A "help all" dump issuing many sequential appendOutput() calls as
// the buffer grows toward its old 24KB cap paid that full-buffer cost on
// EVERY append, compounding across the dump. Orion doesn't hit this at all
// -- it has no LVGL/accumulated-text widget in the first place, just draws
// directly into the framebuffer and scrolls.
void termRender() {
    if (!s_termCanvas) return;

    const int viewH = lv_obj_get_height(s_termCanvas);
    const int viewW = lv_obj_get_width(s_termCanvas);


    // Vertical gradient (kColorBg -> kColorBgDeep), a faithful approximation
    // of the web UI's own 145deg one -- same reasoning the textarea this
    // replaces used, just hand-drawn in banded rects now instead of via
    // lv_obj's bg_grad_color style (a canvas's own pixels fully cover its
    // bounds, so that style layer would never actually show through).
    {
        lv_color_t top = lv_color_hex(kColorBg);
        lv_color_t bot = lv_color_hex(kColorBgDeep);
        constexpr int kBands = 16;
        lv_draw_rect_dsc_t rdsc;
        lv_draw_rect_dsc_init(&rdsc);
        for (int b = 0; b < kBands; ++b) {
            int y0 = (viewH * b) / kBands;
            int y1 = (viewH * (b + 1)) / kBands;
            rdsc.bg_color = lv_color_mix(bot, top, (lv_opa_t)(255 - (255 * b) / (kBands - 1)));
            rdsc.bg_opa = LV_OPA_COVER;
            lv_canvas_draw_rect(s_termCanvas, 0, y0, viewW, y1 - y0, &rdsc);
        }
    }

    const lv_font_t* font = termFont();
    const int rowH = termRowH();
    const int viewRows = (viewH + rowH - 1) / rowH;
    constexpr int kInnerPad = 4;
    constexpr int kScrollbarGutter = 10; // always reserved, whether or not the scrollbar is currently drawn, so wrap width doesn't shift when scrollback appears/disappears
    const lv_coord_t maxW = (lv_coord_t)(viewW - 2 * kInnerPad - kScrollbarGutter);

    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font  = font; // PATCHED: dsc_init() defaults this to LV_FONT_DEFAULT; was never overridden, so termFont() never actually affected drawing
    dsc.color = lv_color_hex(kColorGreen);
    dsc.flag  = LV_TEXT_FLAG_RECOLOR;

    // Walk backward from the newest logical line, wrapping just enough of
    // each one to find the rows that fall within [scroll, scroll+viewRows)
    // -- bounded by viewRows+scroll lines touched, never the full history.
    // Fetches one extra row when s_termScrollFracPx>0 -- the partial-row
    // shift below reveals a sliver of one more row at the top.
    struct VisRow { const std::string* line; uint32_t byteStart, byteEnd; };
    std::vector<VisRow> rows;
    rows.reserve(viewRows + 1);
    int collected = 0;
    const int need = viewRows + s_termScrollRows + (s_termScrollFracPx > 0 ? 1 : 0);
    std::vector<uint32_t> starts;
    for (auto it = s_termLines.rbegin(); it != s_termLines.rend() && collected < need; ++it) {
        termWrapLine(it->text, font, maxW, &starts);
        for (int r = (int)starts.size() - 1; r >= 0 && collected < need; --r, ++collected) {
            if (collected < s_termScrollRows) continue;
            uint32_t s = starts[(size_t)r];
            uint32_t e = (r + 1 < (int)starts.size()) ? starts[(size_t)r + 1] : (uint32_t)it->text.size();
            rows.push_back({&it->text, s, e});
        }
    }

    int y = viewH - rowH + s_termScrollFracPx;
    for (const VisRow& row : rows) {
        std::string rowText = row.line->substr(row.byteStart, row.byteEnd - row.byteStart);
        lv_canvas_draw_text(s_termCanvas, kInnerPad, y, maxW, &dsc, rowText.c_str());
        y -= rowH;
        if (y < -rowH) break;
    }

    // Scrollbar -- lv_textarea's native one (gone now that this is a raw
    // canvas, not a scrollable lv_obj) had this; only drawn when there's
    // actually more than one screenful of content to scroll through.
    if (s_termTotalRows > viewRows) {
        constexpr int kSbWidth = 5;
        const int sbX = viewW - kSbWidth - 2;
        const int maxScroll = s_termTotalRows - viewRows;
        const int thumbH = LV_MAX(20, (viewH * viewRows) / s_termTotalRows);
        // s_termScrollRows==0 (pinned to newest) -> thumb at the BOTTOM of
        // the track; higher scroll (further back) -> thumb moves up. Uses
        // the sub-row pixel position so the thumb glides instead of
        // stepping once per row.
        const int scrollPx = s_termScrollRows * rowH + s_termScrollFracPx;
        const int maxScrollPx = maxScroll * rowH;
        const int thumbY = (viewH - thumbH) -
            (maxScrollPx > 0 ? ((viewH - thumbH) * scrollPx) / maxScrollPx : 0);

        lv_draw_rect_dsc_t sbdsc;
        lv_draw_rect_dsc_init(&sbdsc);
        sbdsc.bg_opa = LV_OPA_30;
        sbdsc.bg_color = lv_color_hex(kColorGreen);
        lv_canvas_draw_rect(s_termCanvas, sbX, 0, kSbWidth, viewH, &sbdsc); // track
        sbdsc.bg_opa = LV_OPA_70;
        lv_canvas_draw_rect(s_termCanvas, sbX, thumbY, kSbWidth, thumbH, &sbdsc); // thumb
    }

    lv_obj_invalidate(s_termCanvas);
}

// Max scroll position in pixels -- canvas-equivalent of lv_obj_get_scroll_bottom(),
// used by both the drag handler and the momentum timer to clamp consistently.
int termMaxScrollPx() {
    const int rowH = termRowH();
    const int viewRows = (lv_obj_get_height(s_termCanvas) + rowH - 1) / rowH;
    const int maxScroll = s_termTotalRows > viewRows ? s_termTotalRows - viewRows : 0;
    return maxScroll * rowH;
}

// Clamps an absolute pixel scroll position to [0, termMaxScrollPx()] and
// decomposes it into the row/frac pair termRender() draws from. Returns
// false if clamped, so the momentum timer knows to stop at a scroll bound
// rather than keep "pushing" against it.
bool termSetScrollPx(int px) {
    const int maxPx = termMaxScrollPx();
    bool clamped = false;
    if (px < 0) { px = 0; clamped = true; }
    if (px > maxPx) { px = maxPx; clamped = true; }
    const int rowH = termRowH();
    s_termScrollRows = px / rowH;
    s_termScrollFracPx = px % rowH;
    return !clamped;
}

void termMomentumTimerCb(lv_timer_t* timer) {
    (void)timer;
    s_termVelPxPerTick *= kTermMomentumDecay;
    if (fabsf(s_termVelPxPerTick) < kTermMomentumStopPxTick) {
        lv_timer_del(s_termMomentumTimer);
        s_termMomentumTimer = nullptr;
        return;
    }
    int curPx = s_termScrollRows * termRowH() + s_termScrollFracPx;
    bool inBounds = termSetScrollPx(curPx + (int)lroundf(s_termVelPxPerTick));
    termRender();
    if (!inBounds) { // ran into a scroll bound -- stop rather than "pressing" against it
        lv_timer_del(s_termMomentumTimer);
        s_termMomentumTimer = nullptr;
    }
}

void termStopMomentum() {
    if (s_termMomentumTimer) {
        lv_timer_del(s_termMomentumTimer);
        s_termMomentumTimer = nullptr;
    }
    s_termVelPxPerTick = 0.0f;
}

// Hand-rolled drag-to-scroll (a canvas has no scrollable content area for
// LVGL's native scroll to attach to), now pixel-smooth with release
// momentum -- same feel as the file browser list's native LVGL scroll.
void termCanvasEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        termStopMomentum(); // grabbing the content mid-fling cancels the fling, same as a real touch list
        s_termDragStartY = p.y;
        s_termLastDragY = p.y;
        s_termDragStartScrollPx = s_termScrollRows * termRowH() + s_termScrollFracPx;
        s_termVelPxPerTick = 0.0f;
        s_termDragging = true;
    } else if (code == LV_EVENT_PRESSING && s_termDragging) {
        // Drag down -> reveal older content, in raw pixels (not rowH-quantized).
        int newScrollPx = s_termDragStartScrollPx + (p.y - s_termDragStartY);
        int prevRows = s_termScrollRows, prevFrac = s_termScrollFracPx;
        termSetScrollPx(newScrollPx);
        if (s_termScrollRows != prevRows || s_termScrollFracPx != prevFrac) {
            termRender();
        }
        // EMA of this tick's delta, like LVGL's own scroll_throw_vect --
        // smooths out a single noisy sample right before release.
        int tickDelta = p.y - s_termLastDragY;
        s_termVelPxPerTick = (s_termVelPxPerTick + (float)tickDelta) / 2.0f;
        s_termLastDragY = p.y;
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_termDragging = false;
        if (fabsf(s_termVelPxPerTick) >= kTermMomentumMinStartPxTick && !s_termMomentumTimer) {
            s_termMomentumTimer = lv_timer_create(termMomentumTimerCb, 16, nullptr);
        } else {
            s_termVelPxPerTick = 0.0f;
        }
    }
}

// ---- Layout: two complete band positions (history footer vs. keyboard
// footer), applied by resizing/repositioning LVGL objects -- LVGL handles
// the actual redraw/clipping itself, no manual pixel math needed (unlike
// the hand-rolled SGFX version this replaces). ---------------------------

void applyLayout(bool kbVisible) {
    s_keyboardVisible = kbVisible;

    const int inputY = kbVisible ? kInputYKeyboard : kInputYHistory;
    const int outH   = kbVisible ? kOutputHKeyboard : kOutputHHistory;
    const int footerY = kbVisible ? kFooterYKeyboard : kFooterYHistory;

    lv_obj_set_height(s_termCanvas, outH);
    lv_obj_set_y(s_inputBar, inputY);

    if (kbVisible) {
        lv_obj_set_y(s_kb, footerY);
        lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_historyCont, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(s_historyCont, footerY);
        lv_obj_clear_flag(s_historyCont, LV_OBJ_FLAG_HIDDEN);
    }

    // lv_obj_set_height() doesn't apply synchronously -- the real coordinate
    // update is deferred to the next layout pass, so termRender()'s
    // lv_obj_get_height() below would otherwise read back the OLD height.
    // Confirmed on real hardware via trace logging: this was the actual
    // cause of the overlapping/ghosted redraw, not a cross-buffer sync gap.
    lv_obj_update_layout(s_termCanvas);

    // Auto-scroll the output panel to the bottom and re-render now that its
    // visible height (and therefore its viewport row count) just changed,
    // same "stay following the latest line" behavior appendOutput() already
    // gives it.
    termStopMomentum();
    s_termScrollRows = 0;
    s_termScrollFracPx = 0;
    termRender();
    // Catch-up on just s_termCanvas isn't enough here: this toggle also
    // moves/shows/hides s_kb, s_historyCont and s_inputBar, so the other
    // physical buffer needs ALL of those re-synced, not just the canvas
    // (same reasoning as tab5gui_notify_rotation_changed()'s whole-screen
    // invalidate below).
    lv_obj_t* scr = lv_scr_act();
    lv_obj_invalidate(scr);
    tab5gui::forceDirectModeSyncCatchUp(scr);
}

// ---- Event callbacks -----------------------------------------------------

void textZoneClickedCb(lv_event_t*) {
    applyLayout(true);
}

void sendBtnClickedCb(lv_event_t*) {
    pushChar('\n');
}

void aiBtnClickedCb(lv_event_t*) {
    // Hint only -- the real "ai" command (Tab5AiAssistant.cpp) does the work.
    tab5gui::appendOutput(
        "AI assistant: type 'ai key <API_KEY>' once (https://aistudio.google.com/apikey), "
        "then 'ai <question>', e.g. 'ai how do I read an AT24C eeprom'.\n");
}

// ---- Files overlay: a modal file browser for LittleFS / Tab5's own SD
// card. Scope is deliberately list + view + delete only -- no upload/
// download UI, since a touchscreen-only device has no meaningful local
// file source to pick an upload from or destination to send a download
// to (that flow already exists for real in the web UI, wired against
// the same LittleFsService class in WebUi.cpp). Directories ARE browsed
// into now (tapping one descends, a ".." row goes back up) -- s_filesCurrentDir
// tracks where the listing is rooted; delete still only applies to files,
// never directories (no recursive-delete UI).

bool filesEnsureLfs() {
    if (s_filesLfs.mounted()) return true;
    return s_filesLfs.begin(/*formatIfFail=*/true, /*readOnly=*/false);
}

bool filesSdCardAvailable() {
    const sd_t* sd = sic_sd(0);
    return sd && sd->v && sd->v->present(sd);
}

std::string filesRootDir() { return s_filesSourceIsSd ? "/sdcard" : "/"; }

std::string filesJoin(const std::string& dir, const std::string& name) {
    if (dir.back() == '/') return dir + name;
    return dir + "/" + name;
}

std::string filesParentDir(const std::string& dir) {
    size_t slash = dir.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return filesRootDir();
    std::string parent = dir.substr(0, slash);
    return parent.empty() ? filesRootDir() : parent;
}

void fileRowViewClickedCb(lv_event_t* e);
void fileRowDeleteClickedCb(lv_event_t* e);

void filesAddRow(const std::string& shownName, size_t size, bool isDir, const std::string& fullPath) {
    size_t idx = s_filesEntryPaths.size();
    s_filesEntryPaths.push_back(fullPath);
    s_filesEntryIsDir.push_back(isDir);

    lv_obj_t* row = lv_obj_create(s_filesListCont);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 92);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_column(row, kPad, 0);

    // View zone: a clickable CONTAINER wrapping a non-clickable label --
    // the same pattern s_textZone already uses, required because LVGL
    // labels aren't clickable by default (confirmed by reading lv_label.c
    // in an earlier pass; a bare label here would never receive the tap).
    lv_obj_t* viewZone = lv_obj_create(row);
    lv_obj_set_flex_grow(viewZone, 1);
    lv_obj_set_height(viewZone, 92);
    lv_obj_set_style_radius(viewZone, 8, 0);
    lv_obj_set_style_bg_color(viewZone, lv_color_hex(kColorInputBg), 0);
    lv_obj_set_style_border_width(viewZone, 1, 0);
    lv_obj_set_style_border_color(viewZone, lv_color_hex(kColorBorder), 0);
    lv_obj_clear_flag(viewZone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(viewZone, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(viewZone, (void*)(intptr_t)idx);
    lv_obj_add_event_cb(viewZone, fileRowViewClickedCb, LV_EVENT_CLICKED, nullptr);

    std::string shown = (shownName == "..")
        ? std::string("[ .. ]")
        : shownName + (isDir ? "/" : ("  (" + std::to_string(size) + " B)"));
    lv_obj_t* lbl = lv_label_create(viewZone);
    lv_label_set_text(lbl, shown.c_str());
    lv_obj_set_style_text_color(lbl, lv_color_hex(isDir ? kColorAccent : kColorGreen), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);

    // Delete: skipped entirely for directories (out of scope, see header
    // comment) -- no button at all, rather than a button that silently
    // does nothing on tap.
    if (!isDir) {
        lv_obj_t* delBtn = lv_btn_create(row);
        lv_obj_set_size(delBtn, 96, 92);
        lv_obj_set_style_radius(delBtn, 8, 0);
        lv_obj_set_style_bg_color(delBtn, lv_color_hex(kColorBg), 0);
        lv_obj_set_style_border_width(delBtn, 1, 0);
        lv_obj_set_style_border_color(delBtn, lv_color_hex(kColorFail), 0);
        lv_obj_set_user_data(delBtn, (void*)(intptr_t)idx);
        lv_obj_add_event_cb(delBtn, fileRowDeleteClickedCb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* delLbl = lv_label_create(delBtn);
        lv_label_set_text(delLbl, "DEL");
        lv_obj_set_style_text_color(delLbl, lv_color_hex(kColorFail), 0);
        lv_obj_center(delLbl);
    }
}

void filesShowEmptyNotice(const char* text, uint32_t color) {
    lv_obj_t* lbl = lv_label_create(s_filesListCont);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
}

void filesRefreshList() {
    lv_obj_clean(s_filesListCont);
    s_filesEntryPaths.clear();
    s_filesEntryIsDir.clear();

    if (s_filesCurrentDir != filesRootDir()) {
        filesAddRow("..", 0, /*isDir=*/true, filesParentDir(s_filesCurrentDir));
    }

    if (s_filesSourceIsSd) {
        DIR* d = opendir(s_filesCurrentDir.c_str());
        if (!d) {
            filesShowEmptyNotice("SD card not mounted -- run the 'sdcard' command first, or insert a card.", kColorPlaceholder);
            return;
        }
        struct dirent* de;
        while ((de = readdir(d)) != nullptr) {
            std::string name = de->d_name;
            if (name == "." || name == "..") continue;
            std::string full = filesJoin(s_filesCurrentDir, name);

            // d_type comes straight from the SAME FAT directory-entry read
            // readdir() just did (confirmed in esp-idf's vfs_fat.c:
            // vfs_fat_readdir_r() sets it from fat_dir->filinfo.fattrib, no
            // extra I/O) -- using it instead of a separate stat() call skips
            // a full redundant FAT directory walk per entry for directories,
            // which was the main cause of the SD listing feeling laggy next
            // to LittleFS's listing (LittleFsService::list() already gets
            // name/size/isDir from one openNextFile() pass, no second
            // lookup). Regular files still need one stat() for size -- FAT's
            // readdir doesn't surface fsize through the portable dirent API.
            if (de->d_type == DT_DIR) {
                filesAddRow(name, 0, true, full);
                continue;
            }
            struct stat st{};
            size_t size = 0;
            if (stat(full.c_str(), &st) == 0) size = (size_t)st.st_size;
            filesAddRow(name, size, false, full);
        }
        closedir(d);
    } else {
        if (!filesEnsureLfs()) {
            filesShowEmptyNotice("LittleFS mount failed.", kColorFail);
            return;
        }
        for (const auto& e : s_filesLfs.list(s_filesCurrentDir)) {
            filesAddRow(e.name, e.size, e.isDir, filesJoin(s_filesCurrentDir, e.name));
        }
    }

    // The ".." row (if any) doesn't count as real content -- a directory
    // holding only ".." is still "(empty)" from the user's perspective.
    const bool hasUpRow = s_filesCurrentDir != filesRootDir();
    if (s_filesEntryPaths.size() <= (hasUpRow ? 1u : 0u)) {
        filesShowEmptyNotice("(empty)", kColorPlaceholder);
    }
}

void filesUpdateTitle() {
    std::string label = s_filesSourceIsSd ? "Files -- SD Card" : "Files -- LittleFS";
    if (s_filesCurrentDir != filesRootDir()) label += "  " + s_filesCurrentDir;
    lv_label_set_text(s_filesTitleLbl, label.c_str());
}

void filesHide() {
    lv_obj_add_flag(s_filesOverlay, LV_OBJ_FLAG_HIDDEN);
}

void filesShow() {
    lv_obj_move_foreground(s_filesOverlay);
    lv_obj_clear_flag(s_filesOverlay, LV_OBJ_FLAG_HIDDEN);
    s_filesCurrentDir = filesRootDir(); // always reopen at the source's root, no stale browse state
    filesUpdateTitle();
    filesRefreshList();
}

void fileRowViewClickedCb(lv_event_t* e) {
    intptr_t idx = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (idx < 0 || idx >= (intptr_t)s_filesEntryPaths.size()) return;
    const std::string path = s_filesEntryPaths[(size_t)idx]; // copy: row/vector gets rebuilt below
    const bool isDir = s_filesEntryIsDir[(size_t)idx];

    if (isDir) {
        s_filesCurrentDir = path; // path is already the target dir (parent dir for the ".." row)
        filesUpdateTitle();
        filesRefreshList();
        return;
    }

    std::string content;
    bool ok = false;
    if (s_filesSourceIsSd) {
        FILE* f = fopen(path.c_str(), "rb");
        if (f) {
            char buf[2048];
            size_t n = fread(buf, 1, sizeof(buf), f);
            content.assign(buf, n);
            fclose(f);
            ok = true;
        }
    } else {
        ok = s_filesLfs.readAll(path, content);
        if (content.size() > 2048) content.resize(2048);
    }

    filesHide();

    if (!ok) {
        tab5gui::appendOutput("[FILES] Failed to read " + path + "\n");
        return;
    }

    // Escape literal '#' by doubling (LVGL recolor markup's own escape
    // convention, confirmed in an earlier pass) -- appendOutput()'s
    // `markup` parameter interprets bare '#' as a color-span delimiter,
    // and arbitrary file content has no reason to respect that. Control
    // bytes other than newline/tab are dropped rather than risking
    // garbled rendering from genuinely-binary content.
    std::string escaped;
    escaped.reserve(content.size());
    for (char c : content) {
        if (c == '#') escaped += "##";
        else if ((unsigned char)c >= 0x20 || c == '\n' || c == '\t') escaped += c;
    }
    tab5gui::appendOutput("[FILES] " + path + ":\n" + escaped + "\n");
}

void fileRowDeleteClickedCb(lv_event_t* e) {
    intptr_t idx = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (idx < 0 || idx >= (intptr_t)s_filesEntryPaths.size()) return;
    if (s_filesEntryIsDir[(size_t)idx]) return; // no delete button is ever built for dirs; defensive only
    const std::string path = s_filesEntryPaths[(size_t)idx];

    bool ok = s_filesSourceIsSd ? (remove(path.c_str()) == 0) : s_filesLfs.removeFile(path);
    tab5gui::appendOutput(std::string("[FILES] delete ") + path + ": " + (ok ? "OK" : "FAILED") + "\n");
    filesRefreshList(); // rebuilds the list in place; overlay stays open
}

void filesCloseClickedCb(lv_event_t*) {
    filesHide();
}

void filesSourceLfsClickedCb(lv_event_t*) {
    s_filesSourceIsSd = false;
    s_filesCurrentDir = filesRootDir();
    filesUpdateTitle();
    filesRefreshList();
}

void filesSourceSdClickedCb(lv_event_t*) {
    if (!filesSdCardAvailable()) {
        tab5gui::appendOutput("[FILES] SD card not detected -- insert a card and try again.\n");
        return;
    }
    s_filesSourceIsSd = true;
    s_filesCurrentDir = filesRootDir();
    filesUpdateTitle();
    filesRefreshList();
}

void filesBtnClickedCb(lv_event_t*) {
    filesShow();
}

void historyBtnClickedCb(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target(e);
    intptr_t idx = (intptr_t)lv_obj_get_user_data(btn);
    if (idx < 0 || idx >= (intptr_t)s_historyTexts.size()) return;
    for (char c : s_historyTexts[(size_t)idx]) pushChar(c);
}

// The on-screen keyboard is bound to a tiny, always-hidden scratch
// textarea (s_kbScratchTa) -- NOT the visible input-bar label. LVGL's own
// lv_keyboard_def_event_cb() only applies character/backspace/enter/arrow
// edits to a bound textarea at all (confirmed by reading
// lv_keyboard.c: everything past the mode-switch/CLOSE/OK checks returns
// immediately `if (keyboard->ta == NULL) return;`), so a bound scratch ta
// is required to get those events to fire -- we just derive our own
// character stream from its content, rather than letting it drive any
// visible widget directly (Tab5TerminalView owns what's actually shown,
// via setInputLineText(), fed by the SAME character stream this produces).
//
// LV_EVENT_READY fires on this ta for Enter (one-line mode) -- the '\n'
// lv_keyboard already appended to the scratch content is intentionally
// NOT re-pushed from the VALUE_CHANGED branch below (would double-handle
// the same keypress); this is the single source of truth for Enter.
//
// LV_EVENT_CANCEL fires on this ta (and on the keyboard object) when the
// dedicated "hide keyboard" key (LV_SYMBOL_KEYBOARD, bottom-left of the
// default text map) is pressed -- LVGL's own built-in dismiss signal, not
// a button-label string match we maintain ourselves.
void kbScratchEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* ta = lv_event_get_target(e);

    if (code == LV_EVENT_READY) {
        pushChar('\n');
        lv_textarea_set_text(ta, "");
        s_scratchLastLen = 0;
        return;
    }
    if (code == LV_EVENT_CANCEL) {
        lv_textarea_set_text(ta, "");
        s_scratchLastLen = 0;
        applyLayout(false);
        return;
    }
    // LV_EVENT_VALUE_CHANGED: typed char, backspace, or (ignored here,
    // see above) the '\n' Enter already appended.
    const char* txt = lv_textarea_get_text(ta);
    int len = (int)strlen(txt);
    if (len > s_scratchLastLen) {
        char c = txt[len - 1];
        if (c != '\n') pushChar(c);
    } else if (len < s_scratchLastLen) {
        pushChar('\b');
    }
    s_scratchLastLen = len;
}

lv_obj_t* makeChromeBtn(lv_obj_t* parent, const char* label, uint32_t textColor) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 150, kInputBarH);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kColorBg), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(kColorChrome), 0);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, lv_color_hex(textColor), 0);
    lv_obj_center(lbl);
    return btn;
}

void buildWidgetTree() {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(kColorBgDeep), 0);
    // The screen is a scrollable lv_obj by default. LVGL auto-scrolls a
    // focused widget's scrollable ancestor into view -- since focus lands
    // on s_kbScratchTa (bound to the keyboard), that default behavior was
    // auto-scrolling the WHOLE SCREEN, pushing every fixed-position band
    // (keyboard included) out of its intended place until manually
    // scrolled back. Every band here is explicitly positioned by
    // applyLayout()/buildWidgetTree() -- the screen itself must never
    // scroll. Only the terminal canvas keeps its own (intentional,
    // independent) scrollback (termCanvasEventCb()'s hand-rolled drag).
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ---- Output panel: custom lv_canvas renderer (termRender()), rounded/
    // bordered/shadow -- NOT lv_textarea; see termRender()'s header comment
    // for why (lv_textarea's per-append full-buffer relayout cost).
    s_termCanvas = lv_canvas_create(scr);
    lv_obj_set_pos(s_termCanvas, kPad, kPad);
    const uint32_t termBufBytes = LV_CANVAS_BUF_SIZE_TRUE_COLOR(kTermCanvasW, kTermCanvasBufH);
    s_termCanvasBuf = heap_caps_malloc(termBufBytes, MALLOC_CAP_SPIRAM);
    lv_canvas_set_buffer(s_termCanvas, s_termCanvasBuf, kTermCanvasW, kTermCanvasBufH, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_width(s_termCanvas, kTermCanvasW);
    lv_obj_set_height(s_termCanvas, kOutputHHistory); // applyLayout() resizes this dynamically; buffer itself stays fixed at kTermCanvasBufH and just gets cropped
    // Same CLICKABLE reasoning the old textarea's own comment documented
    // (lv_obj_hit_test() gates on it) -- needed for termCanvasEventCb()'s
    // drag-to-scroll to receive touches at all.
    lv_obj_add_flag(s_termCanvas, LV_OBJ_FLAG_CLICKABLE);
    // Narrow registration (not LV_EVENT_ALL) -- only the touch events
    // termCanvasEventCb() actually handles, avoiding any risk from internal
    // LVGL events (DRAW_MAIN, SIZE_CHANGED, etc.) reaching a callback that
    // assumes an active touch indev/point.
    lv_obj_add_event_cb(s_termCanvas, termCanvasEventCb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(s_termCanvas, termCanvasEventCb, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(s_termCanvas, termCanvasEventCb, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(s_termCanvas, termCanvasEventCb, LV_EVENT_PRESS_LOST, nullptr);
    lv_obj_set_style_radius(s_termCanvas, 12, 0);
    lv_obj_set_style_border_width(s_termCanvas, 1, 0);
    lv_obj_set_style_border_color(s_termCanvas, lv_color_hex(kColorBorder), 0);
    // Outer shadow (LVGL 8.x has no true inset-shadow mode) approximating
    // the CSS's "inset 0 0 5px #000" -- an honest approximation, not a
    // claim of pixel-identical CSS behavior.
    lv_obj_set_style_shadow_width(s_termCanvas, 12, 0);
    lv_obj_set_style_shadow_color(s_termCanvas, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_termCanvas, LV_OPA_60, 0);
    termRender(); // empty at boot, but establishes the gradient-filled background immediately

    // ---- Input bar: flex row (text zone grows, 3 fixed-width buttons),
    // mirrors webui's `.input-area`.
    s_inputBar = lv_obj_create(scr);
    lv_obj_remove_style_all(s_inputBar);
    lv_obj_set_pos(s_inputBar, kPad, kInputYHistory);
    lv_obj_set_size(s_inputBar, kScreenW - 2 * kPad, kInputBarH);
    lv_obj_set_style_bg_opa(s_inputBar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_inputBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_inputBar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_inputBar, kPad, 0);

    s_textZone = lv_obj_create(s_inputBar);
    lv_obj_set_flex_grow(s_textZone, 1);
    lv_obj_set_height(s_textZone, kInputBarH);
    lv_obj_set_style_radius(s_textZone, 10, 0);
    lv_obj_set_style_bg_color(s_textZone, lv_color_hex(kColorInputBg), 0);
    lv_obj_set_style_border_width(s_textZone, 1, 0);
    lv_obj_set_style_border_color(s_textZone, lv_color_hex(kColorBorder), 0);
    lv_obj_clear_flag(s_textZone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_textZone, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_textZone, textZoneClickedCb, LV_EVENT_CLICKED, nullptr);

    s_inputLabel = lv_label_create(s_textZone);
    lv_obj_set_style_text_color(s_inputLabel, lv_color_hex(kColorPlaceholder), 0);
    lv_label_set_text(s_inputLabel, "Enter command");
    lv_obj_align(s_inputLabel, LV_ALIGN_LEFT_MID, 12, 0);

    // AI/Send: plain text only, no icon -- explicit product decision (no
    // good built-in LVGL symbol matches "AI"/"Send" closely enough to be
    // worth using over text). Files: LV_SYMBOL_DIRECTORY (a real folder
    // icon, confirmed present in the compiled-in font's glyph table)
    // ahead of the label, mirroring webui's "📁 Files" icon+label pattern.
    lv_obj_t* aiBtn = makeChromeBtn(s_inputBar, "AI", kColorWarn);
    lv_obj_add_event_cb(aiBtn, aiBtnClickedCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* filesBtn = makeChromeBtn(s_inputBar, LV_SYMBOL_DIRECTORY " FILES", kColorWarn);
    lv_obj_add_event_cb(filesBtn, filesBtnClickedCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* sendBtn = makeChromeBtn(s_inputBar, "SEND", kColorGreen);
    lv_obj_add_event_cb(sendBtn, sendBtnClickedCb, LV_EVENT_CLICKED, nullptr);

    // ---- History strip: flex row, horizontally scrollable, mirrors
    // webui's `.history-area`. Rebuilt by setHistoryEntries() whenever the
    // recalled-command list changes. Scroll direction/flag are set
    // EXPLICITLY below -- lv_obj_create()'s default IS scrollable with
    // LV_DIR_ALL, but relying on that default for a flex-row strip that
    // must never scroll vertically (there's nothing above/below to reveal)
    // was fragile; pin it to horizontal-only and make the scrollbar visible
    // as a real affordance, matching the webui's own scrollable history
    // strip look.
    s_historyCont = lv_obj_create(scr);
    lv_obj_remove_style_all(s_historyCont);
    lv_obj_set_pos(s_historyCont, kPad, kFooterYHistory);
    lv_obj_set_size(s_historyCont, kScreenW - 2 * kPad, kFooterHistoryH);
    lv_obj_set_style_bg_opa(s_historyCont, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(s_historyCont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_historyCont, kPad, 0);
    lv_obj_add_flag(s_historyCont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_historyCont, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_historyCont, LV_SCROLLBAR_MODE_ACTIVE);
    // lv_obj_remove_style_all() above strips the theme's default scrollbar
    // appearance along with everything else, leaving it invisible (zero
    // width/opacity) -- style it explicitly, matching the web UI's own
    // .history-area::-webkit-scrollbar-thumb (#444, rounded, 6px).
    lv_obj_set_style_bg_color(s_historyCont, lv_color_hex(kColorChrome), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_historyCont, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_historyCont, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_height(s_historyCont, 6, LV_PART_SCROLLBAR);

    // ---- On-screen keyboard: a real, mature LVGL widget (lv_keyboard)
    // replacing the hand-rolled TouchKeyboard that had repeated hit-
    // testing/dismiss bugs -- rect-exact hit-testing, immediate press
    // feedback, and a dedicated dismiss key are all LVGL's own default
    // behavior here, not something this project re-implements.
    s_kbScratchTa = lv_textarea_create(scr);
    lv_obj_set_size(s_kbScratchTa, 10, 10);
    lv_obj_add_flag(s_kbScratchTa, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_kbScratchTa, LV_OBJ_FLAG_SCROLLABLE);
    lv_textarea_set_one_line(s_kbScratchTa, true);
    lv_textarea_set_text(s_kbScratchTa, "");
    lv_obj_add_event_cb(s_kbScratchTa, kbScratchEventCb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(s_kbScratchTa, kbScratchEventCb, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(s_kbScratchTa, kbScratchEventCb, LV_EVENT_CANCEL, nullptr);

    s_kb = lv_keyboard_create(scr);
    lv_keyboard_set_textarea(s_kb, s_kbScratchTa);
    lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    // lv_keyboard's OWN constructor (lv_keyboard.c) calls
    // lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, 0, 0) -- a PERSISTENT style
    // alignment, not a one-time position set. Left in place, every later
    // lv_obj_set_pos()/set_y() call only updates the style's x/y OFFSET,
    // which LVGL's layout pass then adds to a bottom-aligned base position
    // (y = y_offset + (parent_h - obj_h)) on every refresh -- confirmed by
    // reading lv_obj_pos.c's real recompute function and matching its
    // exact formula against logged runtime coordinates (290 + (720-420) =
    // 590, bit for bit what was observed: the keyboard's bottom edge
    // landing at 1010, overflowing far past the 720px screen -- this was
    // the real cause of the reported "overflow bottom, half hidden" bug).
    // Clear it so this object behaves like every other widget in this file
    // (plain absolute top-left positioning via set_pos/set_y).
    lv_obj_set_align(s_kb, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_kb, kPad, kFooterYKeyboard);
    lv_obj_set_size(s_kb, kScreenW - 2 * kPad, kFooterKeyboardH);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

    // ---- Bit Pirate theme for the keyboard's actual key buttons (was
    // LVGL's stock default skin) -- lv_keyboard is an lv_btnmatrix:
    // LV_PART_MAIN is the keyboard's own background/border, LV_PART_ITEMS
    // is each key, styled per-state (default / pressed / checked, the
    // last being shift/caps-lock's toggled-on look).
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(kColorBgDeep), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_kb, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_kb, lv_color_hex(kColorBorder), LV_PART_MAIN);
    lv_obj_set_style_radius(s_kb, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_kb, kPad, LV_PART_MAIN);

    lv_obj_set_style_bg_color(s_kb, lv_color_hex(kColorInputBg), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_kb, lv_color_hex(kColorBorder), LV_PART_ITEMS);
    lv_obj_set_style_radius(s_kb, 8, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_kb, lv_color_hex(kColorGreen), LV_PART_ITEMS);

    // Pressed: immediate visual feedback, cyan accent (same "press-down
    // highlight" principle established for every other touch surface in
    // this project).
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(kColorAccent), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(s_kb, lv_color_hex(kColorAccent), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(s_kb, lv_color_hex(kColorBgDeep), LV_PART_ITEMS | LV_STATE_PRESSED);

    // Checked: shift/caps/symbol-mode toggled "on" -- cyan accent border,
    // not a full fill, so it reads as "active" without looking identical
    // to the pressed-this-instant state above.
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(kColorBg), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(s_kb, lv_color_hex(kColorAccent), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(s_kb, 2, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(s_kb, lv_color_hex(kColorAccent), LV_PART_ITEMS | LV_STATE_CHECKED);

    // ---- Files overlay: modal, created last so lv_obj_move_foreground()
    // in filesShow() always has somewhere above everything else to go
    // (including the on-screen keyboard, which can be visible at the same
    // time this is opened).
    s_filesOverlay = lv_obj_create(scr);
    lv_obj_set_pos(s_filesOverlay, 60, 60);
    lv_obj_set_size(s_filesOverlay, kScreenW - 120, kScreenH - 120);
    lv_obj_set_style_bg_color(s_filesOverlay, lv_color_hex(kColorBg), 0);
    lv_obj_set_style_border_width(s_filesOverlay, 1, 0);
    lv_obj_set_style_border_color(s_filesOverlay, lv_color_hex(kColorBorder), 0);
    lv_obj_set_style_radius(s_filesOverlay, 12, 0);
    lv_obj_set_style_shadow_width(s_filesOverlay, 20, 0);
    lv_obj_set_style_shadow_color(s_filesOverlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_filesOverlay, LV_OPA_70, 0);
    // Unlike filesHeader/s_filesListCont below, this container never got
    // lv_obj_remove_style_all(), so it silently kept the default theme's
    // ~21px content padding on every side, stacked on top of the explicit
    // kPad offset filesHeader/s_filesListCont are positioned with below.
    // Real logged coordinates confirmed it: filesHeader landed at absolute
    // x=91 (expected 60+kPad=70), and filesInnerW's width never accounted
    // for that extra padding either, so the header row -- sized against an
    // effectively-too-wide "available" span -- pushed CLOSE's right edge
    // to x=1230, past the overlay's own right edge at x=1219. Zeroing the
    // padding here makes the overlay's content origin match what kPad-based
    // positioning below already assumes.
    lv_obj_set_style_pad_all(s_filesOverlay, 0, 0);
    lv_obj_clear_flag(s_filesOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_filesOverlay, LV_OBJ_FLAG_HIDDEN);

    const int filesInnerW = kScreenW - 120 - 2 * kPad;

    lv_obj_t* filesHeader = lv_obj_create(s_filesOverlay);
    lv_obj_remove_style_all(filesHeader);
    lv_obj_set_pos(filesHeader, kPad, kPad);
    lv_obj_set_size(filesHeader, filesInnerW, kTouchPrimary);
    lv_obj_set_style_bg_opa(filesHeader, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(filesHeader, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(filesHeader, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(filesHeader, kPad, 0);

    s_filesTitleLbl = lv_label_create(filesHeader);
    lv_obj_set_flex_grow(s_filesTitleLbl, 1);
    lv_obj_set_style_text_color(s_filesTitleLbl, lv_color_hex(kColorGreen), 0);
    lv_label_set_text(s_filesTitleLbl, "Files -- LittleFS");

    // Dedicated, isolated CLOSE button -- the specific structural lesson
    // from this project's earlier keyboard-dismiss bugs: a show/hide
    // control's hit-rect must never share space with anything else.
    lv_obj_t* lfsTabBtn = makeChromeBtn(filesHeader, "LITTLEFS", kColorAccent);
    lv_obj_add_event_cb(lfsTabBtn, filesSourceLfsClickedCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* sdTabBtn = makeChromeBtn(filesHeader, "SDCARD", kColorAccent);
    lv_obj_add_event_cb(sdTabBtn, filesSourceSdClickedCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* filesCloseBtn = makeChromeBtn(filesHeader, "CLOSE", kColorFail);
    lv_obj_add_event_cb(filesCloseBtn, filesCloseClickedCb, LV_EVENT_CLICKED, nullptr);

    s_filesListCont = lv_obj_create(s_filesOverlay);
    lv_obj_remove_style_all(s_filesListCont);
    lv_obj_set_pos(s_filesListCont, kPad, 2 * kPad + kTouchPrimary);
    lv_obj_set_size(s_filesListCont, filesInnerW, kScreenH - 120 - 3 * kPad - kTouchPrimary);
    lv_obj_set_style_bg_opa(s_filesListCont, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(s_filesListCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_filesListCont, 8, 0);

    applyLayout(false);
    tab5gui::setHistoryEntries({});

#if BITPIRATE_RUN_FILES_LAYOUT_DIAG
    {
        for (int i = 0; i < 12; i++) {
            char nm[40];
            snprintf(nm, sizeof(nm), "diag_test_file_%02d.bin", i);
            filesAddRow(nm, 1000 + i, false, std::string("/") + nm);
        }
        lv_obj_clear_flag(s_filesOverlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(s_filesOverlay);
        auto dump = [](const char* tag, lv_obj_t* o) {
            if (!o) { ESP_LOGW("filesdiag", "%s: null", tag); return; }
            ESP_LOGI("filesdiag", "%s: x=%d y=%d w=%d h=%d right=%d bottom=%d",
                     tag, lv_obj_get_x(o), lv_obj_get_y(o), lv_obj_get_width(o), lv_obj_get_height(o),
                     lv_obj_get_x(o) + lv_obj_get_width(o), lv_obj_get_y(o) + lv_obj_get_height(o));
        };
        auto dumpAbs = [](const char* tag, lv_obj_t* o) {
            if (!o) { ESP_LOGW("filesdiag", "%s: null", tag); return; }
            lv_area_t a;
            lv_obj_get_coords(o, &a);
            ESP_LOGI("filesdiag", "%s: ABS x1=%d y1=%d x2=%d y2=%d w=%d h=%d",
                     tag, (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2,
                     (int)(a.x2 - a.x1 + 1), (int)(a.y2 - a.y1 + 1));
        };
        bool lfsOk = filesEnsureLfs();
        ESP_LOGI("filesdiag", "LittleFS mount result: %s", lfsOk ? "OK" : "FAILED");
        ESP_LOGI("filesdiag", "screen bounds: %dx%d", kScreenW, kScreenH);
        dump("overlay", s_filesOverlay);
        dumpAbs("overlay_abs", s_filesOverlay);
        {
            // Header row + its 4 children, in ABSOLUTE screen coords --
            // this is the row the user reports CLOSE getting cropped on,
            // and the previous diag pass never looked at it at all.
            lv_obj_t* header = lv_obj_get_parent(s_filesTitleLbl);
            dumpAbs("filesHeader", header);
            dumpAbs("filesTitleLbl", s_filesTitleLbl);
            uint32_t hc = lv_obj_get_child_cnt(header);
            for (uint32_t i = 0; i < hc; i++) {
                lv_obj_t* child = lv_obj_get_child(header, i);
                char tag[32];
                snprintf(tag, sizeof(tag), "header_child[%u]", (unsigned)i);
                dumpAbs(tag, child);
            }
        }
        dumpAbs("listCont_abs", s_filesListCont);
        dump("listCont", s_filesListCont);
        ESP_LOGI("filesdiag", "listCont scrollable=%d scroll_mode=%d scroll_y=%d content_h=%d",
                 lv_obj_has_flag(s_filesListCont, LV_OBJ_FLAG_SCROLLABLE),
                 (int)lv_obj_get_scrollbar_mode(s_filesListCont),
                 lv_obj_get_scroll_y(s_filesListCont),
                 (int)lv_obj_get_content_height(s_filesListCont));
        uint32_t childCount = lv_obj_get_child_cnt(s_filesListCont);
        ESP_LOGI("filesdiag", "row count=%u", (unsigned)childCount);
        for (uint32_t i = 0; i < childCount; i++) {
            lv_obj_t* row = lv_obj_get_child(s_filesListCont, i);
            char rowtag[48];
            snprintf(rowtag, sizeof(rowtag), "row[%u]", (unsigned)i);
            dump(rowtag, row);
        }
        lv_obj_add_flag(s_filesOverlay, LV_OBJ_FLAG_HIDDEN);
    }
#endif
}

} // namespace

// ---- lv_mem_custom_* -- required C symbols for CONFIG_LV_MEM_CUSTOM=y,
// routing LVGL's object/style heap into PSRAM rather than fighting the
// ~274KB of internal RAM everything else budgets tightly. ----------------
extern "C" {
void* lv_mem_custom_alloc(size_t size)  { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM); }
void  lv_mem_custom_free(void* ptr)     { heap_caps_free(ptr); }
void* lv_mem_custom_realloc(void* ptr, size_t size) { return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM); }
}

namespace tab5gui {

void begin() {
    if (!orion_display_ready()) {
        ESP_LOGE(TAG, "display not ready, cannot start LVGL GUI");
        return;
    }

    s_lvglLock = xSemaphoreCreateRecursiveMutex();
    s_flushKickedSem = xSemaphoreCreateBinary();

    lv_init();

    // Two LVGL-side draw buffers (PSRAM, ~29MB free, 1.8MB each is
    // negligible): orion_dsi_present() already does a real hardware
    // double-buffered flip at the display-driver level (sgfx_hal_dsi_flip,
    // confirmed in display.c), completely independent of this -- LVGL's
    // own buf1/buf2 is a different, higher-level overlap: it lets LVGL
    // start computing the NEXT frame's dirty pixels into buf2 while
    // dispFlushCb() is still blocked on THIS frame's vsync-synced flip
    // (orion_dsi_present() blocks up to a 200-tick wait), instead of
    // stalling LVGL's render step on that wait with only one buffer.
    // MALLOC_CAP_DMA added alongside MALLOC_CAP_SPIRAM (wasn't needed before)
    // so PPA (orion_dsi_blit_rotated_ppa(), see dispFlushCb below) can read
    // these buffers via its own DMA transfer -- ESP32-P4's GDMA can address
    // PSRAM directly, this is additive capability, not a behavior change for
    // the existing CPU path.
    size_t fb_bytes = (size_t)kScreenW * kScreenH * sizeof(lv_color_t);
    s_fb1 = (lv_color_t*)heap_caps_malloc(fb_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    s_fb2 = (lv_color_t*)heap_caps_malloc(fb_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!s_fb1 || !s_fb2) {
        ESP_LOGE(TAG, "PSRAM alloc failed for %u-byte LVGL framebuffer(s)", (unsigned)fb_bytes);
        return;
    }
    lv_disp_draw_buf_init(&s_drawBuf, s_fb1, s_fb2, kScreenW * kScreenH);

    lv_disp_drv_init(&s_dispDrv);
    s_dispDrv.draw_buf     = &s_drawBuf;
    s_dispDrv.flush_cb     = dispFlushCb;
    s_dispDrv.hor_res      = kScreenW;
    s_dispDrv.ver_res      = kScreenH;
    // direct_mode=1 -- the PROVEN-correct, PROVEN-faster-or-equal setting
    // as of attempt #4 below. This has now been tried four ways; read this
    // before touching it a fifth time:
    //
    // 1. full_refresh=0 + plain partial rendering (original attempt):
    //    dispFlushCb() only blitted the current dirty sub-rect into
    //    whichever physical buffer display.c had as "back" that cycle.
    //    Since display.c's two physical scanout buffers alternate every
    //    present() (orion_dsi_present() flips s_fb to front and swaps in
    //    the OTHER physical buffer as the new back buffer), each one only
    //    ever received the blits that happened while IT was back --
    //    never the other's. Each buffer ended up an incomplete patchwork
    //    -> visible garbage on real hardware. Confirmed root cause; NOT a
    //    simpler bug like wrong buffer pointers (checked, was correct).
    // 2. full_refresh=1 (shipped after #1, THIS is what's active again):
    //    every dispFlushCb() call covers the entire screen, so whichever
    //    physical buffer is back gets fully repainted every cycle --
    //    correct regardless of which one it is. Gives up the "skip
    //    recomputing unchanged pixels" CPU saving (confirmed to make
    //    scrolling feel laggy -- LVGL recomputes the whole 1280x720
    //    canvas every refresh even for a one-line scroll), but a
    //    visually-broken garbage bug is strictly worse than being less
    //    CPU-efficient.
    // 3. direct_mode=1, no catch-up (tried, REVERTED at the time): LVGL
    //    8.3.11's built-in mechanism for exactly this situation --
    //    screen-sized buffers + an internal sync_areas list
    //    (lv_refr.c, refr_sync_areas()) that's supposed to keep both LVGL
    //    buffers a complete, correct frame at all times while only
    //    recomputing dirty widgets. REAL hardware testing found a
    //    reproducible ~59-67KB scattered mismatch between the two physical
    //    buffers after settling, concentrated in a FIXED, repeatable byte
    //    range. Root cause NOT isolated at the time; reverted rather than
    //    ship a demonstrated bug.
    // 4. direct_mode=1 + forced sync catch-up (CURRENT, shipped): the real
    //    root cause from #3, actually found this time via a temporary
    //    instrumented build of lv_refr.c logging every cycle's inv_areas/
    //    sync_areas (not guessed): refr_sync_areas()'s buffer_copy() that
    //    propagates a cycle's redraw into the OTHER physical buffer is
    //    only reached -- and more importantly only PUSHED OUT to the panel
    //    via dispFlushCb()'s blit+present -- on a SUBSEQUENT refresh cycle
    //    that ALSO has something marked dirty (refr_invalid_areas()
    //    returns immediately, before ever reaching flush_cb, when
    //    inv_p==0). If the display goes idle right after the LAST real
    //    content change (confirmed via the logged trace: several further
    //    cycles fired with the exact same already-correct area and
    //    nothing left to copy -- fine on their own -- but nothing forced
    //    one more cycle before the test's readback), whichever physical
    //    buffer didn't just receive that last fresh draw never gets the
    //    pending sync_area's copy flushed out to it, because nothing
    //    marks anything dirty to trigger that push. Fix: appendOutput()/
    //    clearOutput() now call forceDirectModeSyncCatchUp() (below) after
    //    their real work -- lv_refr_now(NULL) once (the normal cycle,
    //    forced immediately instead of waiting for the ~30ms timer
    //    period), then a manual re-invalidate + lv_refr_now(NULL) again
    //    (consumes the just-recorded sync_area AND has something dirty to
    //    push it out with). Verified with the same buffer-diff readback
    //    test used to find #3's bug: mismatch dropped from 67056/1843200
    //    bytes (span 391004, "LARGE/SCATTERED") to 704/1843200 bytes (span
    //    4496, "small/localized, consistent with cursor-blink phase") --
    //    the same tiny residual the full_refresh=1 baseline ALSO shows
    //    (measured both back to back, identical test), confirming it's
    //    normal cursor-blink variance, not a direct_mode artifact. Speed:
    //    measured the exact same 60-append stress workload both ways --
    //    full_refresh=1: 23117ms. direct_mode+catch-up: 20951ms -- ~9%
    //    FASTER even in this worst-case zero-gap-between-updates synthetic
    //    test (each append here pays 2 forced present()s instead of
    //    letting the timer batch multiple appends into fewer presents).
    //    For realistic, human-paced interaction (real gaps between
    //    updates, confirmed via idle perf counters settling to 0/2s
    //    between them), direct_mode should win by much more, since it
    //    skips full-1280x720-canvas recompute entirely between actual
    //    content changes -- full_refresh=1 never gets that benefit
    //    regardless of pacing. (#4's forceDirectModeSyncCatchUp() call site
    //    described above is now stale -- see #5.)
    // 5. (CURRENT, shipped) #4's per-append catch-up was itself found to
    //    regress badly for BURSTS of output (a `help` listing: dozens of
    //    println() calls, each paying 2 forced present()s serially ->
    //    measured 5 minutes for one help listing). Fixed first by deferring
    //    the catch-up to run at most once per burst (appendOutput()/
    //    clearOutput() now just set s_pendingSyncCatchUp; see
    //    drainPendingSyncCatchUp(), called from Tab5TouchInput::pollOnce()'s
    //    idle branch) -- but that alone left a DEEPER cost: lvTimerTask held
    //    s_lvglLock for the full duration of lv_timer_handler(), which
    //    reaches dispFlushCb's call into the old sgfx_present() -- a real,
    //    vsync-synced hardware wait (up to 200 ticks). Every OTHER task's
    //    cheap LVGL mutation (appendOutput() etc.) queued up behind that
    //    one task's hardware wait, one flush at a time -- measured 80 rapid
    //    appends at ~287ms EACH (~23s total) even after the per-burst fix,
    //    matching "one full vsync wait per append" almost exactly. Real fix:
    //    dispFlushCb now calls orion_dsi_present_kick() (fast: cache sync +
    //    a non-blocking draw_bitmap request) instead of the blocking
    //    sgfx_present(), and returns without marking the flush ready;
    //    dsiFlipWaitTask (a separate task) blocks on the actual vsync
    //    confirmation and calls lv_disp_flush_ready() itself once it has
    //    it -- so lvTimerTask's lock hold per flush is now just the kick's
    //    cost, not the wait's. See dispFlushCb/dsiFlipWaitTask's own
    //    comments for the full design and why it's still correct (the
    //    kick/wait split preserves the exact buffer-swap-safety ordering
    //    orion_dsi_present() always had, see display.h).
    s_dispDrv.full_refresh = 0;
    s_dispDrv.direct_mode = 1;
    lv_disp_drv_register(&s_dispDrv);

    lv_indev_drv_init(&s_indevDrv);
    s_indevDrv.type    = LV_INDEV_TYPE_POINTER;
    s_indevDrv.read_cb = indevReadCb;
    lv_indev_drv_register(&s_indevDrv);

    s_charQueue = xQueueCreate(64, sizeof(char));

    buildWidgetTree();

    xTaskCreate(lvTickTask, "lv_tick", 2048, NULL, 6, NULL);
    xTaskCreate(lvTimerTask, "lv_timer", 8192, NULL, 4, NULL);
    // dsiFlipWaitTask NOT spawned -- see the "REVERTED (attempt #6)" comment
    // in dispFlushCb. It deadlocked under real concurrent load; dispFlushCb
    // now waits synchronously again instead of handing off to this task.
    // Left defined, not deleted, for whoever next attempts the proper fix.

    ESP_LOGI(TAG, "LVGL GUI started: %dx%d, fb=%u bytes x2 PSRAM, direct_mode=%d full_refresh=%d",
             kScreenW, kScreenH, (unsigned)fb_bytes,
             (int)s_dispDrv.direct_mode, (int)s_dispDrv.full_refresh);
}

// direct_mode's cross-buffer sync (lv_refr.c's sync_areas/refr_sync_areas)
// only gets CONSUMED on the next refresh cycle, and that consumption is
// only pushed out to the physical panel (via dispFlushCb -> sgfx_blit +
// sgfx_present) if that same cycle also has something marked dirty to draw
// -- refr_invalid_areas() returns immediately, before ever touching
// flush_cb, when inv_p==0. Confirmed empirically (real area-list trace,
// not guessed): after the last real content change, several further
// refresh cycles can fire with the SAME already-correct invalidated area
// and genuinely nothing to copy -- that part is fine -- but if NOTHING
// forces a cycle at all before the display is read/considered settled,
// whichever physical buffer didn't just receive the fresh draw never gets
// the pending sync_area's buffer_copy flushed out to it. Forcing one
// extra, immediate, genuinely-redundant redraw of the same content (via a
// manual re-invalidate + lv_refr_now(), bypassing the normal ~30ms refresh
// timer period) guarantees that catch-up actually happens before this
// function returns, rather than depending on some unrelated future event
// (next cursor blink, next real content change) to accidentally trigger
// it. Cheap: the pixels are identical, only the flush+present cost is
// paid twice, not the widget redraw logic.
void forceDirectModeSyncCatchUp(lv_obj_t* obj) {
    if (!obj) return;
    lv_refr_now(NULL);
    lv_obj_invalidate(obj);
    lv_refr_now(NULL);
}

// extern "C" bridge for gui_rotation_bridge.h -- see that header for why
// this exists (display.c's rotation poll is plain C and can't include
// this file's own header to call appendOutput()-style helpers directly).
// Declaring extern "C" inside this namespace is fine -- it only affects
// linkage/name-mangling, not scoping, so s_termCanvas/LvglGuard (anonymous
// namespace, earlier in this file) and forceDirectModeSyncCatchUp (this
// namespace) all resolve via ordinary unqualified lookup exactly as they
// do in every other function in this file.
extern "C" void tab5gui_notify_rotation_changed(void) {
    if (!s_termCanvas) return; // widget tree not built yet (e.g. main.c's one-time boot-time set, before tab5gui::begin())
    LvglGuard guard;
    lv_obj_t* scr = lv_scr_act();
    lv_obj_invalidate(scr);
    forceDirectModeSyncCatchUp(scr);
}

// Hides the trace canvas (if currently shown) so the output panel underneath
// becomes visible again -- see registerTraceCanvas()'s declaration for why.
// Cheap no-op when no graph has ever been drawn (s_traceCanvas stays null)
// or it's already hidden.
void hideTraceCanvasIfShown() {
    if (s_traceCanvas && !lv_obj_has_flag(s_traceCanvas, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(s_traceCanvas, LV_OBJ_FLAG_HIDDEN);
    }
}

void registerTraceCanvas(void* canvas) {
    s_traceCanvas = static_cast<lv_obj_t*>(canvas);
}

void appendOutput(const std::string& markup) {
    if (!s_termCanvas) return;
    LvglGuard guard;
    hideTraceCanvasIfShown();

    size_t start = 0;
    while (true) {
        size_t nl = markup.find('\n', start);
        std::string seg = (nl == std::string::npos) ? markup.substr(start) : markup.substr(start, nl - start);

        if (s_termLines.empty()) s_termLines.push_back({});
        TermLine& last = s_termLines.back();
        s_termTotalRows -= last.rows;
        last.text += seg;
        last.rows = termWrapLine(last.text, termFont(), (lv_coord_t)(lv_obj_get_width(s_termCanvas) - 8), nullptr);
        s_termTotalRows += last.rows;

        if (nl == std::string::npos) break;
        s_termLines.push_back({}); // newline found -- close this line, start a fresh pending one
        start = nl + 1;
    }

    while (s_termLines.size() > kTermMaxLines) {
        s_termTotalRows -= s_termLines.front().rows;
        s_termLines.pop_front();
    }

    termStopMomentum();
    s_termScrollRows = 0; // same "stay following the latest line" behavior the old textarea had
    s_termScrollFracPx = 0;
    // termRender() itself is deferred to drainPendingSyncCatchUp(), NOT
    // called here -- same coalescing reasoning as the flush it used to pair
    // with 1:1. A big dump (e.g. "help all", hundreds of println() calls
    // inside ONE command, no yield back to the idle loop until it returns)
    // calls appendOutput() many times in a tight loop; redrawing the
    // viewport on every single one is pure waste when only the LAST state
    // before the eventual flush is ever actually seen. Confirmed on real
    // hardware: redrawing per-append here was still enough to trip the task
    // watchdog during "help all" (no yield for 5+ seconds) even though the
    // flush itself was already correctly coalesced to one at the end.
    s_pendingSyncCatchUp = true;
}

void clearOutput() {
    if (!s_termCanvas) return;
    LvglGuard guard;
    hideTraceCanvasIfShown();
    s_termLines.clear();
    s_termTotalRows = 0;
    termStopMomentum();
    s_termScrollRows = 0;
    s_termScrollFracPx = 0;
    s_pendingSyncCatchUp = true;
}

// Called from Tab5TouchInput::pollOnce()'s idle branch -- the natural
// "nothing happened this poll" point, reached within ~1ms of the last
// appendOutput()/clearOutput() in a burst, but only acted on once (the
// flag is cleared immediately), not on every idle poll afterward. Does the
// ACTUAL termRender() (deferred from every individual append/clear, see
// their own comments) before the flush, so a whole burst pays for exactly
// one redraw instead of one per line.
void drainPendingSyncCatchUp() {
    if (!s_pendingSyncCatchUp || !s_termCanvas) return;
    s_pendingSyncCatchUp = false;
    LvglGuard guard;
    termRender();
    forceDirectModeSyncCatchUp(s_termCanvas);
}

void lvglLockAcquire() { xSemaphoreTakeRecursive(s_lvglLock, portMAX_DELAY); }
void lvglLockRelease() { xSemaphoreGiveRecursive(s_lvglLock); }

void setInputLineText(const std::string& text) {
    if (!s_inputLabel) return;
    LvglGuard guard;
    if (text.empty()) {
        lv_obj_set_style_text_color(s_inputLabel, lv_color_hex(kColorPlaceholder), 0);
        lv_label_set_text(s_inputLabel, "Enter command");
    } else {
        lv_obj_set_style_text_color(s_inputLabel, lv_color_hex(kColorGreen), 0);
        lv_label_set_text(s_inputLabel, text.c_str());
    }
}

void setHistoryEntries(const std::vector<std::string>& entries) {
    if (!s_historyCont) return;
    LvglGuard guard;
    s_historyTexts = entries;
    lv_obj_clean(s_historyCont);

    if (entries.empty()) {
        lv_obj_t* hint = lv_label_create(s_historyCont);
        lv_label_set_text(hint, "(recent commands appear here)");
        lv_obj_set_style_text_color(hint, lv_color_hex(kColorBorder), 0);
        return;
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        std::string label = entries[i].size() > 18 ? entries[i].substr(0, 17) + "~" : entries[i];
        lv_obj_t* btn = lv_btn_create(s_historyCont);
        lv_obj_set_height(btn, kFooterHistoryH - 2 * kPad);
        lv_obj_set_width(btn, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(kColorBg), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(kColorBorder), 0);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, historyBtnClickedCb, LV_EVENT_CLICKED, nullptr);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, label.c_str());
        lv_obj_set_style_text_color(lbl, lv_color_hex(kColorGreen), 0);
        lv_obj_center(lbl);
    }

    // Force immediate layout of the LV_SIZE_CONTENT buttons just built --
    // same fix the files overlay already needed (see filesShow()'s own
    // lv_obj_update_layout() call): under direct_mode, SIZE_CONTENT
    // children in a freshly (re)built flex row aren't guaranteed to have
    // correct widths/positions (hence correct scrollable content width)
    // until a layout pass runs, which would otherwise only happen on some
    // later, unrelated redraw.
    lv_obj_update_layout(s_historyCont);
}

QueueHandle_t charQueue() {
    return s_charQueue;
}

} // namespace tab5gui
