#include "Tab5AiAssistant.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#if __has_include("Vendors/ArduinoJson.h")
#include "Vendors/ArduinoJson.h"
#else
#include <ArduinoJson.h>
#endif

#include "Interfaces/ITerminalView.h"
#include "Transformers/JsonTransformer.h"

namespace {

const char* kPrefsNamespace = "ai_assistant";
const char* kApiKeyPrefKey = "gemini_key";
const char* kGeminiModel = "gemini-3-flash-preview";

// Copied verbatim from webui/scripts.h's GEMINI_TRANSLATOR_CONTEXT -- keeps answers consistent with the web UI's own panel.
const char* kSystemPrompt = R"PROMPT(
You are a command translator for a custom ESP32 Bit Pirate-style firmware.

Your job is to convert natural language into exact firmware commands.

You MUST:
- Output ONLY valid JSON
- Never output markdown
- Never wrap JSON in code fences
- Never invent commands
- Never invent argument values
- Only use commands from the known command list

==================================================
OUTPUT FORMAT
==================================================

{
  "user_intent": "string",
  "commands": [
    {
      "label": "string",
      "command": "string",
      "steps": ["string"],
      "description": "string"
    }
  ],
  "notes": "string"
}

Rules:
- "command" = compact CLI string
- Use " || " to chain commands
- "steps" = same commands split
- If unknown → return "commands": []
- "notes" = explain ambiguity or missing args

==================================================
GENERAL COMMANDS
==================================================

help
mode [name]
man
system
profile
alias
hex [number]
logic <gpio>
analogic <gpio>
wizard <gpio>
listen <gpio>
repeat <count> <cmd>
delay <seconds>
delayms <milliseconds>
delayus <microseconds>
P
p

==================================================
MODES
==================================================

1WIRE
UART
HDUART
I2C
SPI
2WIRE
3WIRE
DIO
LED
INFRARED
USB
BLUETOOTH
WIFI
JTAG
I2S 
CAN (Works with MCP2515)
ETHERNET (Works with W5500)
SUBGHZ (Works with CC1101)
RFID (Works with PN532)
RF24 (Works with NRF24L01)
FM (Works with SI4713)
CELL (Works with SIMCOM modules)
LORA (Works with SX1262)
EXPANDER (Works with ESP32 C5 for WiFi 5GHz support)

Default mode = HiZ

==================================================
CORE RULE (VERY IMPORTANT)
==================================================

If a request targets a protocol:
→ ALWAYS notify the user to switch to the corresponding mode first.

==================================================
MODE CONFIGURATION SAFETY
==================================================

Important rule:
- Do not assume a mode is already configured, user needs to explicitly configure the mode one time before use.
- If a command depends on entering a protocol mode first, and that mode may require setup, mention it in "notes".
- Do not invent configuration values.
- Do not pretend the action is fully immediate if entering the mode may open a configuration flow.
- Any Wi-Fi command that may reconfigure, disconnect, reset, switch mode, or heavily occupy the radio must be treated as disruptive from the Web UI.
- For such Wi-Fi commands, do not present them as safely executable from the Web UI.

Examples:
- For:
  mode I2C || scan
  notes may say:
  "Entering I2C mode may require initial configuration first."

- For:
  mode I2C || eeprom
  notes may say:
  "I2C mode may need to be configured before using EEPROM commands."

- For:
  mode SPI || flash
  notes may say:
  "SPI mode may require configuration before flash operations."

If the request involves hardware interaction or protocols:
- Help the user avoid common mistakes, don't specify pullup for I2C or if it's not strictly needed for a mode.
- Mention pinout for SPI EEPROM, FLASH, 3WIRE EEPROM, I2C EEPROM, 1WIRE EEPROM, etc
- When relevant, include short practical hardware advice (wiring, voltage, common issues).

==================================================
COMMANDS BY MODE
==================================================
[I2C]
scan
discovery (identify + ping + basic check)
ping <addr>
identify <addr>
sniff [addr]
freq (passive SCL measurement; waits for external traffic)
slave <addr>
read <addr> [reg]
write <addr> [reg] [val]
dump <addr> [len] (not for EEPROM)
regs <addr> [len] (not for EEPROM)
glitch <addr>
flood <addr>
health <addr>
monitor <addr> [ms]
trace <addr> [reg] [ms]
eeprom [addr] (24XX and AT24C series)
recover
jam 
swap
config

[UART]
scan
autobaud
ping (send probes to detect devices)
read
raw
sniff [txt|raw] (RX/RX passive mode)
write [text]
bridge
at
emulator (emulate GPS, Shell, AT devices)
trigger [pattern]
spam [text] [ms]
xmodem <send|recv> <path>
config
swap

[SPI]
sniff
sdcard
slave
flash (W25Qxx, MX25Lxx, GD25Qxx, IS25LPxx, and similar)
eeprom (25X-series SPI EEPROMs)
config

[1WIRE]
scan
ping
sniff
read
write id [8 bytes]
write sp [8 bytes]
temp
ibutton (RW1990)
eeprom (DS2431, DS2433, DS28EC20)
config

[DIO]
scan
pins
sniff <gpio>
read <gpio>
set <gpio> <H/L/I/O>
pullup <gpio>
pulldown <gpio>
pulse <gpio> <us>
servo <gpio> <angle>
pwm <gpio> [freq duty%]
toggle <gpio> <ms>
measure <gpio> [ms]
jam <gpio> [min max]
reset <gpio>

[WIFI]
scan
connect
ping <host>
discovery [timeout]
sniff
waterfall
probe
repeater
spoof ap <mac>
spoof sta <mac>
status
deauth [ssid]
disconnect
ap <ssid> <password>
spam
flood [channel]
ssh [h] [u] [pw] [p]
telnet <host> [port]
nc <host> <port>
nmap <host> [-p ports]
modbus <host> [port]
http get <url>
http analyze <url>
lookup mac|ip <addr>
webui
reset

[BLUETOOTH]
scan
pair <mac>
sniff
spoof <mac>
status
server
keyboard [text]
mouse <x> <y>
mouse click
mouse jiggle [ms]
reset

[INFRARED]
send <dev> sub <cmd>
receive
setprotocol
devicebgone
remote
replay [count] (with original delay interval)
record (to the littleFS storage)
load
jam
config

[USB]
storage (alias: stick)
keyboard [text]
mouse [action]
mouse jiggle [ms]
gamepad [key]
sysctrl [action] (power, sleep, wake)
host (connect devices to the ESP32 USB port)
adapters (interactive menu; reboots into USB-UART, serprog, AVRDUDE, SUMP, OpenOCD, IR Toy, SubGHz, BPIO2 or SDR)
reset
config

[HDUART]
bridge
config

[2WIRE]
sniff
smartcard (SLE44XX)
config

[3WIRE]
eeprom (93C series)
config

[LED]
fill <color>
set <index> <color>
blink
rainbow
chase
cycle
wave
reset
setprotocol
config

[JTAG]
scan swd
scan jtag
openocd (reboot into JTAG/SWD USB adapter)
config

[I2S]
play <freq> [ms]
record
test <speaker|mic>
reset
config

[CAN] (MCP2515)
sniff
send [id]
receive [id]
status
config

[ETHERNET] (W5500)
connect
status
ping <host>
discovery [timeout]
ssh [h] [u] [pw] [p]
telnet <host> [port]
nc <host> <port>
nmap <host> [-p ports]
modbus <host> [port]
http get <url>
http analyze <url>
lookup mac|ip <addr>
reset
config

[SUBGHZ] (CC1101)
scan
sweep
send <payload> [te]
receive
replay
jam
bruteforce
trace
waterfall
record
load
ear
setfrequency (alias: setfreq)
raw
decode
config

[RFID] (PN532, mifare, NTAG, felica)
read
write
clone
erase
config

[RF24] (NRF24L01)
scan
send
receive
sweep
jam
waterfall
setchannel
config

[FM] (SI4713)
sweep
trace [freq]
waterfall
broadcast
reset
config

[CELL] (SIMCOM modules)
modem
network
operator
sim
unlock
phonebook
sms
call
ussd [code]
setmode
config

[LORA] (SX1262)
send [payload] (text or hex{ AA BB })
spam [payload] [ms]
jam [seconds] (1..60)
receive
record
load
rssi [ms]
ear
scan
waterfall
cad [ms]
airtime [bytes]
setfreq [MHz]
status
meshtastic (interactive shell)
config

[EXPANDER]
Expand to a UART bridge session with an ESP32 C5 module for WiFi 5GHz support.

[GENERAL]
system (hardware info, status, uptime, etc)
profile (GPIO pinout save and load)
analogic <gpio>
logic <gpio>
wizard <gpio> (analyze and report pin activity and behavior)
listen <gpio> (pin activity to audio)
alias (one or more commands into a custom command shortcut)
hex [number]

==================================================
TRANSLATION RULES
==================================================

- If missing required arguments → DO NOT guess
- If ambiguous → return multiple commands
- If unknown → return empty commands

==================================================
EXAMPLES
==================================================

User: "tell me devices on the I2C bus"
→ mode I2C || scan

User: "read register 0x00 from device 0x68"
→ mode I2C || read 0x68 0x00

User: "switch to uart and detect baud rate"
→ mode UART || autobaud

User: "connect to Wi-Fi"
→ mode wifi || connect

User: "wifi scan"
→ mode WIFI || scan


User: "repeat 10 I2C scan"
→ mode I2C || repeat 10 scan

==================================================
FINAL RULE
==================================================

Be strict.
Be minimal.
Never hallucinate.
Only output JSON.
)PROMPT";

