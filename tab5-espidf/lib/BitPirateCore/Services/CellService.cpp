#include "CellService.h"
#include <Arduino.h>
#include <stdio.h>

static inline bool containsFinalOk(const std::string& s) {
    return s.find("\r\nOK") != std::string::npos || s == "OK" || s.find("OK") != std::string::npos;
}

static inline bool containsFinalError(const std::string& s) {
    return s.find("\r\nERROR") != std::string::npos ||
           s.find("ERROR") != std::string::npos ||
           s.find("+CME ERROR") != std::string::npos ||
           s.find("+CMS ERROR") != std::string::npos;
}

// -------------------- base --------------------

void CellService::init(uint8_t rxPin, uint8_t txPin, uint32_t baudrate)
{
    _baudrate = baudrate;

    Serial1.end();
    delay(50);

    Serial1.begin(baudrate, SERIAL_8N1, rxPin, txPin);
    delay(200);

    flushInput();

    if (_profile.echoOff && _profile.echoOff[0]) {
        (void)sendCommand(_profile.echoOff, 800);
    }
    if (_profile.verboseErrorsOn && _profile.verboseErrorsOn[0]) {
        (void)sendCommand(_profile.verboseErrorsOn, 800);
    }
}

bool CellService::detect()
{
    if (!_profile.at || !_profile.at[0]) return false;

    flushInput();

    for (int i = 0; i < 3; ++i) {
        auto r = sendCommand(_profile.at, 800);
        if (containsFinalOk(r)) return true;
        delay(50);
    }
    return false;
}

std::string CellService::sendCommand(const std::string& cmd, uint32_t timeoutMs)
{
    flushInput();

    Serial1.print(cmd.c_str());
    Serial1.print("\r");

    return readResponse(timeoutMs);
}

std::string CellService::readResponse(uint32_t timeoutMs)
{
    std::string resp;
    uint32_t start = millis();

    while ((millis() - start) < timeoutMs) {
        while (Serial1.available()) {
            resp += (char)Serial1.read();
        }

        if (containsFinalOk(resp) || containsFinalError(resp)) {
            break;
        }

        delay(10);
    }

    return resp;
}

void CellService::flushInput()
{
    while (Serial1.available()) {
        Serial1.read();
    }
}

bool CellService::sendTextAndCtrlZExpectOk(const std::string& text, uint32_t timeoutMs)
{
    flushInput();

    Serial1.print(text.c_str());
    Serial1.write(0x1A); // CTRL+Z

    auto r = readResponse(timeoutMs);
    return containsFinalOk(r) && !containsFinalError(r);
}

bool CellService::sendExpectOk(const std::string& cmd, uint32_t timeoutMs)
{
    auto r = sendCommand(cmd, timeoutMs);
    return containsFinalOk(r) && !containsFinalError(r);
}

std::string CellService::getClock()
{
    if (_profile.getClock && _profile.getClock[0]) {
        return sendCommand(_profile.getClock,500);
    }
    return "";
}

// -------------------- identity --------------------

std::string CellService::getModuleInfo()
{
    if (!_profile.ati || !_profile.ati[0]) return "";
    return sendCommand(_profile.ati,500);
}

std::string CellService::getManufacturer()
{
    if (!_profile.getManufacturer || !_profile.getManufacturer[0]) return "";
    return sendCommand(_profile.getManufacturer, 500);
}

std::string CellService::getModel()
{
    if (!_profile.getModel || !_profile.getModel[0]) return "";
    return sendCommand(_profile.getModel, 500);
}

std::string CellService::getRevision()
{
    if (!_profile.getRevision || !_profile.getRevision[0]) return "";
    return sendCommand(_profile.getRevision, 500);
}

std::string CellService::getImei()
{
    if (!_profile.getImei || !_profile.getImei[0]) return "";
    return sendCommand(_profile.getImei, 500);
}

// -------------------- SIM --------------------

bool CellService::isSimReady()
{
    if (!_profile.getSimState || !_profile.getSimState[0]) {
        return false;
    }

    std::string resp = sendCommand(_profile.getSimState,500);

    if (resp.empty()) return false;

    //  +CPIN: READY
    if (resp.find("READY") != std::string::npos) {
        return true;
    }

    // SIM PIN / PUK / NOT INSERTED / etc.
    return false;
}

std::string CellService::getSimState()
{
    if (!_profile.getSimState || !_profile.getSimState[0]) return "";
    return sendCommand(_profile.getSimState,500);
}

bool CellService::enterPin(const std::string& pin)
{
    if (!_profile.enterPinFmt || !_profile.enterPinFmt[0]) return false;

    std::string cmd = format1(_profile.enterPinFmt, pin);
    if (cmd.empty()) return false;

    return sendExpectOk(cmd, 5000);
}

