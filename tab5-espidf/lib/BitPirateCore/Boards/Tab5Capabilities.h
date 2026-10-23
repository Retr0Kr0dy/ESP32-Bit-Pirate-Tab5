#pragma once

// Shared SoC-capability flags for P4/Tab5 vs S3, replacing scattered
// #if CONFIG_IDF_TARGET_ESP32P4 checks across BitPirateCore. Naming matches
// the official p4-tab5 branch's own Tab5Capabilities.h for consistency.

#include "sdkconfig.h"
#include "soc/soc_caps.h"

#if defined(SOC_WIFI_SUPPORTED) && SOC_WIFI_SUPPORTED
#define BP_HAS_NATIVE_WIFI_RADIO 1
#else
#define BP_HAS_NATIVE_WIFI_RADIO 0
#endif

#if defined(SOC_BT_SUPPORTED) && SOC_BT_SUPPORTED
#define BP_HAS_NATIVE_BLE_CONTROLLER 1
#else
#define BP_HAS_NATIVE_BLE_CONTROLLER 0
#endif

// RTC_CNTL_OPTION1_REG/FORCE_DOWNLOAD_BOOT (SystemService's
// rebootToBootloader()) doesn't exist on P4 -- no soc/rtc_cntl_reg.h at all
// for that target, confirmed against esp-idf's own esp32p4 soc headers.
#if CONFIG_IDF_TARGET_ESP32P4
#define BP_HAS_FORCE_DOWNLOAD_BOOT 0
#else
#define BP_HAS_FORCE_DOWNLOAD_BOOT 1
#endif

// P4 has no UART_SCLK_APB in its clock tree (confirmed against esp-idf's
// soc/esp32p4/include/soc/clk_tree_defs.h); PLL_F80M is its default equivalent.
#if CONFIG_IDF_TARGET_ESP32P4
#define BP_UART_SCLK_SOURCE UART_SCLK_PLL_F80M
#else
#define BP_UART_SCLK_SOURCE UART_SCLK_APB
#endif
