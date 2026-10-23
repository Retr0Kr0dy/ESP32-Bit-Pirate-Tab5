#include "AtTransformer.h"
#include <algorithm>

bool AtTransformer::isOk(const std::string& raw) const
{
    auto ls = lines(raw);
    for (auto& l : ls) {
        if (l == "OK") return true;
    }
    return false;
}

bool AtTransformer::isError(const std::string& raw) const
{
    auto ls = lines(raw);
    for (auto& l : ls) {
        if (l == "ERROR" || startsWith(l, "+CME ERROR") || startsWith(l, "+CMS ERROR")) return true;
    }
    return false;
}

std::string AtTransformer::clean(const std::string& raw) const
{
    // Remove echo / OK / ERROR, keep meaningful lines
    auto ls = lines(raw);
    std::string out;

    for (auto& l : ls) {
        if (l == "OK" || l == "ERROR") continue;
        if (startsWith(l, "AT")) continue; // crude echo filter
        if (!out.empty()) out += "\n\r";
        out += l;
    }

    return out;
}

std::string AtTransformer::firstValueLine(const std::string& raw) const
{
    auto ls = lines(raw);
    for (auto& l : ls) {
        if (l == "OK" || l == "ERROR") continue;
        if (startsWith(l, "AT")) continue;
        return l;
    }
    return "";
}

// ---------------- Basic / identity ----------------

std::string AtTransformer::formatModuleInfo(const std::string& atiRaw) const
{
    std::string c = clean(atiRaw);
    if (c.empty()) return "No response.";
    return "" + c;
}

std::string AtTransformer::formatManufacturer(const std::string& cgmiRaw) const
{
    std::string v = clean(cgmiRaw);
    if (v.empty()) return "Manufacturer: no response";
    return "Manufacturer: " + v;
}

std::string AtTransformer::formatImei(const std::string& gsnRaw) const
{
    // AT+GSN usually returns digits line
    auto ls = lines(gsnRaw);
    for (auto& l : ls) {
        if (l == "OK" || l == "ERROR") continue;
        if (startsWith(l, "AT")) continue;

        bool allDigits = !l.empty() && std::all_of(l.begin(), l.end(),
            [](char c){ return (c >= '0' && c <= '9'); });

        if (allDigits) return "IMEI: " + l;
        return "IMEI: " + l;
    }
    return "IMEI: no response";
}

std::string AtTransformer::formatClock(const std::string& raw) const
{
    std::string l = firstValueLine(raw);
    if (l.empty()) return "";

    if (startsWith(l, "+CCLK:")) {
        return trim(l.substr(6)); // +CCLK:
    }

    return l; // fallback 
}

// ---------------- SIM / security ----------------

std::string AtTransformer::formatSimState(const std::string& cpinRaw) const
{
    std::string l = firstValueLine(cpinRaw);
    if (l.empty()) return "SIM state: no response";

    // Expected: +CPIN: READY / SIM PIN / SIM PUK ...
    auto pos = l.find(':');
    if (pos == std::string::npos) return "SIM state: " + l;

    std::string v = trim(l.substr(pos + 1));
    return "SIM state: " + v;
}

std::string AtTransformer::formatIccid(const std::string& ccidRaw) const
{
    // SIM800 often returns:
    //   +CCID: 8933...
    // Some modems return just the digits on one line.
    auto ls = lines(ccidRaw);

    for (auto& l : ls) {
        if (l == "OK" || l == "ERROR") continue;
        if (startsWith(l, "AT")) continue;

        if (startsWith(l, "+CCID:")) {
            auto pos = l.find(':');
            std::string digits = (pos == std::string::npos) ? "" : trim(l.substr(pos + 1));
            if (!digits.empty()) return "ICCID: " + digits;
        }

        // fallback: digits-only line
        bool allDigits = !l.empty() && std::all_of(l.begin(), l.end(),
            [](char c){ return (c >= '0' && c <= '9'); });

        if (allDigits) return "ICCID: " + l;

        // fallback: show whatever it is
        return "ICCID: " + l;
    }

    return "ICCID: no response";
}