bool CellService::enterPuk(const std::string& puk, const std::string& newPin)
{
    if (!_profile.enterPukFmt || !_profile.enterPukFmt[0]) return false;

    char buf[96];
    snprintf(buf, sizeof(buf), _profile.enterPukFmt, puk.c_str(), newPin.c_str());

    return sendExpectOk(std::string(buf), 8000);
}

bool CellService::isSimPukRequired()
{
    if (!_profile.getSimState || !_profile.getSimState[0]) return false;

    std::string resp = sendCommand(_profile.getSimState, 500);
    return resp.find("PUK") != std::string::npos;
}

std::string CellService::getIccid()
{
    if (!_profile.getIccid || !_profile.getIccid[0]) return "";
    return sendCommand(_profile.getIccid, 500);
}

std::string CellService::getImsi()
{
    if (!_profile.getImsi || !_profile.getImsi[0]) return "";
    return sendCommand(_profile.getImsi, 500);
}

std::string CellService::getMsisdn()
{
    if (!_profile.getMsisdn || !_profile.getMsisdn[0]) return "";
    return sendCommand(_profile.getMsisdn, 500); // AT+CNUM
}

std::string CellService::getPinLockStatus()
{
    if (!_profile.getPinLockStatus || !_profile.getPinLockStatus[0]) return "";
    return sendCommand(_profile.getPinLockStatus, 500); // AT+CLCK="SC",2
}

std::string CellService::scanOperators(uint32_t timeoutMs)
{
    flushInput();

    Serial1.print(_profile.scanOperators);
    Serial1.print("\r");

    std::string resp;
    uint32_t start = millis();
    uint32_t lastData = millis();

    while ((millis() - start) < timeoutMs) {

        while (Serial1.available()) {
            char c = (char)Serial1.read();
            resp += c;
            lastData = millis();
        }

        if (containsFinalOk(resp) || containsFinalError(resp)) {
            break;
        }

        delay(20);
    }

    return resp;
}

std::string CellService::getSimRetries()
{
    if (!_profile.getSimRetries || !_profile.getSimRetries[0]) return "";
    return sendCommand(_profile.getSimRetries,500);
}

std::string CellService::getServiceProviderName()
{
    if (!_profile.getSpn || !_profile.getSpn[0]) return "";
    return sendCommand(_profile.getSpn,500);
}

std::string CellService::getPhonebookStorage()
{
    if (!_profile.getPhonebookStorage || !_profile.getPhonebookStorage[0]) return "";
    return sendCommand(_profile.getPhonebookStorage,500);
}

std::string CellService::getPhonebookCaps()
{
    if (!_profile.getPhonebookCaps || !_profile.getPhonebookCaps[0]) return "";
    return sendCommand(_profile.getPhonebookCaps, 500);
}

std::string CellService::getSmsStorage()
{
    if (!_profile.getSmsStorage || !_profile.getSmsStorage[0]) return "";
    return sendCommand(_profile.getSmsStorage, 500);
}

// -------------------- Network --------------------

std::string CellService::getSignal()
{
    if (!_profile.getSignal || !_profile.getSignal[0]) return "";
    return sendCommand(_profile.getSignal,500);
}

std::string CellService::getOperator()
{
    if (!_profile.getOperator || !_profile.getOperator[0]) return "";
    return sendCommand(_profile.getOperator, 3000);
}

bool CellService::setOperator(const std::string& mccmnc)
{
    if (!_profile.setOperatorFmt || !_profile.setOperatorFmt[0]) return false;
    if (mccmnc.empty()) return false;

    char buf[64];
    snprintf(buf, sizeof(buf), _profile.setOperatorFmt, mccmnc.c_str());

    return sendExpectOk(std::string(buf), 30000);
}

bool CellService::setOperatorAuto()
{
    if (!_profile.setOperatorAuto || !_profile.setOperatorAuto[0]) return false;
    return sendExpectOk(_profile.setOperatorAuto, 8000);
}

std::string CellService::getRegistrationCS()
{
    if (!_profile.getRegCS || !_profile.getRegCS[0]) return "";
    return sendCommand(_profile.getRegCS,500);
}

std::string CellService::getRegistrationPS()
{
    if (!_profile.getRegPS || !_profile.getRegPS[0]) return "";
    return sendCommand(_profile.getRegPS,500);
}

bool CellService::setFunctionality(uint8_t fun)
{
    if (!_profile.setFunFmt || !_profile.setFunFmt[0]) return false;

    std::string cmd = format1u(_profile.setFunFmt, fun);
    if (cmd.empty()) return false;

    return sendExpectOk(cmd, 5000);
}

