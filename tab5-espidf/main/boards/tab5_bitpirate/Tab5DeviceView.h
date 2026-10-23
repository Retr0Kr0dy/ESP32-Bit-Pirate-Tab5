#pragma once

/*
 * Tab5DeviceView — IDeviceView. This project's real interaction model is
 * 100% text (see Tab5Gui.h's header comment: the real Bit Pirate web UI
 * and serial terminal pair with NoScreenDeviceView -- there is no
 * graphical menu/pinout widget in the actual product at all). show()/
 * loading()/adapterMode()/topBar()/horizontalSelection() stay INERT
 * no-ops for that reason -- checked directly, their one real call site
 * (DioController.cpp's show()) already prints the same info as text right
 * next to it, and a full-screen takeover here would clobber Tab5Gui's
 * persistent LVGL layout for no benefit.
 *
 * drawLogicTrace()/drawAnalogicTrace()/drawWaterfall() are DIFFERENT: the
 * original S3 reference (St7789SpiDeviceView.cpp) treats these as real,
 * meaningful visuals (a digital step trace, an analog plot, a scrolling
 * RF waterfall) with no text equivalent that conveys the same thing -- so
 * these three are implemented for real here, via an LVGL lv_canvas sized
 * to sit in the output panel's own screen region (shown/hidden the same
 * way the file-browser overlay is, see Tab5Gui.cpp), not left as stubs.
 *
 * getSharedSpiInstance(): Tab5's display is MIPI-DSI, not SPI, so there is
 * no display bus to share. Returns a lazily-constructed SPIClass bound to
 * the M-Bus SPI lines (MOSI=GPIO18, MISO=GPIO19, SCK=GPIO5).
 */

#include "Interfaces/IDeviceView.h"

class Tab5DeviceView : public IDeviceView {
public:
    Tab5DeviceView() = default;
    ~Tab5DeviceView() override = default;

    void initialize() override;
    SPIClass& getSharedSpiInstance() override;
    void* getScreen() override;
    void logo() override;
    void welcome(TerminalTypeEnum& terminalType, std::string& terminalInfos) override;
    void show(PinoutConfig& config) override;
    void loading() override;
    void adapterMode(const std::string& adapterName, const std::string& description, const std::vector<std::string>& details) override;
    void clear() override;
    void drawLogicTrace(uint8_t pin, const std::vector<uint8_t>& buffer, uint8_t step) override;
    void drawAnalogicTrace(uint8_t pin, const std::vector<uint8_t>& buffer, uint8_t step) override;
    void drawWaterfall(
        const std::string& title,
        float startValue,
        float endValue,
        const char* unit,
        int rowIndex,
        int rowCount,
        int level
    ) override;
    void setRotation(uint8_t rotation) override;
    void setBrightness(uint8_t brightness) override;
    uint8_t getBrightness() override;
    void topBar(const std::string& title, bool submenu, bool searchBar) override;
    void horizontalSelection(
        const std::vector<std::string>& options,
        uint16_t selectedIndex,
        const std::string& description1,
        const std::string& description2) override;

private:
    SPIClass* sharedSpi_ = nullptr;

    // Lazily-created LVGL canvas backing drawLogicTrace()/drawAnalogicTrace()/
    // drawWaterfall() -- real graphical rendering, see Tab5DeviceView.cpp's
    // header comment for why these are no longer inert. Allocated once (PSRAM,
    // same pattern as Tab5Gui.cpp's own LVGL framebuffers), reused across calls.
    void ensureTraceCanvas();
    void showTraceCanvas();
    void* traceCanvas_ = nullptr;   // lv_obj_t*, void* to avoid pulling lvgl.h into this header
    void* traceCanvasBuf_ = nullptr;
};