std::string AtTransformer::formatImsi(const std::string& cimiRaw) const
{
    // AT+CIMI typically returns digits only then OK
    auto ls = lines(cimiRaw);

    for (auto& l : ls) {
        if (l == "OK" || l == "ERROR") continue;
        if (startsWith(l, "AT")) continue;

        bool allDigits = !l.empty() && std::all_of(l.begin(), l.end(),
            [](char c){ return (c >= '0' && c <= '9'); });

        if (allDigits) return "IMSI: " + l;
        return "IMSI: " + l;
    }

    return "IMSI: no response";
}

std::string AtTransformer::formatMsisdn(const std::string& cnumRaw) const
{
    if (cnumRaw.empty()) return "MSISDN: no response";

    // erreurs
    if (cnumRaw.find("+CME ERROR") != std::string::npos || cnumRaw.find("+CMS ERROR") != std::string::npos) {
        if (cnumRaw.find("operation not allowed") != std::string::npos) {
            return "MSISDN: not available (SIM/operator does not provide it)";
        }
        std::string c = clean(cnumRaw);
        if (c.empty()) c = trim(cnumRaw);
        return "MSISDN: error (" + c + ")";
    }

    auto ls = lines(cnumRaw);

    // Cherche +CNUM:
    for (auto& l : ls) {
        if (l == "OK" || l == "ERROR") continue;
        if (startsWith(l, "AT")) continue;

        if (!startsWith(l, "+CNUM:")) continue;

        // +CNUM: "alpha","number",type[,speed[,service]]
        size_t pos = l.find(':');
        if (pos == std::string::npos) return "MSISDN: parse error";

        std::string payload = trim(l.substr(pos + 1));
        if (payload.empty()) return "MSISDN: not stored on SIM";

        // 2e champ entre guillemets
        size_t q1 = payload.find('"');
        if (q1 == std::string::npos) return "MSISDN: not stored on SIM";
        size_t q2 = payload.find('"', q1 + 1);
        if (q2 == std::string::npos) return "MSISDN: parse error";

        size_t q3 = payload.find('"', q2 + 1);
        if (q3 == std::string::npos) return "MSISDN: not stored on SIM";
        size_t q4 = payload.find('"', q3 + 1);
        if (q4 == std::string::npos) return "MSISDN: parse error";

        std::string number = payload.substr(q3 + 1, q4 - (q3 + 1));
        if (number.empty()) return "MSISDN: not stored on SIM";

        return "MSISDN: " + number;
    }

    // no ligne +CNUM:
    for (auto& l : ls) {
        if (l == "OK") return "MSISDN: not stored on SIM";
    }

    // fallback
    std::string c = clean(cnumRaw);
    if (c.empty()) return "MSISDN: no response";
    return "MSISDN: " + c;
}

std::string AtTransformer::formatPinLock(const std::string& raw) const
{
    if (raw.empty()) return "PIN lock: unknown (no response)";

    // If modem returned a CME/CMS error
    if (raw.find("+CME ERROR") != std::string::npos || raw.find("+CMS ERROR") != std::string::npos) {
        std::string c = clean(raw);
        if (c.empty()) c = trim(raw);
        return "PIN lock: error (" + c + ")";
    }

    auto pos = raw.find("+CLCK:");
    if (pos == std::string::npos) return "PIN lock: unsupported";

    pos += 6;
    while (pos < raw.size() && (raw[pos] == ' ' || raw[pos] == '\t')) ++pos;
    if (pos >= raw.size()) return "PIN lock: parse error";

    char state = raw[pos];
    if (state == '1') return "PIN lock: enabled (SIM requires PIN at boot)";
    if (state == '0') return "PIN lock: disabled";
    return "PIN lock: unknown state";
}

std::string AtTransformer::formatSpn(const std::string& raw) const
{
    std::string l = firstValueLine(raw);
    if (l.empty()) return "SPN: no response";
    return "SPN: " + l;
}