std::string CellService::getFunctionality()
{
    if (!_profile.getFun || !_profile.getFun[0]) return "";
    return sendCommand(_profile.getFun,500);
}

bool CellService::reboot()
{
    if (!_profile.reboot || !_profile.reboot[0]) return false;
    return sendExpectOk(_profile.reboot, 8000);
}

std::string CellService::phonebookReadIndex(uint16_t index)
{
    if (!_profile.pbReadIndexFmt || !_profile.pbReadIndexFmt[0]) return "";
    std::string cmd = format1u(_profile.pbReadIndexFmt, index);
    if (cmd.empty()) return "";
    return sendCommand(cmd, 8000);
}

std::string CellService::phonebookReadRange(uint16_t start, uint16_t end)
{
    if (!_profile.pbReadRangeFmt || !_profile.pbReadRangeFmt[0]) return "";
    if (start == 0 || end == 0 || end < start) return "";

    std::string cmd = format2u(_profile.pbReadRangeFmt, start, end);
    if (cmd.empty()) return "";

    // dump 
    return sendCommand(cmd, 6000);
}

// -------------------- PDP / attach --------------------

std::string CellService::getAttach()
{
    if (!_profile.getAttach || !_profile.getAttach[0]) return "";
    return sendCommand(_profile.getAttach, 500);
}

bool CellService::setAttach(bool attached)
{
    if (!_profile.setAttachFmt || !_profile.setAttachFmt[0]) return false;

    std::string cmd = format1u(_profile.setAttachFmt, attached ? 1 : 0);
    if (cmd.empty()) return false;

    return sendExpectOk(cmd, 8000);
}

bool CellService::definePdpContext(uint8_t cid, const std::string& pdpType, const std::string& apn)
{
    if (!_profile.definePdpCtxFmt || !_profile.definePdpCtxFmt[0]) return false;

    std::string cmd = format3(_profile.definePdpCtxFmt, cid, pdpType, apn);
    if (cmd.empty()) return false;

    return sendExpectOk(cmd, 3000);
}

std::string CellService::queryPdpContexts()
{
    if (!_profile.queryPdpCtx || !_profile.queryPdpCtx[0]) return "";
    return sendCommand(_profile.queryPdpCtx, 3000);
}

bool CellService::activatePdp(uint8_t cid, bool active)
{
    if (!_profile.activatePdpFmt || !_profile.activatePdpFmt[0]) return false;

    std::string cmd = format2u(_profile.activatePdpFmt, active ? 1 : 0, cid);
    if (cmd.empty()) return false;

    return sendExpectOk(cmd, 12000);
}

std::string CellService::queryPdpActive()
{
    if (!_profile.queryPdp || !_profile.queryPdp[0]) return "";
    return sendCommand(_profile.queryPdp, 3000);
}

std::string CellService::getPdpAddress(uint8_t cid)
{
    if (!_profile.getPdpAddrFmt || !_profile.getPdpAddrFmt[0]) return "";

    std::string cmd = format1u(_profile.getPdpAddrFmt, cid);
    if (cmd.empty()) return "";

    return sendCommand(cmd, 3000);
}

// -------------------- SMS --------------------

bool CellService::smsSetTextMode(bool enabled)
{
    if (!_profile.smsTextModeFmt || !_profile.smsTextModeFmt[0]) return false;

    std::string cmd = format1u(_profile.smsTextModeFmt, enabled ? 1 : 0);
    if (cmd.empty()) return false;

    return sendExpectOk(cmd, 3000);
}

bool CellService::smsSetCharset(const std::string& charset)
{
    if (!_profile.smsCharSetFmt || !_profile.smsCharSetFmt[0]) return false;

    std::string cmd = format1(_profile.smsCharSetFmt, charset);
    if (cmd.empty()) return false;

    return sendExpectOk(cmd, 3000);
}

bool CellService::smsSetNewIndications(uint8_t mode, uint8_t mt, uint8_t bm, uint8_t ds, uint8_t bfr)
{
    if (!_profile.smsNewIndFmt || !_profile.smsNewIndFmt[0]) return false;

    std::string cmd = format5u(_profile.smsNewIndFmt, mode, mt, bm, ds, bfr);
    if (cmd.empty()) return false;

    return sendExpectOk(cmd, 3000);
}

std::string CellService::smsGetServiceCenter()
{
    if (!_profile.smsServiceCenter || !_profile.smsServiceCenter[0]) return "";
    return sendCommand(_profile.smsServiceCenter, 3000);
}