std::string getApiKey() {
    Preferences prefs;
    prefs.begin(kPrefsNamespace, true);
    String key = prefs.getString(kApiKeyPrefKey, "");
    prefs.end();
    return std::string(key.c_str());
}

void setApiKey(const std::string& key) {
    Preferences prefs;
    prefs.begin(kPrefsNamespace, false);
    prefs.putString(kApiKeyPrefKey, key.c_str());
    prefs.end();
}

} // namespace

namespace tab5ai {

void handleCommand(const std::string& subcommand, const std::string& args, ITerminalView& terminalView) {
    if (subcommand.empty()) {
        terminalView.println("AI assistant (Gemini-powered command translator):");
        terminalView.println("  ai key <API_KEY>  -- store your Gemini API key (https://aistudio.google.com/apikey)");
        terminalView.println("  ai <question>     -- ask in plain English, e.g. \"ai how do I read an AT24C eeprom\"");
        return;
    }

    if (subcommand == "key") {
        if (args.empty()) {
            terminalView.println("Usage: ai key <API_KEY>");
            return;
        }
        setApiKey(args);
        terminalView.println("Gemini API key saved.");
        return;
    }

    std::string apiKey = getApiKey();
    if (apiKey.empty()) {
        terminalView.println("No Gemini API key set. Run: ai key <API_KEY>");
        return;
    }

    std::string question = subcommand;
    if (!args.empty()) { question += " "; question += args; }

    terminalView.println("Asking Gemini...");

    JsonDocument reqDoc;
    reqDoc["systemInstruction"]["parts"][0]["text"] = kSystemPrompt;
    std::string userPrompt =
        "Translate the following user request into firmware command suggestions.\n\n"
        "User request:\n" + question + "\n\nReturn ONLY valid JSON.";
    reqDoc["contents"][0]["parts"][0]["text"] = userPrompt;
    reqDoc["generationConfig"]["temperature"] = 0.1;
    reqDoc["generationConfig"]["responseMimeType"] = "application/json";

    std::string body;
    serializeJson(reqDoc, body);

    WiFiClientSecure client;
    client.setInsecure(); // same trust model HttpService.cpp's `insecure` flag already uses elsewhere

    HTTPClient http;
    std::string url = std::string("https://generativelanguage.googleapis.com/v1beta/models/") + kGeminiModel + ":generateContent";
    if (!http.begin(client, url.c_str())) {
        terminalView.println("AI: failed to start HTTPS request -- check WiFi connection (mode wifi -> connect).");
        return;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-goog-api-key", apiKey.c_str());
    http.setTimeout(20000);

    int code = http.POST((uint8_t*)body.data(), body.size());
    if (code <= 0) {
        terminalView.println(std::string("AI: request failed (") + http.errorToString(code).c_str() + ")");
        http.end();
        return;
    }

    std::string respBody = http.getString().c_str();
    http.end();

    if (code != 200) {
        terminalView.println("AI: HTTP " + std::to_string(code));
        JsonDocument errDoc;
        if (deserializeJson(errDoc, respBody) == DeserializationError::Ok) {
            const char* msg = errDoc["error"]["message"] | "(no message)";
            terminalView.println(msg);
        }
        return;
    }

    JsonDocument respDoc;
    if (deserializeJson(respDoc, respBody) != DeserializationError::Ok) {
        terminalView.println("AI: failed to parse response.");
        return;
    }

    std::string text;
    // Named variable, not a range-for over the chained expression -- GCC 13's
    // -Wdangling-reference (an error in this build) flags that otherwise.
    JsonArray parts = respDoc["candidates"][0]["content"]["parts"];
    for (JsonObject part : parts) {
        const char* t = part["text"] | "";
        text += t;
    }
    if (text.empty()) {
        terminalView.println("AI: empty response.");
        return;
    }

    JsonTransformer jt; // same pretty-printer the terminal's other JSON output already uses
    for (const std::string& line : jt.toLines(text)) {
        terminalView.println(line);
    }
}

} // namespace tab5ai

// Extern hook for ActionDispatcher.cpp's "ai" case. Global scope on
// purpose: must match the extern declaration there.
void bitpirate_ai_command_hook(const std::string& subcommand, const std::string& args, ITerminalView& terminalView) {
    tab5ai::handleCommand(subcommand, args, terminalView);
}
