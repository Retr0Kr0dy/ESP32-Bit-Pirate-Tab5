#pragma once
#include <stdint.h>

struct CellAtProfile {
    // ---------------- Basic
    const char* at;
    const char* ati;
    const char* echoOff;
    const char* verboseErrorsOn;
    const char* getClock;      // AT+CCLK?
    const char* setClockFmt;   //AT+CCLK=\"%s\"

    // ---------------- Identity
    const char* getManufacturer;   // AT+CGMI  (3GPP 27.007)
    const char* getModel;          // AT+CGMM or AT+GMM 
    const char* getRevision;       // AT+CGMR or AT+GMR
    const char* getImei;           // AT+GSN / AT+CGSN 

    // ---------------- SIM / security
    const char* getSimState;       // AT+CPIN?
    const char* enterPinFmt;       // AT+CPIN="%s"
    const char* getIccid;          // AT+CCID
    const char* getImsi;           // AT+CIMI
    const char* getMsisdn;         // AT+CNUM 
    const char* getPinLockStatus;  // AT+CLCK="SC",2 
    const char* getSimRetries;      // AT+SPIC
    const char* getSpn;             // AT+CSPN?
    const char* getPhonebookCaps;   // AT+CPBR=?
    const char* getPhonebookStorage;// AT+CPBS?
    const char* getSmsStorage;      // AT+CPMS?
    const char* pbReadIndexFmt;   // "AT+CPBR=%u"
    const char* pbReadRangeFmt;   // "AT+CPBR=%u,%u"
    const char* enterPukFmt;      // AT+CPIN="12345678","5678"  (PUK,  new pin)

    // ---------------- Network / registration
    const char* getSignal;         // AT+CSQ
    const char* getOperator;       // AT+COPS?
    const char* scanOperators;     // AT+COPS=?
    const char* setOperatorAuto;   // AT+COPS=0
    const char* setOperatorFmt;    // ex: "AT+COPS=1,2,\"%s\""
    const char* getRegCS;          // AT+CREG?   (CS registration)
    const char* getRegPS;          // AT+CGREG?  (PS/GPRS registration)
    const char* getFun;            // AT+CFUN?
    const char* setFunFmt;         // AT+CFUN=%d
    const char* reboot;            // AT+CFUN=1,1

    // ---------------- PDP / attach 
    const char* getAttach;         // AT+CGATT?
    const char* setAttachFmt;      // AT+CGATT=%d
    const char* definePdpCtxFmt;   // AT+CGDCONT=<cid>,"IP","apn"
    const char* queryPdpCtx;       // AT+CGDCONT?
    const char* activatePdpFmt;    // AT+CGACT=<state>,<cid>
    const char* queryPdp;          // AT+CGACT?
    const char* getPdpAddrFmt;     // AT+CGPADDR=<cid>

    // ---------------- SMS
    const char* smsTextModeFmt;    // AT+CMGF=%d
    const char* smsCharSetFmt;     // AT+CSCS="%s"
    const char* smsNewIndFmt;      // AT+CNMI=... (new SMS indications)
    const char* smsServiceCenter;  // AT+CSCA?
    const char* smsSendFmt;        // AT+CMGS="%s"
    const char* smsListFmt;        // AT+CMGL="ALL"
    const char* smsReadFmt;        // AT+CMGR=<idx>
    const char* smsDeleteFmt;      // AT+CMGD=<idx>,<delflag>

    // ---------------- USSD 
    const char* ussdFmt;           // AT+CUSD=1,"*123#",15
    const char* ussdCancel;        // AT+CUSD=2

    // ---------------- Calls
    const char* dialFmt;           // ATD%s;
    const char* answer;            // ATA
    const char* hangup;            // ATH
    const char* listCalls;         // AT+CLCC

    // ---------------- Location
    const char* getGsmLocation;   // AT+CIPGSMLOC=1,1
};

inline constexpr CellAtProfile GENERIC_CELL_PROFILE = {
    // Basic
    "AT",
    "ATI",
    "ATE0",
    "AT+CMEE=2",
    "AT+CCLK?",
    "AT+CCLK=\"%s\"",

    // Identity
    "AT+CGMI",
    "AT+GMM",
    "AT+GMR",
    "AT+GSN",

    // SIM
    "AT+CPIN?",
    "AT+CPIN=\"%s\"",
    "AT+CCID",
    "AT+CIMI",
    "AT+CNUM",
    "AT+CLCK=\"SC\",2",
    "AT+SPIC",
    "AT+CSPN?",
    "AT+CPBR=?",
    "AT+CPBS?",
    "AT+CPMS?",
    "AT+CPBR=%u",
    "AT+CPBR=%u,%u",
    "AT+CPIN=\"%s\",\"%s\"",

    // Network
    "AT+CSQ",
    "AT+COPS?",
    "AT+COPS=?",
    "AT+COPS=0",
    "AT+COPS=1,2,\"%s\"",
    
    "AT+CREG?",
    "AT+CGREG?",
    "AT+CFUN?", 
    "AT+CFUN=%d",
    "AT+CFUN=1,1",

    // PDP / attach
    "AT+CGATT?",
    "AT+CGATT=%d",
    "AT+CGDCONT=%d,\"%s\",\"%s\"",
    "AT+CGDCONT?",
    "AT+CGACT=%d,%d",
    "AT+CGACT?",
    "AT+CGPADDR=%d",

    // SMS
    "AT+CMGF=%d",
    "AT+CSCS=\"%s\"",
    "AT+CNMI=%d,%d,%d,%d,%d",
    "AT+CSCA?",
    "AT+CMGS=\"%s\"",
    "AT+CMGL=\"%s\"",
    "AT+CMGR=%d",
    "AT+CMGD=%d,%d",

    // USSD
    "AT+CUSD=1,\"%s\",%d",
    "AT+CUSD=2",

    // Calls
    "ATD%s;",
    "ATA",
    "AT+CHUP",
    "AT+CLCC",

    // Location
    "AT+CIPGSMLOC=1,1"
};
