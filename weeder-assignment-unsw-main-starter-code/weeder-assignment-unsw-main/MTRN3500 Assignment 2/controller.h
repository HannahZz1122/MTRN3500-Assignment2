
#pragma once

#include "UGVModule.h"
#include "ControllerInterface.h"
#include "SMObjects.h"

using namespace System;
using namespace System::Threading;
using namespace System::Diagnostics;

ref class Controller : public UGVModule
{
public:
    Controller(SM_ThreadManagement^ SM_TM,
        SM_VehicleControl^ SM_VC);

    virtual void threadFunction() override;
    virtual error_state processSharedMemory() override;
    virtual bool getShutdownFlag() override;

    error_state processHeartbeats();
    void shutdownThreads();

private:
    SM_VehicleControl^ Controller_SM_VC_;
    Stopwatch^ Controller_Stopwatch;

    ControllerInterface* Controller_Input_;
};
