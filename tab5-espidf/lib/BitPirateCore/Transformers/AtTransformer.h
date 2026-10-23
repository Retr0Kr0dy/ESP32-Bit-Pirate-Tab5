#pragma once

#include <string>
#include <vector>

class AtTransformer {
public:
    // Generic cleanup helpers
    std::string clean(const std::string& raw) const;
    std::vector<std::string> lines(const std::string& raw) const;

    bool isOk(const std::string& raw) const;
    bool isError(const std::string& raw) const;

    // Extract first meaningful line (excluding echo/empty/OK/ERROR)
    std::string firstValueLine(const std::string& raw) const;

    // Specific formatters
    std::string formatModuleInfo(const std::string& atiRaw) const;
    std::string formatSimState(const std::string& cpinRaw) const;      // +CPIN: READY
    std::string formatSignal(const std::string& csqRaw) const;         // +CSQ: rssi,ber
    std::string formatOperator(const std::string& copsRaw) const;      // +COPS: ...
    std::string formatScanOperators(const std::string& copsRaw) const; // +COPS=?
    std::string formatRegistration(const std::string& regRaw) const;   // +CREG/+CEREG: ...
    std::string formatPinLock(const std::string& raw) const;              // +CLCK: ...
    std::string formatSpn(const std::string& raw) const;                  // +CSPN: ...
    std::string formatSimRetries(const std::string& raw) const;           // +CPINR: ...
    std::string formatPhonebookStorage(const std::string& raw) const;     // +CPBS: ...
    std::string formatPhonebookCaps(const std::string& raw) const;        // +CPBR: ?
    std::string formatSmsStorage(const std::string& raw) const;           // +CPMS: ...
    std::string formatClock(const std::string& cclkRaw) const;
    
    // Identity / misc
    std::string formatManufacturer(const std::string& cgmiRaw) const;
    std::string formatIccid(const std::string& ccidRaw) const;
    std::string formatImsi(const std::string& cimiRaw) const;
    std::string formatImei(const std::string& gsnRaw) const;
    std::string formatMsisdn(const std::string& cnumRaw) const;
    
    // Network
    std::string formatRegistrationCS(const std::string& cregRaw) const;
    std::string formatRegistrationPS(const std::string& cgregRaw) const;
    std::string formatFunctionality(const std::string& raw) const;

    // PDP / attach
    std::string formatAttach(const std::string& cgattRaw) const;
    std::string formatPdpContexts(const std::string& cgdcontRaw) const;
    std::string formatPdpActive(const std::string& cgactRaw) const;
    std::string formatPdpAddress(const std::string& cgpaddrRaw) const;

    // SMS / USSD / Calls / Location (basic readable output)
    std::string formatSmsList(const std::string& cmglRaw) const;
    std::string formatSmsRead(const std::string& cmgrRaw) const;
    std::string formatUssd(const std::string& cusdRaw) const;
    std::string formatCallList(const std::string& clccRaw) const;
    std::string formatGsmLocation(const std::string& raw) const;
    std::string formatPhonebookEntries(const std::string& raw) const;

private:
    static std::string trim(const std::string& s);
    static bool startsWith(const std::string& s, const std::string& prefix);
    std::string regStatToString(int stat) const;
    bool parseRegLine(const std::string& line, int& n, int& stat) const;
};