std::string AtTransformer::formatSimRetries(const std::string& raw) const
{
    std::string l = firstValueLine(raw);
    if (l.empty()) return "SIM retries: no response";

    if (l.find("ERROR") != std::string::npos || l.find("+CME ERROR") != std::string::npos) {
        return "SIM retries: not supported";
    }

    int pin1, puk1, pin2, puk2;
    if (sscanf(l.c_str(), "+SPIC: %d,%d,%d,%d", &pin1, &puk1, &pin2, &puk2) == 4) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Retries: PIN(%d) PUK(%d) PIN2(%d) PUK2(%d)", pin1, puk1, pin2, puk2);
        return std::string(buf);
    }

    return "SIM retries: " + l;
}

std::string AtTransformer::formatPhonebookStorage(const std::string& raw) const
{
    std::string l = firstValueLine(raw);
    if (l.empty()) return "Phonebook: no response";

    if (l.find("ERROR") != std::string::npos || l.find("+CME ERROR") != std::string::npos) {
        return "Phonebook: not available";
    }

    // +CPBS: "SM",7,250
    size_t p = l.find("+CPBS:");
    if (p == std::string::npos) {
        return "Phonebook: " + l;
    }

    // Extract storage between quotes
    size_t q1 = l.find('"', p);
    size_t q2 = (q1 != std::string::npos) ? l.find('"', q1 + 1) : std::string::npos;

    std::string storage = (q1 != std::string::npos && q2 != std::string::npos)
                            ? l.substr(q1 + 1, q2 - q1 - 1)
                            : "";

    // Parse used,total after quotes
    int used = -1, total = -1;
    if (q2 != std::string::npos) {
        const char* s = l.c_str() + q2 + 1;
        // skip commas/spaces
        while (*s == ',' || *s == ' ') s++;
        // try parse: used,total
        if (sscanf(s, "%d,%d", &used, &total) != 2) {
            used = -1; total = -1;
        }
    }

    if (!storage.empty() && used >= 0 && total >= 0) {
        return "Phonebook: " + storage + " (" + std::to_string(used) + "/" + std::to_string(total) + " used)";
    }

    // Fallback raw
    return "Phonebook: " + l;
}

std::string AtTransformer::formatPhonebookCaps(const std::string& raw) const
{
    std::string l = firstValueLine(raw);
    if (l.empty()) return "Phonebook caps: no response";

    if (l.find("ERROR") != std::string::npos || l.find("+CME ERROR") != std::string::npos) {
        return "Phonebook caps: not available";
    }

    // +CPBR: (1-250),40,30
    size_t p = l.find("+CPBR:");
    if (p == std::string::npos) {
        return "Phonebook caps: " + l;
    }

    // Parse range and sizes 
    int minIdx = -1, maxIdx = -1, maxNumLen = -1, maxTextLen = -1;
    if (sscanf(l.c_str(), " +CPBR: (%d-%d),%d,%d", &minIdx, &maxIdx, &maxNumLen, &maxTextLen) != 4 &&
        sscanf(l.c_str(), "+CPBR: (%d-%d),%d,%d", &minIdx, &maxIdx, &maxNumLen, &maxTextLen) != 4) {
        return "Phonebook caps: " + l;
    }

    return "Phonebook caps: index " + std::to_string(minIdx) + "-" + std::to_string(maxIdx) +
           ", numberLen=" + std::to_string(maxNumLen) +
           ", nameLen=" + std::to_string(maxTextLen);
}

