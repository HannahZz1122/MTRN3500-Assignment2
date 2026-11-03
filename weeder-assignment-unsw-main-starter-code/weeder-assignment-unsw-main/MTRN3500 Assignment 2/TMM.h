
#pragma once

#include "UGVModule.h"
#include "SMObjects.h"

using namespace System;
using namespace System::Threading;
using namespace System::Diagnostics;

ref class ThreadManagement : public UGVModule
{
public:
    ThreadManagement(SM_ThreadManagement^ SM_TM);

    // Create and initialise shared memory
    error_state setupSharedMemory();

    // Process heartbeat information
    virtual error_state processSharedMemory() override;

    // Shutdown all modules
    void shutdownModules();

    // Check shutdown flag
    virtual bool getShutdownFlag() override;

    // Main thread function
    virtual void threadFunction() override;

private:
    Stopwatch^ TM_Stopwatch;
};
