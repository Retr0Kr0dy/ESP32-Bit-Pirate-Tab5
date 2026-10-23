#pragma once

#include <string>
#include <cstring>
#include <Enums/InfraredProtocolEnum.h>

class InfraredCommand {
public:
    InfraredCommand()
        : functionName(""), protocol(InfraredProtocolEnum::_RAW),
          device(0), subdevice(0), function(0) {}

    InfraredCommand(InfraredProtocolEnum proto, int device, int subdevice, int function)
        : protocol(proto), device(device), subdevice(subdevice), function(function) {}


    InfraredCommand(const char* functionName, InfraredProtocolEnum protocol, int16_t device, int16_t subdevice, int16_t function)
        : functionName(functionName), protocol(protocol), device(device), subdevice(subdevice), function(function) {}

    const char* getFunctionName() const { return functionName; }
    InfraredProtocolEnum getProtocol() const { return protocol; }
    int16_t getDevice() const { return device; }
    int16_t getSubdevice() const { return subdevice; }
    int16_t getFunction() const { return function; }

    void setDevice(int16_t value) { device = value; }
    void setSubdevice(int16_t value) { subdevice = value; }
    void setFunction(int16_t value) { function = value; }
    void setProtocol(InfraredProtocolEnum prot)  {protocol = prot; }

    bool operator==(const InfraredCommand& other) const {
        return (std::strcmp(functionName, other.functionName) == 0) &&
               (protocol == other.protocol) &&
               (device == other.device) &&
               (subdevice == other.subdevice) &&
               (function == other.function);
    }

    bool operator!=(const InfraredCommand& other) const {
        return !(*this == other);
    }

private:
    const char* functionName;
    InfraredProtocolEnum protocol;
    int16_t device;
    int16_t subdevice;
    int16_t function;
};