std::string AtTransformer::formatSmsStorage(const std::string& raw) const
{
    std::string l = firstValueLine(raw);
    if (l.empty()) return "SMS storage: no response";

    if (l.find("ERROR") != std::string::npos || l.find("+CME ERROR") != std::string::npos) {
        return "SMS storage: not available";
    }

    // +CPMS: "SM_P",0,20,"SM_P",0,20,"SM_P",0,20
    size_t p = l.find("+CPMS:");
    if (p == std::string::npos) {
        return "SMS storage: " + l;
    }

    char mem1[24] = {0}, mem2[24] = {0}, mem3[24] = {0};
    int used1 = -1, total1 = -1, used2 = -1, total2 = -1, used3 = -1, total3 = -1;

    int parsed = sscanf(l.c_str(),
                        " +CPMS: \"%23[^\"]\",%d,%d,\"%23[^\"]\",%d,%d,\"%23[^\"]\",%d,%d",
                        mem1, &used1, &total1, mem2, &used2, &total2, mem3, &used3, &total3);

    if (parsed != 9) {
        parsed = sscanf(l.c_str(),
                        "+CPMS: \"%23[^\"]\",%d,%d,\"%23[^\"]\",%d,%d,\"%23[^\"]\",%d,%d",
                        mem1, &used1, &total1, mem2, &used2, &total2, mem3, &used3, &total3);
    }

    if (parsed == 9) {
        return "SMS storage: " +
               std::string(mem1) + " " + std::to_string(used1) + "/" + std::to_string(total1) +
               " | " + std::string(mem2) + " " + std::to_string(used2) + "/" + std::to_string(total2) +
               " | " + std::string(mem3) + " " + std::to_string(used3) + "/" + std::to_string(total3);
    }

    return "SMS storage: " + l;
}

std::string AtTransformer::formatPhonebookEntries(const std::string& raw) const
{
    if (raw.empty()) return "Phonebook entries: no response";
    if (raw.find("ERROR") != std::string::npos || raw.find("+CME ERROR") != std::string::npos) {
        return "Phonebook entries: not available";
    }

    std::string out;
    int count = 0;

    size_t pos = 0;
    while (pos < raw.size()) {
        size_t eol = raw.find('\n', pos);
        if (eol == std::string::npos) eol = raw.size();

        std::string line = raw.substr(pos, eol - pos);
        // trim CR
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.rfind("+CPBR:", 0) == 0) {
            // +CPBR: <idx>,"<number>",<type>,"<text>"
            int idx = -1;
            char number[64] = {0};
            int type = -1;
            char text[64] = {0};

            int parsed = sscanf(line.c_str(),
                                "+CPBR: %d,\"%63[^\"]\",%d,\"%63[^\"]\"",
                                &idx, number, &type, text);

            if (parsed >= 3) {
                count++;
                if (!out.empty()) out += "\n\r";

                out += "  ";
                out += std::to_string(idx);
                out += ": ";

                if (parsed == 4 && text[0]) {
                    out += "\"";
                    out += text;
                    out += "\" ";
                }

                out += number[0] ? number : "(no number)";
            } else {
                // fallback
                count++;
                if (!out.empty()) out += "\n\r";
                out += "  ";
                out += line;
            }
        }

        pos = (eol < raw.size()) ? (eol + 1) : raw.size();
    }

    if (count == 0) return "  No entries found.";
    return out;
}

// ---------------- Network ----------------

std::string AtTransformer::formatSignal(const std::string& csqRaw) const
{
    std::string l = firstValueLine(csqRaw);
    if (l.empty()) return "Signal: no response";

    // +CSQ: <rssi>,<ber>
    auto pos = l.find(':');
    if (pos == std::string::npos) return "Signal: " + l;

    std::string payload = trim(l.substr(pos + 1));
    auto comma = payload.find(',');
    if (comma == std::string::npos) return "Signal: " + payload;

    int rssi = -1;
    int ber = -1;
    try {
        rssi = std::stoi(trim(payload.substr(0, comma)));
        ber = std::stoi(trim(payload.substr(comma + 1)));
    } catch (...) {
        return "Signal: " + payload;
    }

    std::string rssiStr;
    if (rssi == 99) {
        rssiStr = "unknown";
    } else {
        int dbm = -113 + (rssi * 2);
        rssiStr = std::to_string(dbm) + " dBm";
    }

    return "Signal: rssi=" + std::to_string(rssi) + " (" + rssiStr + "), ber=" + std::to_string(ber);
}

