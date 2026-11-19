
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
    Controller(SM_ThreadManagement^ threadMemory,
        SM_VehicleControl^ vehicleMemory,
        bool useKeyboard);

    ~Controller();

    virtual void threadFunction() override;
    virtual error_state processSharedMemory() override;
    virtual bool getShutdownFlag() override;

    error_state processHeartbeats();
    void shutdownThreads();

protected:
    !Controller();

private:
    SM_VehicleControl^ Controller_SM_VC_;
    ControllerInterface* Controller_Input_;
    bool useKeyboard_;
};