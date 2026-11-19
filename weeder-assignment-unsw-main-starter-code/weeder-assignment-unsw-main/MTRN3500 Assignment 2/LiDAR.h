
#pragma once

#include "NetworkedModule.h"
#include "SMObjects.h"

using namespace System;
using namespace System::Threading;
using namespace System::Diagnostics;
using namespace System::Net::Sockets;

ref class Laser : public NetworkedModule
{
public:
    Laser(SM_ThreadManagement^ SM_TM,
        SM_Laser^ SM_laser);

    error_state setupShareMemory();

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
    SM_Laser^ Laser_SM_Laser_;

    Stopwatch^ Laser_Stopwatch;
    Stopwatch^ Laser_PrintStopwatch;

    String^ readTelegram();
    void sendCommand(String^ command);
};