std::string AtTransformer::formatOperator(const std::string& copsRaw) const
{
    std::string l = firstValueLine(copsRaw);
    if (l.empty()) return "Operator: no response";

    // Cas +COPS: 0 (auto)
    if (l == "+COPS: 0" || l == "+COPS:0") {
        return "Operator: auto";
    }

    return "Operator: " + l;
}

std::string AtTransformer::formatScanOperators(const std::string& copsRaw) const
{
    if (copsRaw.empty()) return "Operator: no response";

    if (copsRaw.find("+COPS:") == std::string::npos || copsRaw.find('(') == std::string::npos) {
        // pas un scan (ou réponse vide)
        std::string l = firstValueLine(copsRaw);
        if (l.empty()) return "Operator: no response";
        return "Operator: " + l;
    }

    int found = 0;
    std::string out = "[Operators detected]\n\r";

    size_t pos = copsRaw.find("+COPS:");
    if (pos == std::string::npos) return "Operator scan: no +COPS";

    // On parcourt chaque tuple "( ... )"
    size_t p = copsRaw.find('(', pos);
    while (p != std::string::npos) {
        size_t q = copsRaw.find(')', p + 1);
        if (q == std::string::npos) break;

        std::string tuple = copsRaw.substr(p + 1, q - p - 1);
        p = copsRaw.find('(', q + 1);

        // ignore capabilities
        if (tuple.find('-') != std::string::npos) continue;

        int stat = -1;
        char longName[64] = {0};
        char shortName[64] = {0};
        char mccmnc[16] = {0};

        // strict: (stat,"long","short","mccmnc")
        int parsed = sscanf(tuple.c_str(),
                            "%d,\"%63[^\"]\",\"%63[^\"]\",\"%15[^\"]\"",
                            &stat, longName, shortName, mccmnc);

        if (parsed != 4) continue;

        out += "  - ";
        out += longName;
        out += " (";
        out += mccmnc;
        out += ")\n\r";
        found++;
    }

    if (found == 0) {
        // fallback
        size_t a = copsRaw.find("+COPS:");
        if (a == std::string::npos) return "Operator scan: no operators parsed (no +COPS)";
        size_t b = copsRaw.find("\n", a);
        if (b == std::string::npos) b = copsRaw.size();
        std::string line1 = copsRaw.substr(a, b - a);

        return "Operator scan: no operators parsed\n  Raw first +COPS line: " + line1;
    }

    return out;
}

std::string AtTransformer::formatRegistration(const std::string& regRaw) const
{
    std::string l = firstValueLine(regRaw);
    if (l.empty()) return "Registration: no response";
    return "Registration: " + l;
}

// ---------------- Registration helpers ----------------

std::string AtTransformer::regStatToString(int stat) const
{
    switch (stat) {
        case 0: return "not registered";
        case 1: return "registered (home)";
        case 2: return "searching";
        case 3: return "registration denied";
        case 4: return "unknown";
        case 5: return "registered (roaming)";
        default: return "stat=" + std::to_string(stat);
    }
}

bool AtTransformer::parseRegLine(const std::string& line, int& n, int& stat) const
{
    auto pos = line.find(':');
    if (pos == std::string::npos) return false;

    std::string payload = trim(line.substr(pos + 1));
    auto comma = payload.find(',');
    if (comma == std::string::npos) return false;

    try {
        n = std::stoi(trim(payload.substr(0, comma)));

        std::string statPart = trim(payload.substr(comma + 1));
        auto comma2 = statPart.find(',');
        if (comma2 != std::string::npos) statPart = trim(statPart.substr(0, comma2));

        stat = std::stoi(statPart);
        return true;
    } catch (...) {
        return false;
    }
}

std::string AtTransformer::formatRegistrationCS(const std::string& cregRaw) const
{
    std::string l = firstValueLine(cregRaw);
    if (l.empty()) return "CS reg: no response";

    int n = -1, stat = -1;
    if (parseRegLine(l, n, stat)) return "CS reg: " + regStatToString(stat);
    return "CS reg: " + l;
}

