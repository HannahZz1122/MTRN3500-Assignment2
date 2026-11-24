
#using <System.dll>
#include "TMM.h"

using namespace System;
using namespace System::Threading;
using namespace System::Diagnostics;


// Constructor
ThreadManagement::ThreadManagement(
    SM_ThreadManagement^ SM_TM)
{
    SM_TM_ = SM_TM;
    TM_Stopwatch = gcnew Stopwatch();
    lastHeartbeat_ = gcnew array<Int64>(8);
    heartbeatSeen_ = gcnew array<bool>(8);
    failureReported_ = gcnew array<bool>(8);

    activeModules_ =
        bit_LASER |
        bit_GPS |
        bit_VC |
        bit_CONTROLLER |
        bit_DISPLAY |
        bit_CRASHAVOIDANCE;
}


// Initialise Shared Memory
error_state ThreadManagement::setupSharedMemory()
{
    if (SM_TM_ == nullptr)
        return ERR_SM;

    Monitor::Enter(SM_TM_->lockObject);

    try
    {
        SM_TM_->shutdown = 0;
        SM_TM_->heartbeat = 0;
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
    }

    return SUCCESS;
}


// Process Shared Memory
error_state ThreadManagement::processSharedMemory()
{
    if (SM_TM_ == nullptr)
        return ERR_SM;

    uint8_t heartbeat = 0;

    Monitor::Enter(SM_TM_->lockObject);
    try
    {
        heartbeat = SM_TM_->heartbeat;

        // Clear old heartbeats
        SM_TM_->heartbeat = bit_TM;
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
    }

    Int64 now = TM_Stopwatch->ElapsedMilliseconds;

    // Allow modules time to finish initial startup
    const Int64 STARTUP_GRACE_MS = 3000;

    for (int i = 0; i < 8; i++)
    {
        uint8_t moduleBit =
            static_cast<uint8_t>(1 << i);

        if ((activeModules_ & moduleBit) == 0)
            continue;

        bool received = (heartbeat & moduleBit) != 0;

        if (received)
        {
            lastHeartbeat_[i] = now;
            heartbeatSeen_[i] = true;

            if (failureReported_[i])
            {
                Console::WriteLine(
                    "[TMM] Heartbeat recovered: 0x{0:X2}",
                    static_cast<int>(moduleBit));
            }

            failureReported_[i] = false;
            continue;
        }

        // Give modules time to start and send their first heartbeat
        if (!heartbeatSeen_[i] && now < STARTUP_GRACE_MS)
            continue;

        if (now - lastHeartbeat_[i] <= CRASH_LIMIT)
            continue;

        if (failureReported_[i])
            continue;

        failureReported_[i] = true;

        // Critical thread failure
        if ((moduleBit & CRITICALMASK) != 0)
        {
            Console::WriteLine(
                "[TMM] Critical thread timeout: 0x{0:X2}",
                static_cast<int>(moduleBit));

            return ERR_CRITICAL_THREAD_FAILURE;
        }

        // Non-critical thread failure
        if ((moduleBit & NONCRITICALMASK) != 0)
        {
            Console::WriteLine(
                "[TMM] Non-critical thread timeout: 0x{0:X2}",
                static_cast<int>(moduleBit));
        }
    }

    return SUCCESS;
}


// Shutdown All Modules
void ThreadManagement::shutdownModules()
{
    if (SM_TM_ == nullptr)
        return;

    Monitor::Enter(SM_TM_->lockObject);

    try
    {
        // Set shutdown flags for all modules
        SM_TM_->shutdown = bit_ALL;
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
    }

    Console::WriteLine(
        "Shutdown signal sent to all modules"
    );
}


// Check Shutdown Flag
bool ThreadManagement::getShutdownFlag()
{
    if (SM_TM_ == nullptr)
        return true;

    Monitor::Enter(SM_TM_->lockObject);

    try
    {
        return (SM_TM_->shutdown & bit_TM) != 0;
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
    }
}


// Main Thread Function
void ThreadManagement::threadFunction()
{
    Console::WriteLine(
        "Thread Management Thread is starting"
    );

    if (setupSharedMemory() != SUCCESS)
    {
        Console::WriteLine(
            "Shared Memory initialisation failed"
        );
        return;
    }

    // Wait for other modules
    SM_TM_->ThreadBarrier->SignalAndWait();

    TM_Stopwatch->Restart();

    Console::WriteLine(
        "Thread Management Thread is running"
    );

    Console::WriteLine(
        "Press 'q' to shutdown all modules"
    );

    while (!getShutdownFlag())
    {
        if (Console::KeyAvailable)
        {
            ConsoleKeyInfo key =
                Console::ReadKey(true);

            if (key.KeyChar == 'q' ||
                key.KeyChar == 'Q')
            {
                Console::WriteLine(
                    "Routine shutdown requested"
                );

                shutdownModules();
                break;
            }
        }
        error_state result = processSharedMemory();
        if (result == ERR_CRITICAL_THREAD_FAILURE ||
            result == ERR_SM)
        {
            Console::WriteLine(
                "[TMM] Failure detected. Requesting shutdown.");

            shutdownModules();
            break;
        }
        // Prevent excessive CPU usage
        Thread::Sleep(20);
    }

    Console::WriteLine(
        "Thread Management Thread is terminated"
    );
}
