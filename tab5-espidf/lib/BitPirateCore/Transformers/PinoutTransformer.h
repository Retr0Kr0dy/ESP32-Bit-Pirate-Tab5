#pragma once

#include "Enums/ModeEnum.h"
#include "Models/PinoutConfig.h"
#include "States/GlobalState.h"

class GlobalState;

class PinoutTransformer {
public:
    PinoutConfig build(ModeEnum mode) const;
    GlobalState& state = GlobalState::getInstance();
};