std::string AtTransformer::formatRegistrationPS(const std::string& cgregRaw) const
{
    std::string l = firstValueLine(cgregRaw);
    if (l.empty()) return "PS reg: no response";

    int n = -1, stat = -1;
    if (parseRegLine(l, n, stat)) return "PS reg: " + regStatToString(stat);
    return "PS reg: " + l;
}

std::string AtTransformer::formatFunctionality(const std::string& raw) const
{
    if (raw.empty()) return "Mode: unknown";

    auto pos = raw.find("+CFUN:");
    if (pos == std::string::npos) return "Mode: unsupported";

    pos += 6;
    while (pos < raw.size() && (raw[pos] == ' ' || raw[pos] == '\t')) ++pos;
    if (pos >= raw.size() || raw[pos] < '0' || raw[pos] > '9') return "Mode: parse error";

    int mode = raw[pos] - '0';

    switch (mode) {
        case 0: return "Mode: minimum (CFUN=0)";
        case 1: return "Mode: full (CFUN=1)";
        case 4: return "Mode: airplane / RF off (CFUN=4)";
        default: return "Mode: CFUN=" + std::to_string(mode);
    }
}

// ---------------- PDP / attach ----------------

std::string AtTransformer::formatAttach(const std::string& cgattRaw) const
{
    std::string l = firstValueLine(cgattRaw);
    if (l.empty()) return "Attach: no response";

    // +CGATT: 1
    auto pos = l.find(':');
    if (pos == std::string::npos) return "Attach: " + l;

    std::string v = trim(l.substr(pos + 1));
    if (v == "1") return "Attach: attached";
    if (v == "0") return "Attach: detached";
    return "Attach: " + v;
}

std::string AtTransformer::formatPdpContexts(const std::string& cgdcontRaw) const
{
    auto ls = lines(cgdcontRaw);
    std::string out;

    for (auto& l : ls) {
        if (l == "OK" || l == "ERROR") continue;
        if (startsWith(l, "AT")) continue;
        if (!startsWith(l, "+CGDCONT:")) continue;

        if (!out.empty()) out += "\n\r";
        out += "  " + l; // indent each entry
    }

    if (out.empty()) {
        std::string c = clean(cgdcontRaw);
        if (c.empty()) return "PDP contexts: none / no response";
        return "PDP contexts:\n\r" + c;
    }

    return "PDP contexts:\n\r" + out;
}

std::string AtTransformer::formatPdpActive(const std::string& cgactRaw) const
{
    auto ls = lines(cgactRaw);
    std::string out;

    for (auto& l : ls) {
        if (l == "OK" || l == "ERROR") continue;
        if (startsWith(l, "AT")) continue;
        if (!startsWith(l, "+CGACT:")) continue;

        if (!out.empty()) out += "\n\r";
        out += "  " + l; // indent each entry
    }

    if (out.empty()) {
        std::string c = clean(cgactRaw);
        if (c.empty()) return "PDP active: no response";
        return "PDP active:\n\r" + c;
    }

    return "PDP active:\n\r" + out;
}

std::string AtTransformer::formatPdpAddress(const std::string& cgpaddrRaw) const
{
    std::string c = clean(cgpaddrRaw);
    if (c.empty()) return "PDP addr: no response";
    return "PDP addr: " + c;
}

// ---------------- SMS ----------------

std::string AtTransformer::formatSmsList(const std::string& cmglRaw) const
{
    auto ls = lines(cmglRaw);
    std::string out;

    for (auto& l : ls) {
        if (l == "OK" || l == "ERROR") continue;
        if (startsWith(l, "AT")) continue;

        if (!out.empty()) out += "\n\r";
        out += l;
    }

    if (out.empty()) return "[SMS list] empty / no response";
    return "[SMS list]\n\r" + out;
}