std::string CellService::smsList(const std::string& filter)
{
    if (!_profile.smsListFmt || !_profile.smsListFmt[0]) return "";

    std::string cmd = format1(_profile.smsListFmt, filter);
    if (cmd.empty()) return "";

    return sendCommand(cmd, 8000);
}

std::string CellService::smsRead(uint16_t index)
{
    if (!_profile.smsReadFmt || !_profile.smsReadFmt[0]) return "";

    std::string cmd = format1u(_profile.smsReadFmt, index);
    if (cmd.empty()) return "";

    return sendCommand(cmd, 8000);
}

bool CellService::smsDelete(uint16_t index, uint8_t flag)
{
    if (!_profile.smsDeleteFmt || !_profile.smsDeleteFmt[0]) return false;

    std::string cmd = format2u(_profile.smsDeleteFmt, index, flag);
    if (cmd.empty()) return false;

    return sendExpectOk(cmd, 8000);
}

bool CellService::smsBeginSend(const std::string& number)
{
    if (!_profile.smsSendFmt || !_profile.smsSendFmt[0]) return false;

    std::string cmd = format1(_profile.smsSendFmt, number);
    if (cmd.empty()) return false;

    flushInput();
    Serial1.print(cmd.c_str());
    Serial1.print("\r");

    uint32_t start = millis();
    std::string resp;

    while (millis() - start < 8000) {
        while (Serial1.available()) {
            char c = (char)Serial1.read();
            resp += c;
            if (c == '>') {
                return true;
            }
        }
        if (containsFinalError(resp)) return false;
        delay(10);
    }

    return false;
}

bool CellService::smsSendText(const std::string& text)
{
    return sendTextAndCtrlZExpectOk(text, 20000);
}

// -------------------- USSD --------------------

bool CellService::ussdRequest(const std::string& code, uint8_t dcs)
{
    if (!_profile.ussdFmt || !_profile.ussdFmt[0]) return false;

    char buf[160];
    snprintf(buf, sizeof(buf), _profile.ussdFmt, code.c_str(), (unsigned)dcs);

    return sendExpectOk(std::string(buf), 5000);
}

bool CellService::ussdCancel()
{
    if (!_profile.ussdCancel || !_profile.ussdCancel[0]) return false;
    return sendExpectOk(_profile.ussdCancel, 3000);
}

// -------------------- Calls --------------------

bool CellService::dial(const std::string& number)
{
    if (!_profile.dialFmt || !_profile.dialFmt[0]) return false;

    std::string cmd = format1(_profile.dialFmt, number);
    if (cmd.empty()) return false;

    auto r = sendCommand(cmd, 5000);
    return !containsFinalError(r);
}

bool CellService::answerCall()
{
    if (!_profile.answer || !_profile.answer[0]) return false;
    return sendExpectOk(_profile.answer, 5000);
}

bool CellService::hangupCall()
{
    if (!_profile.hangup || !_profile.hangup[0]) return false;
    return sendExpectOk(_profile.hangup, 5000);
}

std::string CellService::listCalls()
{
    if (!_profile.listCalls || !_profile.listCalls[0]) return "";
    return sendCommand(_profile.listCalls, 3000);
}

// -------------------- format helpers --------------------

std::string CellService::format1(const char* fmt, const std::string& a)
{
    if (!fmt) return "";
    char buf[128];
    snprintf(buf, sizeof(buf), fmt, a.c_str());
    return std::string(buf);
}

std::string CellService::format1u(const char* fmt, uint32_t v)
{
    if (!fmt) return "";
    char buf[64];
    snprintf(buf, sizeof(buf), fmt, (unsigned)v);
    return std::string(buf);
}

std::string CellService::format2u(const char* fmt, uint32_t a, uint32_t b)
{
    if (!fmt) return "";
    char buf[96];
    snprintf(buf, sizeof(buf), fmt, (unsigned)a, (unsigned)b);
    return std::string(buf);
}

std::string CellService::format3(const char* fmt, int a, const std::string& b, const std::string& c)
{
    if (!fmt) return "";
    char buf[192];
    snprintf(buf, sizeof(buf), fmt, a, b.c_str(), c.c_str());
    return std::string(buf);
}

std::string CellService::format5u(const char* fmt, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    if (!fmt) return "";
    char buf[128];
    snprintf(buf, sizeof(buf), fmt, (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)d, (unsigned)e);
    return std::string(buf);
}

std::string CellService::getGsmLocation()
{
    if (!_profile.getGsmLocation || !_profile.getGsmLocation[0]) return "";
    return sendCommand(_profile.getGsmLocation, 5000);
}