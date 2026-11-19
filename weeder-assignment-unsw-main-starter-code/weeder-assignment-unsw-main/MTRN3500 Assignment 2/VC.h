
#pragma once

#include "NetworkedModule.h"
#include "SMObjects.h"

using namespace System;
using namespace System::Threading;
using namespace System::Diagnostics;
using namespace System::Net::Sockets;

ref class VehicleControl : public NetworkedModule
{
public:
    VehicleControl(SM_ThreadManagement^ SM_TM,
        SM_VehicleControl^ SM_VC);

    virtual void threadFunction() override;
    virtual error_state processSharedMemory() override;
    virtual bool getShutdownFlag() override;

    virtual error_state connect(
        String^ hostName, int portNumber) override;

    virtual error_state communicate() override;

    error_state processHeartbeats();
    void shutdownThreads();
    error_state checkData();

private:
    SM_VehicleControl^ VC_SM_VC_;
    Stopwatch^ VC_Stopwatch;

    double Speed_;
    double Steering_;
};