std::string AtTransformer::formatSmsRead(const std::string& cmgrRaw) const
{
    auto ls = lines(cmgrRaw);
    std::string out;

    for (auto& l : ls) {
        if (l == "OK" || l == "ERROR") continue;
        if (startsWith(l, "AT")) continue;

        std::string lineOut = l;

        if (!startsWith(l, "+CMGR:")) {
            bool looksHex = !l.empty() && ((l.size() % 2) == 0);

            if (looksHex) {
                for (char c : l) {
                    bool isHex =
                        (c >= '0' && c <= '9') ||
                        (c >= 'a' && c <= 'f') ||
                        (c >= 'A' && c <= 'F');

                    if (!isHex) {
                        looksHex = false;
                        break;
                    }
                }
            }

            if (looksHex) {
                std::string decoded;
                decoded.reserve(l.size() / 2);

                for (size_t i = 0; i + 1 < l.size(); i += 2) {
                    auto hexVal = [](char c) -> uint8_t {
                        if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
                        if (c >= 'a' && c <= 'f') return (uint8_t)(10 + c - 'a');
                        if (c >= 'A' && c <= 'F') return (uint8_t)(10 + c - 'A');
                        return 0;
                    };

                    uint8_t b = (uint8_t)((hexVal(l[i]) << 4) | hexVal(l[i + 1]));

                    if (b >= 32 && b <= 126) {
                        decoded.push_back((char)b);
                    } else if (b == '\r' || b == '\n' || b == '\t') {
                        decoded.push_back((char)b);
                    } else {
                        decoded.push_back('.');
                    }
                }

                lineOut += "\n\r  -> ";
                lineOut += decoded;
            }
        }

        if (!out.empty()) out += "\n\r";
        out += lineOut;
    }

    if (out.empty()) return "SMS read: no response";
    return "SMS read:\n\r" + out;
}

// ---------------- USSD ----------------

std::string AtTransformer::formatUssd(const std::string& cusdRaw) const
{
    auto ls = lines(cusdRaw);
    for (auto& l : ls) {
        if (startsWith(l, "+CUSD:")) {
            return "USSD: " + l;
        }
    }

    std::string c = clean(cusdRaw);
    if (c.empty()) return "USSD: no response";
    return "USSD: " + c;
}

// ---------------- Calls ----------------

std::string AtTransformer::formatCallList(const std::string& clccRaw) const
{
    auto ls = lines(clccRaw);
    std::string out;

    for (auto& l : ls) {
        if (l == "OK" || l == "ERROR") continue;
        if (startsWith(l, "AT")) continue;
        if (!startsWith(l, "+CLCC:")) continue;

        if (!out.empty()) out += "\n";
        out += l;
    }

    if (out.empty()) {
        std::string c = clean(clccRaw);
        if (c.empty()) return "\n[Calls] none / no response";
        return "\n[Calls]\n" + c;
    }

    return "\n[Calls]\n\r" + out;
}

// -------------- Misc -------------------

static inline bool isWhitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string AtTransformer::trim(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && isWhitespace(s[a])) a++;

    size_t b = s.size();
    while (b > a && isWhitespace(s[b - 1])) b--;

    return s.substr(a, b - a);
}

bool AtTransformer::startsWith(const std::string& s, const std::string& prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::vector<std::string> AtTransformer::lines(const std::string& raw) const
{
    std::vector<std::string> out;
    std::string cur;
    cur.reserve(64);

    for (char c : raw) {
        if (c == '\r') continue;
        if (c == '\n') {
            std::string t = trim(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
        } else {
            cur += c;
        }
    }

    std::string t = trim(cur);
    if (!t.empty()) out.push_back(t);

    return out;
}

std::string AtTransformer::formatGsmLocation(const std::string& raw) const
{
    std::string l = firstValueLine(raw);
    if (l.empty()) return "Location: no response";

    //  +CIPGSMLOC: <err>,<lat>,<lon>,<date>,<time>
    if (l.find("+CIPGSMLOC:") == std::string::npos) {
        return "Location: " + l;
    }

    return "Location: " + l;
}