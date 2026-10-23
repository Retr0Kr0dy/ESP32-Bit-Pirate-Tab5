#pragma once
#include <vector>
#include <string>

// =============================================================
#ifndef UART_AT_EMOJI
  #define UART_AT_EMOJI 1
#endif
#if UART_AT_EMOJI
  #define E(x) x
#else
  #define E(x) ""
#endif

#ifndef AT_COUNT_OF
  #define AT_COUNT_OF(a) (sizeof(a)/sizeof((a)[0]))
#endif
#ifndef AT_NOARGS
  #define AT_NOARGS nullptr, 0
#endif
#ifndef AT_ACTION_OF
    #define AT_ACTIONS_OF(arr) AtActionSlice{ (arr), AT_COUNT_OF(arr) }
#endif

// =============================================================

enum class AtStandard { Hayes, ThreeGPP, SIMCom, Quectel };

enum class AtArgType {
    String,     // free text
    Uint,       // unsigned integer
    Int,        // signed integer
    Bool01,     // 0/1
    Phone,      // +33....
    HexBytes,   // "01 AA 03 ..."
    Choice,     // must be in 'choices' (pipe-separated)
    Regex       // must match 'pattern'
};

struct AtActionArg {
    const char* name;           // prompt label
    bool        required;
    const char* defaultValue;   // default if user enters empty (optional)
    const char* hint;           // help text / example

    // typing + optional constraints
    AtArgType   type;
    long long   minValue;
    long long   maxValue;
    const char* choices;
    const char* pattern;
};

struct AtActionItem {
    const char*  emoji;
    const char*  label;
    const char*  command;
    AtStandard   standard;
    const char*  example;
    bool         destructive;
    const AtActionArg* args;
    size_t       args_count;
};

enum class AtMode {
    Config, Info, SimPin, Network, Calls, Sms, Phonebook, Ussd, Data, TimeClock,
    TcpIp_Vendor, Http_Vendor, Gnss_Vendor, Lora, Audio, Power, Hardware, Mqtt, Files
};

struct AtModeOption {
    const char* emoji;
    const char* name;
    AtMode      mode;
};


struct AtActionSlice {
    const AtActionItem* data;
    std::size_t         size;
};

// ================== Modes ==================

inline constexpr AtModeOption kAtModes[] = {
    { E("🔧"), "Config",                 AtMode::Config },
    { E("📌"), "Info",                   AtMode::Info },
    { E("🔐"), "SIM & PIN",               AtMode::SimPin },
    { E("📶"), "Telephony Network",       AtMode::Network },
    { E("📞"), "Calls",                   AtMode::Calls },
    { E("📩"), "SMS",                    AtMode::Sms },
    { E("📒"), "Phonebook",               AtMode::Phonebook },
    { E("💱"), "USSD",                    AtMode::Ussd },
    { E("🌐"), "Data (PDP)",              AtMode::Data },
    { E("⏰"), "Time & Clock",            AtMode::TimeClock },
    { E("🌍"), "Internet TCP/IP",         AtMode::TcpIp_Vendor },
    { E("🔒"), "HTTP(S)",                AtMode::Http_Vendor },
    { E("🧭"), "GNSS/GPS",                AtMode::Gnss_Vendor },
    { E("📡"), "LoRa / LoRaWAN",          AtMode::Lora },
    { E("🔈"), "Audio",                   AtMode::Audio },
    { E("🔋"), "Power / PSM",             AtMode::Power },
    { E("🧰"), "Hardware I/O",            AtMode::Hardware },
    { E("🌪️"), "MQTT",                   AtMode::Mqtt },
    { E("📁"), "File Storage",            AtMode::Files },
};
inline constexpr size_t kAtModesCount = AT_COUNT_OF(kAtModes);


// ================== Actions per mode ==================

