/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * This file is part of the TinyUSB stack.
 */

#ifndef TUSB_H_
#define TUSB_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------+
// INCLUDE
//--------------------------------------------------------------------+
#include "common/tusb_common.h"
#include "osal/osal.h"
#include "common/tusb_fifo.h"

//------------- TypeC -------------//
#if CFG_TUC_ENABLED
  #include "typec/usbc.h"
#endif

//------------- HOST -------------//
#if CFG_TUH_ENABLED
  #include "host/usbh.h"

  #if CFG_TUH_HID
    #include "class/hid/hid_host.h"
  #endif

  #if CFG_TUH_MSC
    #include "class/msc/msc_host.h"
  #endif

  #if CFG_TUH_CDC
    #include "class/cdc/cdc_host.h"
  #endif

  #if CFG_TUH_MIDI
    #include "class/midi/midi_host.h"
  #endif

  #if CFG_TUH_MIDI2
    #include "class/midi/midi2_host.h"
  #endif

  #if CFG_TUH_VENDOR
    #include "class/vendor/vendor_host.h"
  #endif
#else
  #ifndef tuh_int_handler
  #define tuh_int_handler(...)
  #endif
#endif

//------------- DEVICE -------------//
#if CFG_TUD_ENABLED
  #include "device/usbd.h"

  #if CFG_TUD_HID
    #include "class/hid/hid_device.h"
  #endif

  #if CFG_TUD_CDC
    #include "class/cdc/cdc_device.h"
  #endif

  #if CFG_TUD_MSC
    #include "class/msc/msc_device.h"
  #endif

  #if CFG_TUD_PRINTER
    #include "class/printer/printer_device.h"
  #endif

  #if CFG_TUD_MTP
    #include "class/mtp/mtp_device.h"
  #endif

  #if CFG_TUD_AUDIO
    #include "class/audio/audio_device.h"
  #endif

  #if CFG_TUD_VIDEO
    #include "class/video/video_device.h"
  #endif

  #if CFG_TUD_MIDI
    #include "class/midi/midi_device.h"
  #endif

  #if CFG_TUD_MIDI2
    #include "class/midi/midi2_device.h"
  #endif

  #if CFG_TUD_VENDOR
    #include "class/vendor/vendor_device.h"
  #endif

  #if CFG_TUD_USBTMC
    #include "class/usbtmc/usbtmc_device.h"
  #endif

  #if CFG_TUD_DFU_RUNTIME
    #include "class/dfu/dfu_rt_device.h"
  #endif

  #if CFG_TUD_DFU
    #include "class/dfu/dfu_device.h"
  #endif

  #if CFG_TUD_ECM_RNDIS || CFG_TUD_NCM
    #include "class/net/net_device.h"
  #endif

  #if CFG_TUD_BTH
    #include "class/bth/bth_device.h"
  #endif
#else
  #ifndef tud_int_handler
  #define tud_int_handler(...)
  #endif
#endif


//--------------------------------------------------------------------+
// Application API
//--------------------------------------------------------------------+

// Following macros are defined to be backward compatible
// tinyusb 0.17.x and 0.18.x
#define tusb_rhport_teardown(rhport)  tusb_deinit(rhport)
// PATCHED (not upstream): was tusb_deinit(0), hardcoding rhport 0. Wrong for
// any CFG_TUSB_RHPORT1_MODE target (ESP32-P4's OTG-HS, this project's only
// configured device port) -- same class of bug already patched in
// class/hid/hid_device.c's tud_hid_n_ready()/tud_hid_n_report() (see that
// file's own comment). tusb_deinit(rhport) checks
// _tusb_rhport_role[rhport] == TUSB_ROLE_DEVICE before doing anything
// (tusb.c) -- with the device actually on rhport 1, tusb_deinit(0) found
// nothing to tear down and returned false, which esp_tinyusb's
// tinyusb_driver_uninstall() (tinyusb.c) surfaces as "Unable to teardown
// TinyUSB" and then refuses to free the USB PHY -- confirmed on real
// hardware: switching from USB device mode to USB host mode left the PHY
// "in use" (usb_new_phy() -> ESP_ERR_INVALID_STATE), permanently blocking
// host mode until reboot. TUD_OPT_RHPORT is the correct, already-existing
// portable macro (tusb_option.h) for "whichever rhport is actually
// configured as the device port" -- this project has no host-mode tinyusb
// role at all (USB host mode here uses raw usb/usb_host.h, not tinyusb's
// tuh_* stack), so tusb_teardown() only ever needs to handle the device
// case.
#define tusb_teardown()               tusb_deinit(TUD_OPT_RHPORT)

#if CFG_TUH_ENABLED || CFG_TUD_ENABLED

// Internal helper for backward compatible with tusb_init(void)
bool tusb_rhport_init(uint8_t rhport, const tusb_rhport_init_t* rh_init);

// Initialize roothub port with device/host role
// Note: when using with RTOS, this should be called after scheduler/kernel is started.
//       Since USB IRQ handler does use RTOS queue API.
// Note2: defined as macro for backward compatible with tusb_init(void), can be changed to function in the future.
#if defined(TUD_OPT_RHPORT) || defined(TUH_OPT_RHPORT)
  #define _tusb_init_arg0()        tusb_rhport_init(0, NULL)
#else
  #define _tusb_init_arg0()        TU_VERIFY_STATIC(false, "CFG_TUSB_RHPORT0_MODE/CFG_TUSB_RHPORT1_MODE must be defined")
#endif

#define _tusb_init_arg1(_rhport)             _tusb_init_arg0()
#define _tusb_init_arg2(_rhport, _rh_init)   tusb_rhport_init(_rhport, _rh_init)
#define tusb_init(...)                       TU_FUNC_OPTIONAL_ARG(_tusb_init, __VA_ARGS__)

// Check if stack is initialized
bool tusb_inited(void);

// Called to handle usb interrupt/event. tusb_init(rhport, role) must be called before
void tusb_int_handler(uint8_t rhport, bool in_isr);

// Deinit usb stack on roothub port
bool tusb_deinit(uint8_t rhport);

#else

#define tusb_init(...)  (false)
#define tusb_int_handler(...)  do {}while(0)
#define tusb_inited()  (false)
#define tusb_deinit(...) (false)

#endif

#ifdef __cplusplus
 }
#endif

#endif /* TUSB_H_ */
