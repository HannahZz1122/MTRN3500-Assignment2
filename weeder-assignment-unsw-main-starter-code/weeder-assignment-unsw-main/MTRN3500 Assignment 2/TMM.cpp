
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

    Monitor::Enter(SM_TM_->lockObject);

    try
    {
        // Read heartbeats from all modules
        uint8_t heartbeat = SM_TM_->heartbeat;

        // Clear module heartbeats after processing
        SM_TM_->heartbeat = 0;

        // TM heartbeat
        SM_TM_->heartbeat |= bit_TM;
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
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

    TM_Stopwatch->Start();

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
        processSharedMemory();

        // Prevent excessive CPU usage
        Thread::Sleep(20);
    }

    Console::WriteLine(
        "Thread Management Thread is terminated"
    );
}