// CONFIG (universal)
inline constexpr AtActionArg kArgs_DeepSleep[] = {
  { "Duration (ms)", true, "1000", "sleep time (module-dependent)",
    AtArgType::Uint, 1, 86400000, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_UartCommon[] = {
  { "Baud",      true, "115200", nullptr,               AtArgType::Uint,   1200, 921600, nullptr, nullptr },
  { "Data bits", true, "8",      "5/6/7/8",             AtArgType::Choice, 0,    0,      "5|6|7|8", nullptr },
  { "Stop bits", true, "1",      "1/2",                 AtArgType::Choice, 0,    0,      "1|2",    nullptr },
  { "Parity",    true, "0",      "0=None,1=Odd,2=Even", AtArgType::Choice, 0,    0,      "0|1|2",  nullptr },
  { "FlowCtl",   true, "0",      "0=off,1=on",          AtArgType::Choice, 0,    0,      "0|1",    nullptr }
};

inline constexpr AtActionArg kArgs_SleepMode[] = {
  { "Mode", true, "1", "0=off,1=light,2=modem (module-dep.)",
    AtArgType::Choice, 0, 0, "0|1|2", nullptr }
};

inline constexpr AtActionArg kArgs_RfTxPower[] = {
  { "Level", true, "78", "module-dependent", AtArgType::Uint, 0, 100, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_BoolEnable[] = {
  { "Enable", true, "1", "0/1", AtArgType::Bool01, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_Ipr[] = {
  { "Baud rate", true, "115200", "9600 / 19200 / 115200 ...",
    AtArgType::Uint, 1200, 921600, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_Cmee[] = {
  { "Mode", true, "2", "0=off,1=basic,2=verbose",
    AtArgType::Choice, 0, 0, "0|1|2", nullptr }
};

inline constexpr AtActionArg kArgs_Cscs[] = {
  { "Charset", true, "GSM", "GSM/IRA/UCS2",
    AtArgType::Choice, 0, 0, "GSM|IRA|UCS2", nullptr }
};

inline constexpr AtActionArg kArgs_Ifc[] = {
  { "RX flow", true, "2", "0=off,1=hw,2=sw", AtArgType::Choice, 0, 0, "0|1|2", nullptr },
  { "TX flow", true, "2", "0=off,1=hw,2=sw", AtArgType::Choice, 0, 0, "0|1|2", nullptr }
};

inline constexpr AtActionArg kArgs_Profile01[] = {
  { "Profile", true, "1", "0 or 1", AtArgType::Choice, 0, 0, "0|1", nullptr }
};

inline constexpr AtActionArg kArgs_Quiet[] = {
  { "Quiet", true, "0", "0=off,1=on", AtArgType::Choice, 0, 0, "0|1", nullptr }
};

inline constexpr AtActionArg kArgs_Verbose[] = {
  { "Mode", true, "1", "0=numeric,1=verbose", AtArgType::Choice, 0, 0, "0|1", nullptr }
};

inline constexpr AtActionArg kArgs_DcdUsage[] = {
  { "Mode", true, "1", "0=always ON,1=follows carrier", AtArgType::Choice, 0, 0, "0|1", nullptr }
};

inline constexpr AtActionArg kArgs_DtrBehaviour[] = {
  { "Mode", true, "2", "0=ignore,2=hang up+CMD", AtArgType::Choice, 0, 0, "0|2", nullptr }
};

inline constexpr AtActionArg kArgs_Icf[] = {
  { "Format", true, "3", "module-specific", AtArgType::Uint, 0, 10, nullptr, nullptr },
  { "Parity", true, "0", "0=none,1=odd,2=even (module)", AtArgType::Uint, 0, 5, nullptr, nullptr }
};

// CONFIG (universal)
inline constexpr AtActionItem kAtConfigActions[] = {
    { E("⚡"), "Test communication", "AT",          AtStandard::Hayes,    "AT",                   false, AT_NOARGS},
    { E("🔈"), "Enable/Disable echo","ATE",         AtStandard::Hayes,    "ATE",                  false, AT_NOARGS},
    { E("🔇"), "Disable echo",       "ATE0",        AtStandard::Hayes,    "ATE0",                 false, AT_NOARGS},
    { E("🔊"), "Enable echo",        "ATE1",        AtStandard::Hayes,    "ATE1",                 false, AT_NOARGS},
    { E("🔄"), "Restart module",     "AT+RST",      AtStandard::Hayes,    "AT+RST",               false, AT_NOARGS},

    { E("💤"), "Deep sleep",         "AT+GSLP=%1",  AtStandard::Hayes,    "AT+GSLP=1000",         false,
      kArgs_DeepSleep, AT_COUNT_OF(kArgs_DeepSleep) },

    { E("🧼"), "Factory reset (ESP-AT)","AT+RESTORE", AtStandard::Hayes,  "AT+RESTORE",           true,  AT_NOARGS},

    // UART configuration (ESP-AT)
    { E("🔧"), "UART config (deprecated)", "AT+UART=%1,%2,%3,%4,%5",
      AtStandard::Hayes, "AT+UART=115200,8,1,0,0", false,
      kArgs_UartCommon, AT_COUNT_OF(kArgs_UartCommon) },

    { E("🔧"), "UART current (no save)", "AT+UART_CUR=%1,%2,%3,%4,%5",
      AtStandard::Hayes, "AT+UART_CUR=115200,8,1,0,0", false,
      kArgs_UartCommon, AT_COUNT_OF(kArgs_UartCommon) },

    { E("🔧"), "UART default (save)", "AT+UART_DEF=%1,%2,%3,%4,%5",
      AtStandard::Hayes, "AT+UART_DEF=115200,8,1,0,0", false,
      kArgs_UartCommon, AT_COUNT_OF(kArgs_UartCommon) },

    { E("🌙"), "Sleep mode",          "AT+SLEEP=%1", AtStandard::Hayes,   "AT+SLEEP=1",           false,
      kArgs_SleepMode, AT_COUNT_OF(kArgs_SleepMode) },

    { E("📶"), "Set RF TX power",     "AT+RFPOWER=%1", AtStandard::Hayes, "AT+RFPOWER=78",        false,
      kArgs_RfTxPower, AT_COUNT_OF(kArgs_RfTxPower) },

    { E("🔌"), "RF power by VDD",     "AT+RFVDD=%1", AtStandard::Hayes,   "AT+RFVDD=1",           false,
      kArgs_BoolEnable, AT_COUNT_OF(kArgs_BoolEnable) },

    { E("⚡"), "Soft reset (profile)","ATZ",        AtStandard::Hayes,   "ATZ",                  false, AT_NOARGS},
    { E("🏭"), "Factory defaults",    "AT&F",        AtStandard::Hayes,   "AT&F",                 true,  AT_NOARGS},
    { E("💾"), "Save profile",        "AT&W",        AtStandard::Hayes,   "AT&W",                 false, AT_NOARGS},

    { E("⏩"), "Set baud rate",       "AT+IPR=%1",   AtStandard::ThreeGPP,"AT+IPR=115200",        false,
      kArgs_Ipr, AT_COUNT_OF(kArgs_Ipr) },

    { E("🧩"), "Verbose errors",      "AT+CMEE=%1",  AtStandard::ThreeGPP,"AT+CMEE=2",            false,
      kArgs_Cmee, AT_COUNT_OF(kArgs_Cmee) },

    { E("🔤"), "Char set",            "AT+CSCS=\"%1\"", AtStandard::ThreeGPP, "AT+CSCS=\"GSM\"",  false,
      kArgs_Cscs, AT_COUNT_OF(kArgs_Cscs) },

    { E("🪛"), "Flow control",       "AT+IFC=%1,%2", AtStandard::ThreeGPP, "AT+IFC=2,2",         false,
      kArgs_Ifc, AT_COUNT_OF(kArgs_Ifc) },

    { E("🔍"), "View active config",  "AT&V",        AtStandard::Hayes,   "AT&V",                 false, AT_NOARGS},

    { E("🧭"), "Power-up profile",    "AT&Y%1",      AtStandard::Hayes,   "AT&Y1",                false,
      kArgs_Profile01, AT_COUNT_OF(kArgs_Profile01) },

    { E("🤫"), "Suppress result codes","ATQ%1",      AtStandard::Hayes,   "ATQ0",                 false,
      kArgs_Quiet, AT_COUNT_OF(kArgs_Quiet) },

    { E("🗨️"), "Response format",    "ATV%1",       AtStandard::Hayes,   "ATV1",                 false,
      kArgs_Verbose, AT_COUNT_OF(kArgs_Verbose) },

    { E("📎"), "DCD usage",           "AT&C%1",      AtStandard::Hayes,   "AT&C1",                false,
      kArgs_DcdUsage, AT_COUNT_OF(kArgs_DcdUsage) },

    { E("🗨️"), "DTR behaviour",      "AT&D%1",      AtStandard::Hayes,   "AT&D2",                false,
      kArgs_DtrBehaviour, AT_COUNT_OF(kArgs_DtrBehaviour) },

    { E("🧩"), "Char framing (ICF)",   "AT+ICF=%1,%2",AtStandard::ThreeGPP,"AT+ICF=3,1",          false,
      kArgs_Icf, AT_COUNT_OF(kArgs_Icf) },
};
inline constexpr size_t kAtConfigActionsCount =
    AT_COUNT_OF(kAtConfigActions);

// INFO (universal)
inline constexpr AtActionItem kAtInfoActions[] = {
    { E("📌"), "Module info",           "ATI",     AtStandard::Hayes,    "ATI",     false, AT_NOARGS},
    { E("📎"), "Manufacturer",          "AT+GMI",  AtStandard::Hayes,    "AT+GMI",  false, AT_NOARGS},
    { E("🔧"),  "Model",                 "AT+GMM",  AtStandard::Hayes,    "AT+GMM",  false, AT_NOARGS},
    { E("🔧"), "Firmware version",      "AT+GMR",  AtStandard::Hayes,    "AT+GMR",  false, AT_NOARGS},
    { E("🆔"),  "IMEI (GSN)",            "AT+GSN",  AtStandard::Hayes,    "AT+GSN",  false, AT_NOARGS},
    { E("🆔"),  "IMEI",                  "AT+CGSN", AtStandard::ThreeGPP, "AT+CGSN", false, AT_NOARGS},
    { E("🧩"),  "Capabilities",          "AT+GCAP", AtStandard::Hayes,    "AT+GCAP", false, AT_NOARGS},
    { E("📎"), "Manufacturer (CGMI)",   "AT+CGMI", AtStandard::ThreeGPP, "AT+CGMI", false, AT_NOARGS},
    { E("🔧"),  "Model (CGMM)",          "AT+CGMM", AtStandard::ThreeGPP, "AT+CGMM", false, AT_NOARGS},
    { E("🔧"), "Firmware (CGMR)",       "AT+CGMR", AtStandard::ThreeGPP, "AT+CGMR", false, AT_NOARGS},
};
inline constexpr size_t kAtInfoActionsCount =
    sizeof(kAtInfoActions) / sizeof(kAtInfoActions[0]);


// SIM & PIN
inline constexpr AtActionArg kArgs_CPIN_Enter[] = {
    { "PIN or PUK", true, nullptr, "1234 / PUK code",
      AtArgType::String, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CLCK_Pin[] = {
    { "PIN", true, nullptr, "current PIN",
      AtArgType::String, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CPWD[] = {
    { "Old PIN", true, nullptr, nullptr,
      AtArgType::String, 0, 0, nullptr, nullptr },
    { "New PIN", true, nullptr, nullptr,
      AtArgType::String, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionItem kAtSimPinActions[] = {
    { E("🔍"), "PIN status",        "AT+CPIN?",                      AtStandard::ThreeGPP, "AT+CPIN?",                     false, AT_NOARGS },
    { E("🔓"), "Enter PIN/PUK",     "AT+CPIN=\"%1\"",                AtStandard::ThreeGPP, "AT+CPIN=\"1234\"",             true,  kArgs_CPIN_Enter, AT_COUNT_OF(kArgs_CPIN_Enter) },
    { E("🔐"), "Enable SIM lock",   "AT+CLCK=\"SC\",1,\"%1\"",       AtStandard::ThreeGPP, "AT+CLCK=\"SC\",1,\"1234\"",    true,  kArgs_CLCK_Pin,   AT_COUNT_OF(kArgs_CLCK_Pin)   },
    { E("🔓"), "Disable SIM lock",  "AT+CLCK=\"SC\",0,\"%1\"",       AtStandard::ThreeGPP, "AT+CLCK=\"SC\",0,\"1234\"",    true,  kArgs_CLCK_Pin,   AT_COUNT_OF(kArgs_CLCK_Pin)   },
    { E("⚡"), "Change PIN",       "AT+CPWD=\"SC\",\"%1\",\"%2\"",  AtStandard::ThreeGPP, "AT+CPWD=\"SC\",\"1234\",\"0000\"", true, kArgs_CPWD,      AT_COUNT_OF(kArgs_CPWD)       },
};
inline constexpr size_t kAtSimPinActionsCount = AT_COUNT_OF(kAtSimPinActions);


// TEL NETWORK
 constexpr AtActionArg kArgs_COPS_MccMnc[] = {
    { "MCCMNC", true, nullptr, "e.g. 20801",
      AtArgType::Regex, 0, 0, nullptr, "^[0-9]{5,6}$" }
};

inline constexpr AtActionArg kArgs_CNMP_3gpp_Mode[] = {
    { "Mode", true, "2", "module-dependent",
      AtArgType::Uint, 0, 100, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_QCFG_Nwscanmode[] = {
    { "Mode", true, "3", "0=auto,3=GSM/WCDMA/LTE",
      AtArgType::Uint, 0, 10, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CFUN[] = {
    { "CFUN", true, "1", "0=minimal,1=full,4=airplane",
      AtArgType::Choice, 0, 0, "0|1|4", nullptr }
};

inline constexpr AtActionArg kArgs_CNMP_Simcom_Mode[] = {
    { "Mode", true, "2", "2=auto,13=GSM,38=LTE…",
      AtArgType::Uint, 0, 100, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CMNB_Pref[] = {
    { "Pref", true, "1", "module-dependent",
      AtArgType::Uint, 0, 10, nullptr, nullptr }
};

inline constexpr AtActionItem kAtNetworkActions[] = {
    { E("📡"), "Signal quality",          "AT+CSQ",                       AtStandard::ThreeGPP, "AT+CSQ",                       false, AT_NOARGS },
    { E("📎"), "GSM reg status",          "AT+CREG?",                     AtStandard::ThreeGPP, "AT+CREG?",                     false, AT_NOARGS },
    { E("🪛"), "Packet reg status",       "AT+CGREG?",                    AtStandard::ThreeGPP, "AT+CGREG?",                    false, AT_NOARGS },
    { E("📌"), "LTE reg status",          "AT+CEREG?",                    AtStandard::ThreeGPP, "AT+CEREG?",                    false, AT_NOARGS },
    { E("🏢"),  "Current operator",        "AT+COPS?",                     AtStandard::ThreeGPP, "AT+COPS?",                     false, AT_NOARGS },
    { E("🧭"),  "List operators",          "AT+COPS=?",                    AtStandard::ThreeGPP, "AT+COPS=?",                    false, AT_NOARGS },
    { E("📶"),  "Extended signal quality", "AT+CESQ",                      AtStandard::ThreeGPP, "AT+CESQ",                      false, AT_NOARGS },
    { E("🔁"),  "Auto select operator",    "AT+COPS=0",                    AtStandard::ThreeGPP, "AT+COPS=0",                    false, AT_NOARGS },
    { E("🧯"),  "Extended error report",   "AT+CEER",                      AtStandard::ThreeGPP, "AT+CEER",                      false, AT_NOARGS },

    { E("📶"),  "Set operator",            "AT+COPS=1,2,\"%1\"",           AtStandard::ThreeGPP, "AT+COPS=1,2,\"20801\"",        false, kArgs_COPS_MccMnc,     AT_COUNT_OF(kArgs_COPS_MccMnc) },
    { E("🌐"), "Network mode",            "AT+CNMP=%1",                   AtStandard::ThreeGPP, "AT+CNMP=2",                    false, kArgs_CNMP_3gpp_Mode,  AT_COUNT_OF(kArgs_CNMP_3gpp_Mode) },
    { E("🌐"), "Network scan mode",       "AT+QCFG=\"nwscanmode\",%1",    AtStandard::Quectel,  "AT+QCFG=\"nwscanmode\",3",     false, kArgs_QCFG_Nwscanmode, AT_COUNT_OF(kArgs_QCFG_Nwscanmode) },
    { E("📌"),  "Manual operator (PLMN)",  "AT+COPS=1,2,\"%1\"",           AtStandard::ThreeGPP, "AT+ COPS=1,2,\"20801\"",       false, kArgs_COPS_MccMnc,     AT_COUNT_OF(kArgs_COPS_MccMnc) },
    { E("💫"), "Function level",          "AT+CFUN=%1",                   AtStandard::ThreeGPP, "AT+CFUN=1",                    false, kArgs_CFUN,            AT_COUNT_OF(kArgs_CFUN) },

    // Vendor (popular)
    { E("📶"),  "RAT mode (SIMCom)",       "AT+CNMP=%1",                   AtStandard::SIMCom,   "AT+CNMP=2",                    false, kArgs_CNMP_Simcom_Mode, AT_COUNT_OF(kArgs_CNMP_Simcom_Mode) },
    { E("🧱"),  "NB/LTE pref (SIMCom)",    "AT+CMNB=%1",                   AtStandard::SIMCom,   "AT+CMNB=1",                    false, kArgs_CMNB_Pref,        AT_COUNT_OF(kArgs_CMNB_Pref) },
    { E("🧭"),  "Scan mode (Quectel)",     "AT+QCFG=\"nwscanmode\",%1",    AtStandard::Quectel,  "AT+QCFG=\"nwscanmode\",3",     false, kArgs_QCFG_Nwscanmode, AT_COUNT_OF(kArgs_QCFG_Nwscanmode) },
};

inline constexpr size_t kAtNetworkActionsCount = AT_COUNT_OF(kAtNetworkActions);

// CALLS
inline constexpr AtActionArg kArgs_Dial_Number[] = {
    { "Number", true, nullptr, "+33...", AtArgType::Phone, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CLIP[] = {
    { "CLIP", true, "1", "0/1", AtArgType::Bool01, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CLIR[] = {
    { "CLIR", true, "1", "0/1", AtArgType::Bool01, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_VTS_Digits[] = {
    { "Digits", true, nullptr, "0-9 * #", AtArgType::Regex, 0, 0, nullptr, "^[0-9*#]+$" }
};

inline constexpr AtActionArg kArgs_VTD_Units[] = {
    { "Units", true, "4", "1..255 (dépend module)", AtArgType::Uint, 1, 255, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_COLP_Enable[] = {
    { "Enable", true, "1", "0/1", AtArgType::Bool01, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CSTA_Type[] = {
    { "Type", true, "145", "129=national,145=international", AtArgType::Choice, 0, 0, "129|145", nullptr }
};

// --- CALLS : actions  ---
inline constexpr AtActionItem kAtCallsActions[] = {
    { E("📲"), "Dial (voice)","ATD%1;",      AtStandard::Hayes,    "ATD+33123456789;", false, kArgs_Dial_Number, AT_COUNT_OF(kArgs_Dial_Number) },
    { E("✅"), "Answer",      "ATA",         AtStandard::Hayes,    "ATA",              false, AT_NOARGS },
    { E("📴"), "Hang up",     "ATH",         AtStandard::Hayes,    "ATH",              false, AT_NOARGS },
    { E("📃"), "List current calls","AT+CLCC", AtStandard::ThreeGPP, "AT+CLCC",       false, AT_NOARGS },
    { E("🆔"), "Caller ID presentation","AT+CLIP=%1", AtStandard::ThreeGPP, "AT+CLIP=1", false, kArgs_CLIP, AT_COUNT_OF(kArgs_CLIP) },
    { E("🙈"), "Caller ID restriction","AT+CLIR=%1",  AtStandard::ThreeGPP, "AT+CLIR=1", false, kArgs_CLIR, AT_COUNT_OF(kArgs_CLIR) },
    { E("🔊"), "DTMF tone",   "AT+VTS=\"%1\"", AtStandard::ThreeGPP, "AT+VTS=\"123#\"", false, kArgs_VTS_Digits, AT_COUNT_OF(kArgs_VTS_Digits) },
    { E("🕐"), "DTMF duration","AT+VTD=%1", AtStandard::ThreeGPP, "AT+VTD=4",         false, kArgs_VTD_Units, AT_COUNT_OF(kArgs_VTD_Units) },
    { E("🖲️"), "Connected line ID (COLP)","AT+COLP=%1", AtStandard::ThreeGPP, "AT+COLP=1", false, kArgs_COLP_Enable, AT_COUNT_OF(kArgs_COLP_Enable) },
    { E("🖲️"), "Connected line ID (COLP)","AT+COLP=%1", AtStandard::ThreeGPP, "AT+COLP=1", false, kArgs_COLP_Enable, AT_COUNT_OF(kArgs_COLP_Enable) }, // (doublon volontaire, comme l’original)
    { E("📎"), "Default dial TOA","AT+CSTA=%1", AtStandard::ThreeGPP, "AT+CSTA=145", false, kArgs_CSTA_Type, AT_COUNT_OF(kArgs_CSTA_Type) },
    { E("📱"), "Phone activity status","AT+CPAS", AtStandard::ThreeGPP, "AT+CPAS",     false, AT_NOARGS },
};

inline constexpr size_t kAtCallsActionsCount = AT_COUNT_OF(kAtCallsActions);


// // SMS
inline constexpr AtActionArg kArgs_CMGS_Phone[] = {
    { "Phone number", true, nullptr, "+33...", AtArgType::Phone, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CMGR_Index[] = {
    { "Index", true, "1", "message index", AtArgType::Uint, 1, 1000, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CMGL_Filter[] = {
    { "Filter", true, "ALL",
      "REC UNREAD|REC READ|STO UNSENT|STO SENT|ALL",
      AtArgType::Choice, 0, 0, "REC UNREAD|REC READ|STO UNSENT|STO SENT|ALL", nullptr }
};

inline constexpr AtActionArg kArgs_CMGD_Index[] = {
    { "Index", true, nullptr, "message index", AtArgType::Uint, 0, 1000, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CNMI[] = {
    { "mode", true, "2", nullptr, AtArgType::Uint, 0, 3, nullptr, nullptr },
    { "mt",   true, "1", nullptr, AtArgType::Uint, 0, 3, nullptr, nullptr },
    { "bm",   true, "0", nullptr, AtArgType::Uint, 0, 3, nullptr, nullptr },
    { "ds",   true, "0", nullptr, AtArgType::Uint, 0, 3, nullptr, nullptr },
    { "bfr",  true, "0", nullptr, AtArgType::Uint, 0, 3, nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_CSMP[] = {
    { "FO",  true, "17",  nullptr, AtArgType::Uint, 0, 255, nullptr, nullptr },
    { "VP",  true, "167", nullptr, AtArgType::Uint, 0, 255, nullptr, nullptr },
    { "PID", true, "0",   nullptr, AtArgType::Uint, 0, 255, nullptr, nullptr },
    { "DCS", true, "0",   nullptr, AtArgType::Uint, 0, 255, nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_CMGW_Dest[] = {
    { "Destination (opt.)", false, nullptr, "Could be empty for draft",
      AtArgType::Phone, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CMSS_IndexOverride[] = {
    { "Index", true, "1", nullptr, AtArgType::Uint, 1, 1000, nullptr, nullptr },
    { "Override number (opt.)", false, nullptr, "optionnel", AtArgType::Phone, 0, 0, nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_CSMS_Service[] = {
    { "Service", true, "1", "module-dependent", AtArgType::Uint, 0, 3, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CSDH_Show[] = {
    { "Show", true, "1", "0/1", AtArgType::Bool01, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CSCB[] = {
    { "Mode", true, "0", "0=accept all,1=reject", AtArgType::Choice, 0, 0, "0|1", nullptr },
    { "MIDs", true, "",  "IDs list",              AtArgType::String, 0, 0, nullptr, nullptr },
    { "DCS",  true, "",  "DCS list",              AtArgType::String, 0, 0, nullptr, nullptr },
};

// --- SMS actions ---
inline constexpr AtActionItem kAtSmsActions[] = {
    { E("🧰"), "PDU mode",              "AT+CMGF=0",                    AtStandard::ThreeGPP, "AT+CMGF=0",                    false, AT_NOARGS },
    { E("✉️"), "Text mode",             "AT+CMGF=1",                    AtStandard::ThreeGPP, "AT+CMGF=1",                    false, AT_NOARGS },
    { E("📮"), "SMSC address",          "AT+CSCA?",                     AtStandard::ThreeGPP, "AT+CSCA?",                     false, AT_NOARGS },
    { E("📤"), "Send SMS (text)",       "AT+CMGS=\"%1\"",               AtStandard::ThreeGPP, "AT+CMGS=\"+33123456789\"",     false, kArgs_CMGS_Phone,           AT_COUNT_OF(kArgs_CMGS_Phone) },
    { E("📥"), "Read SMS",              "AT+CMGR=%1",                   AtStandard::ThreeGPP, "AT+CMGR=1",                    false, kArgs_CMGR_Index,           AT_COUNT_OF(kArgs_CMGR_Index) },
    { E("📜"), "List SMS",              "AT+CMGL=\"%1\"",               AtStandard::ThreeGPP, "AT+CMGL=\"ALL\"",              false, kArgs_CMGL_Filter,          AT_COUNT_OF(kArgs_CMGL_Filter) },
    { E("✂️"), "Delete SMS",           "AT+CMGD=%1",                   AtStandard::ThreeGPP, "AT+CMGD=1",                    true,  kArgs_CMGD_Index,           AT_COUNT_OF(kArgs_CMGD_Index) },
    { E("🔔"), "New SMS indications",   "AT+CNMI=%1,%2,%3,%4,%5",       AtStandard::ThreeGPP, "AT+CNMI=2,1,0,0,0",            false, kArgs_CNMI,                 AT_COUNT_OF(kArgs_CNMI) },
    { E("🔧"), "SMS params (CSMP)",     "AT+CSMP=%1,%2,%3,%4",          AtStandard::ThreeGPP, "AT+CSMP=17,167,0,0",           false, kArgs_CSMP,                 AT_COUNT_OF(kArgs_CSMP) },
    { E("💾"), "Write SMS to mem",      "AT+CMGW=\"%1\"",               AtStandard::ThreeGPP, "AT+CMGW=\"+33123456789\"",     false, kArgs_CMGW_Dest,            AT_COUNT_OF(kArgs_CMGW_Dest) },
    { E("📤"), "Send SMS from mem",     "AT+CMSS=%1,\"%2\"",            AtStandard::ThreeGPP, "AT+CMSS=1,\"+33123456789\"",   false, kArgs_CMSS_IndexOverride,   AT_COUNT_OF(kArgs_CMSS_IndexOverride) },
    { E("✅"), "Ack incoming SMS",      "AT+CNMA",                      AtStandard::ThreeGPP, "AT+CNMA",                      false, AT_NOARGS },
    { E("📦"), "Storage status",        "AT+CPMS?",                     AtStandard::ThreeGPP, "AT+CPMS?",                     false, AT_NOARGS },
    { E("🧭"), "Select message service","AT+CSMS=%1",                   AtStandard::ThreeGPP, "AT+CSMS=1",                     false, kArgs_CSMS_Service,         AT_COUNT_OF(kArgs_CSMS_Service) },
    { E("🔎"), "Show text params",      "AT+CSDH=%1",                   AtStandard::ThreeGPP, "AT+CSDH=1",                     false, kArgs_CSDH_Show,            AT_COUNT_OF(kArgs_CSDH_Show) },
    { E("📢"), "Cell broadcast filter", "AT+CSCB=%1,\"%2\",\"%3\"",     AtStandard::ThreeGPP, "AT+CSCB=0,\"\",\"\"",           false, kArgs_CSCB,                 AT_COUNT_OF(kArgs_CSCB) },
    { E("💾"), "Save SMS settings",     "AT+CSAS",                      AtStandard::ThreeGPP, "AT+CSAS",                      false, AT_NOARGS },
    { E("♻️"), "Restore SMS settings",  "AT+CRES",                      AtStandard::ThreeGPP, "AT+CRES",                      false, AT_NOARGS },
};
inline constexpr size_t kAtSmsActionsCount = AT_COUNT_OF(kAtSmsActions);

            
// PHONEBOOK
inline constexpr AtActionArg kArgs_CPBS_Storage[] = {
    { "Storage", true, "SM", "SM/ME", AtArgType::Choice, 0, 0, "SM|ME", nullptr }
};

inline constexpr AtActionArg kArgs_CPBR_Indices[] = {
    { "Start index", true, "1",  nullptr, AtArgType::Uint, 1, 250, nullptr, nullptr },
    { "End index",   true, "10", nullptr, AtArgType::Uint, 1, 250, nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_CPBF_Query[] = {
    { "Query", true, nullptr, "substring", AtArgType::String, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CPBW_WriteUpdate[] = {
    { "Index",  true, nullptr, nullptr, AtArgType::Uint,   1, 250, nullptr, nullptr },
    { "Number", true, nullptr, "+33...", AtArgType::Phone,  0,   0, nullptr, nullptr },
    { "Type",   true, "145",   "129=national,145=international",
                                 AtArgType::Choice, 0, 0, "129|145", nullptr },
    { "Name",   true, nullptr, nullptr,  AtArgType::String, 0,   0, nullptr, nullptr },
};

// --- PHONEBOOK actions ---
inline constexpr AtActionItem kAtPhonebookActions[] = {
    { E("📚"), "Select storage",   "AT+CPBS=\"%1\"",       AtStandard::ThreeGPP, "AT+CPBS=\"SM\"",
      false, kArgs_CPBS_Storage,     AT_COUNT_OF(kArgs_CPBS_Storage) },

    { E("📖"), "Read entries",     "AT+CPBR=%1,%2",        AtStandard::ThreeGPP, "AT+CPBR=1,10",
      false, kArgs_CPBR_Indices,     AT_COUNT_OF(kArgs_CPBR_Indices) },

    { E("🔎"), "Find by name",     "AT+CPBF=\"%1\"",       AtStandard::ThreeGPP, "AT+CPBF=\"ALICE\"",
      false, kArgs_CPBF_Query,       AT_COUNT_OF(kArgs_CPBF_Query) },

    { E("🪛"), "Write/Update entry","AT+CPBW=%1,\"%2\",%3,\"%4\"", AtStandard::ThreeGPP,
      "AT+CPBW=1,\"+33123456789\",145,\"ALICE\"",
      true,  kArgs_CPBW_WriteUpdate, AT_COUNT_OF(kArgs_CPBW_WriteUpdate) },
};
inline constexpr size_t kAtPhonebookActionsCount = AT_COUNT_OF(kAtPhonebookActions);

// USSD
inline constexpr AtActionArg kArgs_CUSD_Request[] = {
    { "USSD code", true, nullptr, "*100#", AtArgType::Regex, 0, 0, nullptr, "^[*#0-9]+#$" },
    { "DCS",       true, "15",    "15=GSM 7-bit",           AtArgType::Uint,  0, 255, nullptr, nullptr }
};

// --- USSD actions ---
inline constexpr AtActionItem kAtUssdActions[] = {
    { E("💱"), "USSD request", "AT+CUSD=1,\"%1\",%2", AtStandard::ThreeGPP,
      "AT+CUSD=1,\"*100#\",15", false,
      kArgs_CUSD_Request, AT_COUNT_OF(kArgs_CUSD_Request) },

    { E("🛑"), "Cancel USSD",  "AT+CUSD=2",            AtStandard::ThreeGPP,
      "AT+CUSD=2",            false, AT_NOARGS }
};

inline constexpr size_t kAtUssdActionsCount = AT_COUNT_OF(kAtUssdActions);


// DATA / PDP
inline constexpr AtActionArg kArgs_CGATT_Attach[] = {
    { "Attach", true, "1", "0/1", AtArgType::Bool01, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CGAUTH[] = {
    { "CID",   true, "1",  nullptr,                 AtArgType::Uint,   1, 16, nullptr, nullptr },
    { "Type",  true, "1",  "0=None,1=PAP,2=CHAP",   AtArgType::Choice, 0,  0, "0|1|2",  nullptr },
    { "User",  true, nullptr, nullptr,              AtArgType::String, 0,  0, nullptr,  nullptr },
    { "Pass",  true, nullptr, nullptr,              AtArgType::String, 0,  0, nullptr,  nullptr },
};

inline constexpr AtActionArg kArgs_CGDCONT[] = {
    { "CID", true, "1",        nullptr,        AtArgType::Uint,  1, 16, nullptr, nullptr },
    { "APN", true, "internet", "APN name",     AtArgType::Regex, 0,  0, nullptr, "^[A-Za-z0-9._-]+$" },
};

inline constexpr AtActionArg kArgs_CID_1_16[] = {
    { "CID", true, "1", nullptr, AtArgType::Uint, 1, 16, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CGCONTRDP_OptCid[] = {
    { "CID (or empty)", false, nullptr, "leave empty for all", AtArgType::String, 0, 0, nullptr, nullptr }
};

// --- DATA / PDP actions ---
inline constexpr AtActionItem kAtDataActions[] = {
    { E("🔌"), "Attach packet svc", "AT+CGATT=%1",              AtStandard::ThreeGPP,
      "AT+CGATT=1", false, kArgs_CGATT_Attach,     AT_COUNT_OF(kArgs_CGATT_Attach) },

    { E("🔒"), "PDP auth (PAP/CHAP)", "AT+CGAUTH=%1,%2,\"%3\",\"%4\"", AtStandard::ThreeGPP,
      "AT+CGAUTH=1,1,\"user\",\"pass\"", false, kArgs_CGAUTH, AT_COUNT_OF(kArgs_CGAUTH) },

    { E("📑"), "Define PDP context", "AT+CGDCONT=%1,\"IP\",\"%2\"", AtStandard::ThreeGPP,
      "AT+CGDCONT=1,\"IP\",\"internet\"", false, kArgs_CGDCONT, AT_COUNT_OF(kArgs_CGDCONT) },

    { E("⚡"), "Activate PDP",     "AT+CGACT=1,%1",           AtStandard::ThreeGPP,
      "AT+CGACT=1,1", false, kArgs_CID_1_16, AT_COUNT_OF(kArgs_CID_1_16) },

    { E("⚡"), "Deactivate PDP",   "AT+CGACT=0,%1",           AtStandard::ThreeGPP,
      "AT+CGACT=0,1", false, kArgs_CID_1_16, AT_COUNT_OF(kArgs_CID_1_16) },

    { E("🌍"), "Show IP address",   "AT+CGPADDR=%1",           AtStandard::ThreeGPP,
      "AT+CGPADDR=1", false, kArgs_CID_1_16, AT_COUNT_OF(kArgs_CID_1_16) },

    { E("📘"), "Context readback",  "AT+CGCONTRDP=%1",         AtStandard::ThreeGPP,
      "AT+CGCONTRDP=1", false, kArgs_CGCONTRDP_OptCid, AT_COUNT_OF(kArgs_CGCONTRDP_OptCid) },
};
inline constexpr size_t kAtDataActionsCount = AT_COUNT_OF(kAtDataActions);


// TIME & CLOCK
inline constexpr AtActionArg kArgs_CCLK_Set[] = {
    { "Datetime (yy/MM/dd,hh:mm:ss±tz)", true, nullptr, "25/08/11,12:00:00+08",
      AtArgType::Regex, 0, 0, nullptr,
      "^[0-9]{2}/[0-9]{2}/[0-9]{2},[0-9]{2}:[0-9]{2}:[0-9]{2}[+-][0-9]{2}$" }
};

inline constexpr AtActionArg kArgs_EnableBool[] = {
    { "Enable", true, "1", "0/1", AtArgType::Bool01, 0, 0, nullptr, nullptr }
};

// --- TIME & CLOCK ---
inline constexpr AtActionItem kAtTimeClockActions[] = {
    { E("⏰"), "Read clock",            "AT+CCLK?",                AtStandard::ThreeGPP,
      "AT+CCLK?", false, AT_NOARGS },

    { E("🕒"), "Set clock",             "AT+CCLK=\"%1\"",          AtStandard::ThreeGPP,
      "AT+CCLK=\"25/08/11,12:00:00+08\"", true,
      kArgs_CCLK_Set, AT_COUNT_OF(kArgs_CCLK_Set) },

    { E("💫"), "Auto TZ update (NITZ)", "AT+CTZU=%1",             AtStandard::ThreeGPP,
      "AT+CTZU=1", false,
      kArgs_EnableBool, AT_COUNT_OF(kArgs_EnableBool) },

    { E("💫"), "TZ URC reporting",     "AT+CTZR=%1",             AtStandard::ThreeGPP,
      "AT+CTZR=1", false,
      kArgs_EnableBool, AT_COUNT_OF(kArgs_EnableBool) },
};

inline constexpr size_t kAtTimeClockActionsCount = AT_COUNT_OF(kAtTimeClockActions);


// INTERNET TCP/IP
inline constexpr AtActionArg kArgs_Mode_STA_AP[] = {
    { "Mode", true, "1", "1=STA,2=AP,3=STA+AP",
      AtArgType::Choice, 0, 0, "1|2|3", nullptr }
};

inline constexpr AtActionArg kArgs_SSID_Password[] = {
    { "SSID",     true, nullptr, "network SSID",     AtArgType::String, 0, 0, nullptr, nullptr },
    { "Password", true, nullptr, "network password", AtArgType::String, 0, 0, nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_SoftAP[] = {
    { "SSID",     true, "MyAP",   nullptr, AtArgType::String, 0, 0, nullptr, nullptr },
    { "Password", true, "apPass", nullptr, AtArgType::String, 0, 0, nullptr, nullptr },
    { "Channel",  true, "6",      nullptr, AtArgType::Uint,   1, 13, nullptr, nullptr },
    { "Ecn",      true, "4", "0=OPEN,1=WEP,2=WPA_PSK,3=WPA2_PSK,4=WPA_WPA2_PSK",
                   AtArgType::Choice, 0, 0, "0|1|2|3|4", nullptr },
};

inline constexpr AtActionArg kArgs_DHCP_Mode_Enable[] = {
    { "Mode",   true, "1", "0=STA,1=AP,2=BOTH", AtArgType::Choice, 0, 0, "0|1|2", nullptr },
    { "Enable", true, "1", "0/1",               AtArgType::Bool01, 0, 0, nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_EnableBool2[] = {
    { "Enable", true, "1", "0/1", AtArgType::Bool01, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_MAC[] = {
    { "MAC", true, nullptr, "AA:BB:CC:DD:EE:FF",
      AtArgType::Regex, 0, 0, nullptr, "^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$" }
};

inline constexpr AtActionArg kArgs_StaIP[] = {
    { "IP",      true, nullptr, "e.g. 192.168.1.100", AtArgType::String, 0, 0, nullptr, nullptr },
    { "Gateway", true, nullptr, "e.g. 192.168.1.1",   AtArgType::String, 0, 0, nullptr, nullptr },
    { "Netmask", true, nullptr, "e.g. 255.255.255.0", AtArgType::String, 0, 0, nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_ApIP_Defaults[] = {
    { "IP",      true, "192.168.4.1",   nullptr, AtArgType::String, 0, 0, nullptr, nullptr },
    { "Gateway", true, "192.168.4.1",   nullptr, AtArgType::String, 0, 0, nullptr, nullptr },
    { "Netmask", true, "255.255.255.0", nullptr, AtArgType::String, 0, 0, nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_Length_2048[] = {
    { "Length", true, "12", nullptr, AtArgType::Uint, 1, 2048, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_Length_4096[] = {
    { "Length", true, "64", nullptr, AtArgType::Uint, 1, 4096, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_SegmentID[] = {
    { "SegmentID", true, "1", nullptr, AtArgType::Uint, 0, 65535, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_ServerMode_Port[] = {
    { "Mode", true, "1", "0=stop,1=start", AtArgType::Bool01, 0, 0, nullptr, nullptr },
    { "Port", true, "8080", nullptr,       AtArgType::Uint,   1, 65535, nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_TransparentMode[] = {
    { "Mode", true, "1", "0=normal,1=transparent", AtArgType::Bool01, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_SaveTransLink[] = {
    { "Enable", true, "1",            "0/1",         AtArgType::Bool01, 0, 0, nullptr, nullptr },
    { "Host",   true, "example.com",  nullptr,       AtArgType::String, 0, 0, nullptr, nullptr },
    { "Port",   true, "80",           nullptr,       AtArgType::Uint,   1, 65535,       nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_TimeoutSec[] = {
    { "Seconds", true, "300", nullptr, AtArgType::Uint, 0, 7200, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_HostOnly[] = {
    { "Host", true, "example.com", nullptr, AtArgType::String, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_HostPort[] = {
    { "Host", true, nullptr, "example.com", AtArgType::String, 0, 0, nullptr, nullptr },
    { "Port", true, "80",    nullptr,       AtArgType::Uint,   1, 65535,       nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_CID_Only[] = {
    { "CID", true, "1", nullptr, AtArgType::Uint, 1, 16, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CID_APN[] = {
    { "CID", true, "1",        nullptr, AtArgType::Uint, 1, 16, nullptr, nullptr },
    { "APN", true, "internet", "APN name / regex", AtArgType::Regex, 0, 0, nullptr, "^[A-Za-z0-9._-]+$" },
};

inline constexpr AtActionArg kArgs_QIOPEN[] = {
    { "ContextId", true, "1",  nullptr, AtArgType::Uint, 1, 16,    nullptr, nullptr },
    { "ConnId",    true, "0",  nullptr, AtArgType::Uint, 0, 11,    nullptr, nullptr },
    { "Host",      true, nullptr, "example.com", AtArgType::String, 0, 0, nullptr, nullptr },
    { "Port",      true, "80",  nullptr, AtArgType::Uint, 1, 65535, nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_QISEND[] = {
    { "ConnId", true, "0",  nullptr, AtArgType::Uint, 0, 11,   nullptr, nullptr },
    { "Length", true, "12", nullptr, AtArgType::Uint, 1, 1500, nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_QIRD[] = {
    { "ConnId", true, "0",   nullptr, AtArgType::Uint, 0, 11,   nullptr, nullptr },
    { "Length", true, "512", nullptr, AtArgType::Uint, 1, 1500, nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_ConnIdOnly[] = {
    { "ConnId", true, "0", nullptr, AtArgType::Uint, 0, 11, nullptr, nullptr }
};

// ---------- INTERNET TCP/IP ----------
inline constexpr AtActionItem kAtTcpIpActions[] = {
    // Wi-Fi (ESP-AT)
    { E("📶"), "Wi-Fi mode (STA/AP)",     "AT+CWMODE=%1",             AtStandard::Hayes,
      "AT+CWMODE=1", false, kArgs_Mode_STA_AP, AT_COUNT_OF(kArgs_Mode_STA_AP) },

    { E("🔎"), "Wi-Fi scan",              "AT+CWLAP",                 AtStandard::Hayes,
      "AT+CWLAP",   false, AT_NOARGS },

    { E("🔗"), "Wi-Fi join AP",           "AT+CWJAP=\"%1\",\"%2\"",   AtStandard::Hayes,
      "AT+CWJAP=\"MySSID\",\"MyPass\"", false, kArgs_SSID_Password, AT_COUNT_OF(kArgs_SSID_Password) },

    { E("🧹"), "Wi-Fi disconnect",        "AT+CWQAP",                 AtStandard::Hayes,
      "AT+CWQAP", false, AT_NOARGS },

    { E("🌐"), "IP local addr",           "AT+CIFSR",                 AtStandard::SIMCom,
      "AT+CIFSR", false, AT_NOARGS },

    { E("📶"), "Wi-Fi mode (DEF)",        "AT+CWMODE_DEF=%1",         AtStandard::Hayes,
      "AT+CWMODE_DEF=1", false, kArgs_Mode_STA_AP, AT_COUNT_OF(kArgs_Mode_STA_AP) },

    { E("🔗"), "Wi-Fi join AP (CUR)",     "AT+CWJAP_CUR=\"%1\",\"%2\"", AtStandard::Hayes,
      "AT+CWJAP_CUR=\"MySSID\",\"MyPass\"", false, kArgs_SSID_Password, AT_COUNT_OF(kArgs_SSID_Password) },

    { E("🔗"), "Wi-Fi join AP (DEF)",     "AT+CWJAP_DEF=\"%1\",\"%2\"", AtStandard::Hayes,
      "AT+CWJAP_DEF=\"MySSID\",\"MyPass\"", false, kArgs_SSID_Password, AT_COUNT_OF(kArgs_SSID_Password) },

    { E("🛜"), "SoftAP config (CUR)",     "AT+CWSAP_CUR=\"%1\",\"%2\",%3,%4", AtStandard::Hayes,
      "AT+CWSAP_CUR=\"MyAP\",\"apPass\",6,4", false, kArgs_SoftAP, AT_COUNT_OF(kArgs_SoftAP) },

    { E("🛜"), "SoftAP config (DEF)",     "AT+CWSAP_DEF=\"%1\",\"%2\",%3,%4", AtStandard::Hayes,
      "AT+CWSAP_DEF=\"MyAP\",\"apPass\",6,4", false, kArgs_SoftAP, AT_COUNT_OF(kArgs_SoftAP) },

    { E("👥"), "List STA on softAP",      "AT+CWLIF",                 AtStandard::Hayes,
      "AT+CWLIF", false, AT_NOARGS },

    { E("🔧"), "DHCP (CUR)",             "AT+CWDHCP_CUR=%1,%2",      AtStandard::Hayes,
      "AT+CWDHCP_CUR=1,1", false, kArgs_DHCP_Mode_Enable, AT_COUNT_OF(kArgs_DHCP_Mode_Enable) },

    { E("🔧"), "DHCP (DEF)",             "AT+CWDHCP_DEF=%1,%2",      AtStandard::Hayes,
      "AT+CWDHCP_DEF=1,1", false, kArgs_DHCP_Mode_Enable, AT_COUNT_OF(kArgs_DHCP_Mode_Enable) },

    { E("🔁"), "Auto-connect on boot",    "AT+CWAUTOCONN=%1",         AtStandard::Hayes,
      "AT+CWAUTOCONN=1", false, kArgs_EnableBool2, AT_COUNT_OF(kArgs_EnableBool) },

    { E("🆔"), "STA MAC (CUR)",           "AT+CIPSTAMAC_CUR=\"%1\"",  AtStandard::Hayes,
      "AT+CIPSTAMAC_CUR=\"AA:BB:CC:DD:EE:FF\"", false, kArgs_MAC, AT_COUNT_OF(kArgs_MAC) },

    { E("🆔"), "STA MAC (DEF)",           "AT+CIPSTAMAC_DEF=\"%1\"",  AtStandard::Hayes,
      "AT+CIPSTAMAC_DEF=\"AA:BB:CC:DD:EE:FF\"", false, kArgs_MAC, AT_COUNT_OF(kArgs_MAC) },

    { E("🆔"), "AP  MAC (CUR)",           "AT+CIPAPMAC_CUR=\"%1\"",   AtStandard::Hayes,
      "AT+CIPAPMAC_CUR=\"AA:BB:CC:DD:EE:FF\"", false, kArgs_MAC, AT_COUNT_OF(kArgs_MAC) },

    { E("🆔"), "AP  MAC (DEF)",           "AT+CIPAPMAC_DEF=\"%1\"",   AtStandard::Hayes,
      "AT+CIPAPMAC_DEF=\"AA:BB:CC:DD:EE:FF\"", false, kArgs_MAC, AT_COUNT_OF(kArgs_MAC) },

    { E("🌐"), "STA IP (CUR)",            "AT+CIPSTA_CUR=\"%1\",\"%2\",\"%3\"", AtStandard::Hayes,
      "AT+CIPSTA_CUR=\"192.168.1.100\",\"192.168.1.1\",\"255.255.255.0\"", false,
      kArgs_StaIP, AT_COUNT_OF(kArgs_StaIP) },

    { E("🌐"), "STA IP (DEF)",            "AT+CIPSTA_DEF=\"%1\",\"%2\",\"%3\"", AtStandard::Hayes,
      "AT+CIPSTA_DEF=\"192.168.1.100\",\"192.168.1.1\",\"255.255.255.0\"", false,
      kArgs_StaIP, AT_COUNT_OF(kArgs_StaIP) },

    { E("🏠"), "AP  IP (CUR)",            "AT+CIPAP_CUR=\"%1\",\"%2\",\"%3\"",  AtStandard::Hayes,
      "AT+CIPAP_CUR=\"192.168.4.1\",\"192.168.4.1\",\"255.255.255.0\"", false,
      kArgs_ApIP_Defaults, AT_COUNT_OF(kArgs_ApIP_Defaults) },

    { E("🏠"), "AP  IP (DEF)",            "AT+CIPAP_DEF=\"%1\",\"%2\",\"%3\"",  AtStandard::Hayes,
      "AT+CIPAP_DEF=\"192.168.4.1\",\"192.168.4.1\",\"255.255.255.0\"", false,
      kArgs_ApIP_Defaults, AT_COUNT_OF(kArgs_ApIP_Defaults) },

    { E("🧠"), "SmartConfig start",       "AT+CWSTARTSMART=%1",       AtStandard::Hayes,
      "AT+CWSTARTSMART=1", false,
      (AtActionArg[]){
        { "Type", true, "1", "0=ESP-TOUCH,1=AIRKISS,3=BOTH", AtArgType::Choice, 0, 0, "0|1|3", nullptr }
      }, 1 },

    { E("🧠"), "SmartConfig stop",        "AT+CWSTOPSMART",           AtStandard::Hayes,
      "AT+CWSTOPSMART", false, AT_NOARGS },

    { E("📌"), "Conn status",            "AT+CIPSTATUS",             AtStandard::Hayes,
      "AT+CIPSTATUS", false, AT_NOARGS },

    { E("📩"), "Send (CIPSENDEX)",       "AT+CIPSENDEX=%1",          AtStandard::Hayes,
      "AT+CIPSENDEX=12", false, kArgs_Length_2048, AT_COUNT_OF(kArgs_Length_2048) },

    { E("📦"),  "Send to buffer",         "AT+CIPSENDBUF=%1",         AtStandard::Hayes,
      "AT+CIPSENDBUF=64", false, kArgs_Length_4096, AT_COUNT_OF(kArgs_Length_4096) },

    { E("⚡"), "Reset seg IDs",          "AT+CIPBUFRESET",           AtStandard::Hayes,
      "AT+CIPBUFRESET", false, AT_NOARGS },

    { E("📊"),  "Buffer status",          "AT+CIPBUFSTATUS",          AtStandard::Hayes,
      "AT+CIPBUFSTATUS", false, AT_NOARGS },

    { E("🔎"),  "Check segment",          "AT+CIPCHECKSEQ=%1",        AtStandard::Hayes,
      "AT+CIPCHECKSEQ=1", false, kArgs_SegmentID, AT_COUNT_OF(kArgs_SegmentID) },

    { E("🔀"),  "Multi-conn (CIPMUX)",    "AT+CIPMUX=%1",             AtStandard::Hayes,
      "AT+CIPMUX=1", false, kArgs_EnableBool, AT_COUNT_OF(kArgs_EnableBool) },

    { E("🏠"), "Server mode",            "AT+CIPSERVER=%1,%2",       AtStandard::Hayes,
      "AT+CIPSERVER=1,8080", false, kArgs_ServerMode_Port, AT_COUNT_OF(kArgs_ServerMode_Port) },

    { E("🔁"),  "Transparent mode",       "AT+CIPMODE=%1",            AtStandard::Hayes,
      "AT+CIPMODE=1", false, kArgs_TransparentMode, AT_COUNT_OF(kArgs_TransparentMode) },

    { E("💾"),  "Save trans link",        "AT+SAVETRANSLINK=%1,\"%2\",%3", AtStandard::Hayes,
      "AT+SAVETRANSLINK=1,\"example.com\",80", false, kArgs_SaveTransLink, AT_COUNT_OF(kArgs_SaveTransLink) },

    { E("🕐"), "Server timeout",         "AT+CIPSTO=%1",             AtStandard::Hayes,
      "AT+CIPSTO=300", false, kArgs_TimeoutSec, AT_COUNT_OF(kArgs_TimeoutSec) },

    { E("📩"), "Firmware update (net)",  "AT+CIUPDATE",              AtStandard::Hayes,
      "AT+CIUPDATE", true, AT_NOARGS },

    { E("🏓"),  "Ping host",              "AT+PING=\"%1\"",           AtStandard::Hayes,
      "AT+PING=\"example.com\"", false, kArgs_HostOnly, AT_COUNT_OF(kArgs_HostOnly) },

    // SIMCom TCP
    { E("🔁"),  "Open TCP",               "AT+CIPSTART=\"TCP\",\"%1\",%2", AtStandard::SIMCom,
      "AT+CIPSTART=\"TCP\",\"example.com\",80", false, kArgs_HostPort, AT_COUNT_OF(kArgs_HostPort) },

    { E("📩"), "Send (enter data)",      "AT+CIPSEND",               AtStandard::SIMCom,
      "AT+CIPSEND", false, AT_NOARGS },

    { E("🧹"),  "Close",                  "AT+CIPCLOSE",              AtStandard::SIMCom,
      "AT+CIPCLOSE", false, AT_NOARGS },

    { E("🧯"),  "Shutdown stack",         "AT+CIPSHUT",               AtStandard::SIMCom,
      "AT+CIPSHUT", false, AT_NOARGS },

    // Quectel QI*
    { E("🔧"), "Define PDP (QI)",        "AT+QICSGP=%1,1,\"%2\",\"\",\"\",1", AtStandard::Quectel,
      "AT+QICSGP=1,1,\"internet\",\"\",\"\",1", false, kArgs_CID_APN, AT_COUNT_OF(kArgs_CID_APN) },

    { E("⚡"), "Activate PDP (QI)",      "AT+QIACT=%1",              AtStandard::Quectel,
      "AT+QIACT=1", false, kArgs_CID_Only, AT_COUNT_OF(kArgs_CID_Only) },

    { E("⚡"),  "Open socket",            "AT+QIOPEN=%1,%2,\"TCP\",\"%3\",%4,0,1", AtStandard::Quectel,
      "AT+QIOPEN=1,0,\"TCP\",\"example.com\",80,0,1", false, kArgs_QIOPEN, AT_COUNT_OF(kArgs_QIOPEN) },

    { E("📩"), "Send len",               "AT+QISEND=%1,%2",          AtStandard::Quectel,
      "AT+QISEND=0,12", false, kArgs_QISEND, AT_COUNT_OF(kArgs_QISEND) },

    { E("📥"),  "Read len",               "AT+QIRD=%1,%2",            AtStandard::Quectel,
      "AT+QIRD=0,512", false, kArgs_QIRD, AT_COUNT_OF(kArgs_QIRD) },

    { E("🧹"),  "Close",                  "AT+QICLOSE=%1",            AtStandard::Quectel,
      "AT+QICLOSE=0", false, kArgs_ConnIdOnly, AT_COUNT_OF(kArgs_ConnIdOnly) },
};

inline constexpr size_t kAtTcpIpActionsCount = AT_COUNT_OF(kAtTcpIpActions);

// HTTP(S)
inline constexpr AtActionArg kArgs_HTTPPARA_URL[] = {
  { "URL", true, nullptr, "http://...", AtArgType::String, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_HTTPACTION[] = {
  { "Method", true, "0", "0=GET,1=POST,2=HEAD", AtArgType::Choice, 0, 0, "0|1|2", nullptr }
};

inline constexpr AtActionArg kArgs_QHTTPGET[] = {
  { "URL",        true, nullptr, "http://...", AtArgType::String, 0, 0, nullptr, nullptr },
  { "Timeout (s)",true, "60",    nullptr,      AtArgType::Uint,   1, 600, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_QHTTPPOST[] = {
  { "URL",         true, nullptr, "http://...",                 AtArgType::String, 0, 0,      nullptr, nullptr },
  { "CT length",   true, "9",     "e.g. length of 'text/plain'",AtArgType::Uint,   1, 4096,  nullptr, nullptr },
  { "Data length", true, "128",   nullptr,                      AtArgType::Uint,   1, 1048576,nullptr, nullptr },
  { "Timeout (s)", true, "60",    nullptr,                      AtArgType::Uint,   1, 600,   nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_QHTTPREAD[] = {
  { "Timeout (s)", true, "60", nullptr, AtArgType::Uint, 1, 600, nullptr, nullptr }
};

inline constexpr AtActionItem kAtHttpActions[] = {
  { E("🚀"), "HTTP init",           "AT+HTTPINIT",                     AtStandard::SIMCom,
    "AT+HTTPINIT", false, AT_NOARGS },

  { E("🔧"), "HTTP param URL",     "AT+HTTPPARA=\"URL\",\"%1\"",      AtStandard::SIMCom,
    "AT+HTTPPARA=\"URL\",\"http://example.com/\"", false,
    kArgs_HTTPPARA_URL, AT_COUNT_OF(kArgs_HTTPPARA_URL) },

  { E("📌"), "HTTP action",        "AT+HTTPACTION=%1",                AtStandard::SIMCom,
    "AT+HTTPACTION=0", false,
    kArgs_HTTPACTION, AT_COUNT_OF(kArgs_HTTPACTION) },

  { E("📥"), "HTTP read",           "AT+HTTPREAD",                     AtStandard::SIMCom,
    "AT+HTTPREAD", false, AT_NOARGS },

  { E("🧹"), "HTTP term",           "AT+HTTPTERM",                     AtStandard::SIMCom,
    "AT+HTTPTERM", false, AT_NOARGS },

  { E("🚀"), "HTTP GET",            "AT+QHTTPGET=\"%1\",%2",           AtStandard::Quectel,
    "AT+QHTTPGET=\"http://example.com\",60", false,
    kArgs_QHTTPGET, AT_COUNT_OF(kArgs_QHTTPGET) },

  { E("📩"), "HTTP POST",          "AT+QHTTPPOST=\"%1\",%2,%3,%4",    AtStandard::Quectel,
    "AT+QHTTPPOST=\"http://example.com\",9,128,60", false,
    kArgs_QHTTPPOST, AT_COUNT_OF(kArgs_QHTTPPOST) },

  { E("📥"), "HTTP read",           "AT+QHTTPREAD=%1",                 AtStandard::Quectel,
    "AT+QHTTPREAD=60", false,
    kArgs_QHTTPREAD, AT_COUNT_OF(kArgs_QHTTPREAD) },
};
inline constexpr size_t kAtHttpActionsCount = AT_COUNT_OF(kAtHttpActions);

// GNSS
inline constexpr AtActionArg kArgs_CGNSPWR[] = {
  { "Power", true, "1", "0/1", AtArgType::Bool01, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CGNSLOC[] = {
  { "Mode", true, "2", "module-specific", AtArgType::Uint, 0, 10, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_QGPS[] = {
  { "Power", true, "1", "0/1", AtArgType::Bool01, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_QGPSCFG[] = {
  { "Item",  true, "gnssconfig", nullptr, AtArgType::String, 0, 0, nullptr, nullptr },
  { "Value", true, "3",          nullptr, AtArgType::Uint,   0, 100, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_QGPSLOC[] = {
  { "Timeout (s)", true, "30", nullptr, AtArgType::Uint, 1, 600, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_QGPSGNMEA[] = {
  { "Sentence", true, "GGA", "GGA/RMC/GSV/...", AtArgType::Choice, 0, 0, "GGA|RMC|GSV|GSA|VTG|GLL", nullptr }
};

// ---- GNSS actions (flash) ----
inline constexpr AtActionItem kAtGnssActions[] = {
  { E("🧭"), "GNSS power",    "AT+CGNSPWR=%1",          AtStandard::SIMCom,
    "AT+CGNSPWR=1", false, kArgs_CGNSPWR,  AT_COUNT_OF(kArgs_CGNSPWR) },

  { E("📡"), "GNSS info",     "AT+CGNSINF",             AtStandard::SIMCom,
    "AT+CGNSINF",  false, AT_NOARGS },

  { E("📍"), "GNSS location", "AT+CGNSLOC=%1",          AtStandard::SIMCom,
    "AT+CGNSLOC=2", false, kArgs_CGNSLOC,   AT_COUNT_OF(kArgs_CGNSLOC) },

  { E("🧭"), "GPS on/off",    "AT+QGPS=%1",             AtStandard::Quectel,
    "AT+QGPS=1",   false, kArgs_QGPS,       AT_COUNT_OF(kArgs_QGPS) },

  { E("🔧"), "GPS config",   "AT+QGPSCFG=\"%1\",%2",   AtStandard::Quectel,
    "AT+QGPSCFG=\"gnssconfig\",3", false, kArgs_QGPSCFG, AT_COUNT_OF(kArgs_QGPSCFG) },

  { E("📍"), "GPS location",  "AT+QGPSLOC=%1",          AtStandard::Quectel,
    "AT+QGPSLOC=30", false, kArgs_QGPSLOC,  AT_COUNT_OF(kArgs_QGPSLOC) },

  { E("🧾"), "NMEA output",   "AT+QGPSGNMEA=\"%1\"",    AtStandard::Quectel,
    "AT+QGPSGNMEA=\"GGA\"", false, kArgs_QGPSGNMEA, AT_COUNT_OF(kArgs_QGPSGNMEA) },
};
inline constexpr size_t kAtGnssActionsCount = AT_COUNT_OF(kAtGnssActions);


// LoRa
inline constexpr AtActionArg kArgs_REGION[] = {
    { "Region", true, "EU868",
      "EU868/US915/AS923/AU915/KR920/IN865",
      AtArgType::Choice, 0, 0,
      "EU868|US915|AS923|AU915|KR920|IN865", nullptr }
};

inline constexpr AtActionArg kArgs_DEVEUI[] = {
    { "DevEUI hex", true, nullptr, "16 hex (8B), no spaces",
      AtArgType::Regex, 0, 0, nullptr, "^[0-9A-Fa-f]{16}$" }
};

inline constexpr AtActionArg kArgs_APPEUI[] = {
    { "AppEUI hex", true, nullptr, "16 hex (8B), no spaces",
      AtArgType::Regex, 0, 0, nullptr, "^[0-9A-Fa-f]{16}$" }
};

inline constexpr AtActionArg kArgs_APPKEY[] = {
    { "AppKey hex", true, nullptr, "32 hex (16B), no spaces",
      AtArgType::Regex, 0, 0, nullptr, "^[0-9A-Fa-f]{32}$" }
};

inline constexpr AtActionArg kArgs_DEVADDR[] = {
    { "DevAddr hex", true, nullptr, "8 hex (4B), no spaces",
      AtArgType::Regex, 0, 0, nullptr, "^[0-9A-Fa-f]{8}$" }
};

inline constexpr AtActionArg kArgs_NWKSKEY[] = {
    { "NwkSKey hex", true, nullptr, "32 hex (16B), no spaces",
      AtArgType::Regex, 0, 0, nullptr, "^[0-9A-Fa-f]{32}$" }
};

inline constexpr AtActionArg kArgs_APPSKEY[] = {
    { "AppSKey hex", true, nullptr, "32 hex (16B), no spaces",
      AtArgType::Regex, 0, 0, nullptr, "^[0-9A-Fa-f]{32}$" }
};

inline constexpr AtActionArg kArgs_BOOL01[] = {
    { "ADR", true, "1", "0/1",
      AtArgType::Bool01, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_DR[] = {
    { "DR index", true, "5", "0..15 (region-dependent)",
      AtArgType::Uint, 0, 15, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CLASS[] = {
    { "Class", true, "A", "A/B/C",
      AtArgType::Choice, 0, 0, "A|B|C", nullptr }
};

inline constexpr AtActionArg kArgs_SEND[] = {
    { "FPort", true, "1", "1..223",
      AtArgType::Uint, 1, 223, nullptr, nullptr },
    { "Payload (hex bytes)", true, nullptr, "e.g. 01 AA 02",
      AtArgType::HexBytes, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionItem kAtLoraActions[] = {
    { E("📌"), "Module version",     "AT+VER",               AtStandard::Hayes,
      "AT+VER", false, AT_NOARGS },

    { E("🌍"),  "Set region",         "AT+REGION=\"%1\"",     AtStandard::Hayes,
      "AT+REGION=\"EU868\"", false,
      kArgs_REGION,  AT_COUNT_OF(kArgs_REGION) },

    { E("🪛"), "Set DevEUI (OTAA)", "AT+DEVEUI=%1",         AtStandard::Hayes,
      "AT+DEVEUI=0011223344556677", false,
      kArgs_DEVEUI,  AT_COUNT_OF(kArgs_DEVEUI) },

    { E("🪛"), "Set AppEUI (OTAA)", "AT+APPEUI=%1",         AtStandard::Hayes,
      "AT+APPEUI=0102030405060708", false,
      kArgs_APPEUI,  AT_COUNT_OF(kArgs_APPEUI) },

    { E("🔑"),  "Set AppKey (OTAA)",  "AT+APPKEY=%1",         AtStandard::Hayes,
      "AT+APPKEY=00112233445566778899AABBCCDDEEFF", false,
      kArgs_APPKEY,  AT_COUNT_OF(kArgs_APPKEY) },

    { E("📎"), "Set DevAddr (ABP)", "AT+DEVADDR=%1",        AtStandard::Hayes,
      "AT+DEVADDR=26011BDA", false,
      kArgs_DEVADDR, AT_COUNT_OF(kArgs_DEVADDR) },

    { E("🔑"), "Set NwkSKey (ABP)", "AT+NWKSKEY=%1",        AtStandard::Hayes,
      "AT+NWKSKEY=00112233445566778899AABBCCDDEEFF", false,
      kArgs_NWKSKEY, AT_COUNT_OF(kArgs_NWKSKEY) },

    { E("🔑"), "Set AppSKey (ABP)", "AT+APPSKEY=%1",        AtStandard::Hayes,
      "AT+APPSKEY=00112233445566778899AABBCCDDEEFF", false,
      kArgs_APPSKEY, AT_COUNT_OF(kArgs_APPSKEY) },

    { E("🔗"),  "Join (OTAA)",        "AT+JOIN",              AtStandard::Hayes,
      "AT+JOIN", false, AT_NOARGS },

    { E("🔁"),  "ADR on/off",         "AT+ADR=%1",            AtStandard::Hayes,
      "AT+ADR=1", false,
      kArgs_BOOL01, AT_COUNT_OF(kArgs_BOOL01) },

    { E("📶"),  "Data rate (DR)",     "AT+DR=%1",             AtStandard::Hayes,
      "AT+DR=5", false,
      kArgs_DR, AT_COUNT_OF(kArgs_DR) },

    { E("📎"), "Device class",       "AT+CLASS=%1",          AtStandard::Hayes,
      "AT+CLASS=A", false,
      kArgs_CLASS, AT_COUNT_OF(kArgs_CLASS) },

    { E("📩"), "Send (unconfirmed)","AT+SEND=%1,%2",         AtStandard::Hayes,
      "AT+SEND=1,01 AA 02", false,
      kArgs_SEND, AT_COUNT_OF(kArgs_SEND) },

    { E("✅"),  "Send (confirmed)",    "AT+CSEND=%1,%2",       AtStandard::Hayes,
      "AT+CSEND=1,01 AA 02", false,
      kArgs_SEND, AT_COUNT_OF(kArgs_SEND) },

    { E("🔍"),  "Join status",         "AT+NJS?",             AtStandard::Hayes,
      "AT+NJS?", false, AT_NOARGS },
};
inline constexpr size_t kAtLoraActionsCount = AT_COUNT_OF(kAtLoraActions);


// Audio (3GPP)
inline constexpr AtActionArg kArgs_CLVL_Level[] = {
    { "Level", true, "5", "0..15 (module-dependent)",
      AtArgType::Uint, 0, 15, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CMUT_Mute[] = {
    { "Mute", true, "1", "0=off,1=on",
      AtArgType::Bool01, 0, 0, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CRSL_Level[] = {
    { "Level", true, "2", "0..5/7/100 (depends module)",
      AtArgType::Uint, 0, 100, nullptr, nullptr }
};

inline constexpr AtActionArg kArgs_CHFA_Path[] = {
    { "Path", true, "1", "0=handset,1=speaker,2=headset",
      AtArgType::Choice, 0, 0, "0|1|2", nullptr }
};

inline constexpr AtActionItem kAtAudioActions[] = {
    { E("🔊"), "Speaker volume",          "AT+CLVL=%1", AtStandard::ThreeGPP,
      "AT+CLVL=5", false,
      kArgs_CLVL_Level, AT_COUNT_OF(kArgs_CLVL_Level) },

    { E("🤫"), "Mic mute",                 "AT+CMUT=%1", AtStandard::ThreeGPP,
      "AT+CMUT=1", false,
      kArgs_CMUT_Mute, AT_COUNT_OF(kArgs_CMUT_Mute) },

    { E("🔔"), "Ring volume",              "AT+CRSL=%1", AtStandard::ThreeGPP,
      "AT+CRSL=2", false,
      kArgs_CRSL_Level, AT_COUNT_OF(kArgs_CRSL_Level) },

    { E("🔈"), "Audio route (SIMCom)",     "AT+CHFA=%1", AtStandard::SIMCom,
      "AT+CHFA=1", false,
      kArgs_CHFA_Path, AT_COUNT_OF(kArgs_CHFA_Path) },
};
inline constexpr size_t kAtAudioActionsCount = AT_COUNT_OF(kAtAudioActions);


// Power / PSM (3GPP + 2G vendor)
inline constexpr AtActionArg kArgs_CPSMS_Timers[] = {
    { "TAU (T3412 bin8)",  true, "00100001", "8 bits binaires",
      AtArgType::Regex, 0, 0, nullptr, "^[01]{8}$" },
    { "ActiveTime (T3324)", true, "00000101", "8 bits binaires",
      AtArgType::Regex, 0, 0, nullptr, "^[01]{8}$" }
};

inline constexpr AtActionArg kArgs_CEDRXS_Set[] = {
    { "Mode",  true, "2",  "0=off,2=on",
      AtArgType::Choice, 0, 0, "0|2", nullptr },
    { "AcT",   true, "4",  "4=LTE-M,5=NB-IoT",
      AtArgType::Choice, 0, 0, "4|5", nullptr },
    { "eDRX",  true, "0101", "4 bits binaires",
      AtArgType::Regex, 0, 0, nullptr, "^[01]{4}$" }
};

// --- Power / PSM (constexpr, pas de heap) ---
inline constexpr AtActionItem kAtPowerActions[] = {
    { E("💤"), "PSM on/off",              "AT+CPSMS=%1",                  AtStandard::ThreeGPP,
      "AT+CPSMS=1", false,
      kArgs_EnableBool, AT_COUNT_OF(kArgs_EnableBool) },

    { E("🕒"), "PSM timers",             "AT+CPSMS=1,,,\"%1\",\"%2\"",   AtStandard::ThreeGPP,
      "AT+CPSMS=1,,,\"00100001\",\"00000101\"", false,
      kArgs_CPSMS_Timers, AT_COUNT_OF(kArgs_CPSMS_Timers) },

    { E("🧩"), "eDRX set",                "AT+CEDRXS=%1,%2,\"%3\"",       AtStandard::ThreeGPP,
      "AT+CEDRXS=2,4,\"0101\"", false,
      kArgs_CEDRXS_Set, AT_COUNT_OF(kArgs_CEDRXS_Set) },

    { E("🔎"), "eDRX readback",           "AT+CEDRXRDP",                  AtStandard::ThreeGPP,
      "AT+CEDRXRDP", false, AT_NOARGS },

    { E("🌙"), "2G sleep (SIMCom)",       "AT+CSCLK=%1",                  AtStandard::SIMCom,
      "AT+CSCLK=1", false,
      kArgs_EnableBool, AT_COUNT_OF(kArgs_EnableBool) },
};

inline constexpr size_t kAtPowerActionsCount = AT_COUNT_OF(kAtPowerActions);


// Hardware I/O & Status
inline constexpr AtActionItem kAtHardwareActions[] = {
    { E("🔋"), "Battery status",        "AT+CBC",   AtStandard::ThreeGPP, "AT+CBC",   false, AT_NOARGS },
    { E("💳"), "SIM ICCID",             "AT+CCID",  AtStandard::ThreeGPP, "AT+CCID",  false, AT_NOARGS },
    { E("🧾"), "IMSI",                  "AT+CIMI",  AtStandard::ThreeGPP, "AT+CIMI",  false, AT_NOARGS },
    { E("📈"), "ADC read (SIMCom)",     "AT+CADC?", AtStandard::SIMCom,   "AT+CADC?", false, AT_NOARGS },
    { E("📇"), "Subscriber number",     "AT+CNUM",  AtStandard::ThreeGPP, "AT+CNUM",  false, AT_NOARGS },
};

inline constexpr size_t kAtHardwareActionsCount = AT_COUNT_OF(kAtHardwareActions);


// MQTT (Quectel + SIMCom)
inline constexpr AtActionArg kArgs_QMTOPEN[] = {
    { "ClientIdx", true, "0",     nullptr, AtArgType::Uint,   0,     5,     nullptr, nullptr },
    { "Host",      true, nullptr, "broker",AtArgType::String, 0,     0,     nullptr, nullptr },
    { "Port",      true, "1883",  nullptr, AtArgType::Uint,   1, 65535,     nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_QMTCONN[] = {
    { "ClientIdx", true, "0",         nullptr, AtArgType::Uint,   0, 5, nullptr, nullptr },
    { "ClientID",  true, "client123", nullptr, AtArgType::String, 0, 0, nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_QMTPUB[] = {
    { "ClientIdx", true, "0",          nullptr, AtArgType::Uint,   0, 5,     nullptr, nullptr },
    { "MsgId",     true, "1",          nullptr, AtArgType::Uint,   1, 65535, nullptr, nullptr },
    { "QoS",       true, "0",          "0/1/2", AtArgType::Choice, 0, 0,     "0|1|2", nullptr },
    { "Retain",    true, "0",          "0/1",   AtArgType::Bool01, 0, 0,     nullptr, nullptr },
    { "Topic",     true, "test/topic", nullptr, AtArgType::String, 0, 0,     nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_QMTSUB[] = {
    { "ClientIdx", true, "0",          nullptr, AtArgType::Uint,   0, 5,     nullptr, nullptr },
    { "MsgId",     true, "2",          nullptr, AtArgType::Uint,   1, 65535, nullptr, nullptr },
    { "Topic",     true, "test/topic", nullptr, AtArgType::String, 0, 0,     nullptr, nullptr },
    { "QoS",       true, "0",          "0/1/2", AtArgType::Choice, 0, 0,     "0|1|2", nullptr },
};

inline constexpr AtActionArg kArgs_QMTDISC[] = {
    { "ClientIdx", true, "0", nullptr, AtArgType::Uint, 0, 5, nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_SMPUB[] = {
    { "Topic",  true, "test/topic", nullptr, AtArgType::String, 0,       0,       nullptr, nullptr },
    { "Length", true, "5",          nullptr, AtArgType::Uint,   1, 1048576,       nullptr, nullptr },
    { "QoS",    true, "0",          "0/1/2", AtArgType::Choice, 0,       0,       "0|1|2", nullptr },
    { "Retain", true, "0",          "0/1",   AtArgType::Bool01, 0,       0,       nullptr, nullptr },
};

// --- Actions ---
inline constexpr AtActionItem kAtMqttActions[] = {
    // Quectel QMT*
    { E("🔌"), "QMT open",      "AT+QMTOPEN=%1,\"%2\",%3", AtStandard::Quectel,
      "AT+QMTOPEN=0,\"broker.hivemq.com\",1883", false,
      kArgs_QMTOPEN, AT_COUNT_OF(kArgs_QMTOPEN) },

    { E("🔐"), "QMT connect",   "AT+QMTCONN=%1,\"%2\"",    AtStandard::Quectel,
      "AT+QMTCONN=0,\"client123\"", false,
      kArgs_QMTCONN, AT_COUNT_OF(kArgs_QMTCONN) },

    { E("📨"), "QMT publish",   "AT+QMTPUB=%1,%2,%3,%4,\"%5\"", AtStandard::Quectel,
      "AT+QMTPUB=0,1,0,0,\"test/topic\"", false,
      kArgs_QMTPUB, AT_COUNT_OF(kArgs_QMTPUB) },

    { E("📥"), "QMT subscribe", "AT+QMTSUB=%1,%2,\"%3\",%4", AtStandard::Quectel,
      "AT+QMTSUB=0,2,\"test/topic\",0", false,
      kArgs_QMTSUB, AT_COUNT_OF(kArgs_QMTSUB) },

    { E("🧹"), "QMT disconnect","AT+QMTDISC=%1",          AtStandard::Quectel,
      "AT+QMTDISC=0", false,
      kArgs_QMTDISC, AT_COUNT_OF(kArgs_QMTDISC) },

    // SIMCom SM*
    { E("🔗"), "SM connect",    "AT+SMCONN",              AtStandard::SIMCom,
      "AT+SMCONN", false, AT_NOARGS },

    { E("📨"), "SM publish",    "AT+SMPUB=\"%1\",%2,%3,%4", AtStandard::SIMCom,
      "AT+SMPUB=\"test/topic\",5,0,0", false,
      kArgs_SMPUB, AT_COUNT_OF(kArgs_SMPUB) },

    { E("🧹"), "SM disconnect", "AT+SMDISC",              AtStandard::SIMCom,
      "AT+SMDISC", false, AT_NOARGS },
};
inline constexpr size_t kAtMqttActionsCount = AT_COUNT_OF(kAtMqttActions);

// File Storage (Quectel + SIMCom)
inline constexpr AtActionArg kArgs_QFUPL[] = {
    { "Path",  true, "/dat/file.bin", nullptr, AtArgType::String, 0,        0,        nullptr, nullptr },
    { "Bytes", true, "1024",          nullptr, AtArgType::Uint,   1,  10485760,        nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_QFDWL[] = {
    { "Path",  true, "/dat/file.bin", nullptr, AtArgType::String, 0,        0,        nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_QFDEL[] = {
    { "Path",  true, "/dat/file.bin", nullptr, AtArgType::String, 0,        0,        nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_CFSWFILE[] = {
    { "Path",   true, "/c/file.bin",  nullptr, AtArgType::String, 0,        0,        nullptr, nullptr },
    { "Offset", true, "0",            nullptr, AtArgType::Uint,   0,  10485760,        nullptr, nullptr },
    { "Length", true, "512",          nullptr, AtArgType::Uint,   1,  10485760,        nullptr, nullptr },
    { "Mode",   true, "10",  "vendor-defined", AtArgType::Uint,   0,       255,        nullptr, nullptr },
};

inline constexpr AtActionArg kArgs_CFSRFILE[] = {
    { "Path",   true, "/c/file.bin",  nullptr, AtArgType::String, 0,        0,        nullptr, nullptr },
    { "Offset", true, "0",            nullptr, AtArgType::Uint,   0,  10485760,        nullptr, nullptr },
    { "Length", true, "512",          nullptr, AtArgType::Uint,   1,  10485760,        nullptr, nullptr },
};

// --- Actions ---
inline constexpr AtActionItem kAtFsActions[] = {
    // Quectel QF*
    { E("📂"), "List files (Quectel)",     "AT+QFLST",                 AtStandard::Quectel,
      "AT+QFLST", false, AT_NOARGS },

    { E("🔧"), "Upload UART (Quectel)",   "AT+QFUPL=\"%1\",%2",       AtStandard::Quectel,
      "AT+QFUPL=\"/dat/cert.pem\",1024",   false,
      kArgs_QFUPL,   AT_COUNT_OF(kArgs_QFUPL) },

    { E("🔧"), "Download UART (Quectel)", "AT+QFDWL=\"%1\"",          AtStandard::Quectel,
      "AT+QFDWL=\"/dat/file.bin\"",        false,
      kArgs_QFDWL,   AT_COUNT_OF(kArgs_QFDWL) },

    { E("💣"), "Delete file (Quectel)",   "AT+QFDEL=\"%1\"",          AtStandard::Quectel,
      "AT+QFDEL=\"/dat/file.bin\"",        false,
      kArgs_QFDEL,   AT_COUNT_OF(kArgs_QFDEL) },

    // SIMCom CFS*
    { E("📌"), "Write file (SIMCom)",     "AT+CFSWFILE=\"%1\",%2,%3,%4", AtStandard::SIMCom,
      "AT+CFSWFILE=\"/c/cert.pem\",0,512,10", false,
      kArgs_CFSWFILE, AT_COUNT_OF(kArgs_CFSWFILE) },

    { E("📖"), "Read file (SIMCom)",       "AT+CFSRFILE=\"%1\",%2,%3", AtStandard::SIMCom,
      "AT+CFSRFILE=\"/c/file.bin\",0,512", false,
      kArgs_CFSRFILE, AT_COUNT_OF(kArgs_CFSRFILE) },
};
inline constexpr size_t kAtFsActionsCount = AT_COUNT_OF(kAtFsActions);

// ================== Selector ==================

inline AtActionSlice getAtActionsFor(AtMode m) noexcept {
    switch (m) {
        case AtMode::Config:        return AT_ACTIONS_OF(kAtConfigActions);
        case AtMode::Info:          return AT_ACTIONS_OF(kAtInfoActions);
        case AtMode::SimPin:        return AT_ACTIONS_OF(kAtSimPinActions);
        case AtMode::Network:       return AT_ACTIONS_OF(kAtNetworkActions);
        case AtMode::Calls:         return AT_ACTIONS_OF(kAtCallsActions);
        case AtMode::Sms:           return AT_ACTIONS_OF(kAtSmsActions);
        case AtMode::Phonebook:     return AT_ACTIONS_OF(kAtPhonebookActions);
        case AtMode::Ussd:          return AT_ACTIONS_OF(kAtUssdActions);
        case AtMode::Data:          return AT_ACTIONS_OF(kAtDataActions);
        case AtMode::TimeClock:     return AT_ACTIONS_OF(kAtTimeClockActions);
        case AtMode::TcpIp_Vendor:  return AT_ACTIONS_OF(kAtTcpIpActions);
        case AtMode::Http_Vendor:   return AT_ACTIONS_OF(kAtHttpActions);
        case AtMode::Gnss_Vendor:   return AT_ACTIONS_OF(kAtGnssActions);
        case AtMode::Lora:          return AT_ACTIONS_OF(kAtLoraActions);
        case AtMode::Audio:         return AT_ACTIONS_OF(kAtAudioActions);
        case AtMode::Power:         return AT_ACTIONS_OF(kAtPowerActions);
        case AtMode::Hardware:      return AT_ACTIONS_OF(kAtHardwareActions);
        case AtMode::Mqtt:          return AT_ACTIONS_OF(kAtMqttActions);
        case AtMode::Files:         return AT_ACTIONS_OF(kAtFsActions);
    }
    return AT_ACTIONS_OF(kAtConfigActions); // fallback
}
