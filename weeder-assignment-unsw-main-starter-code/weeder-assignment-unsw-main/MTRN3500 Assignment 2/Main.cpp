
#using <System.dll>

#include "TMM.h"
#include "Laser.h"
#include "SMObjects.h"

using namespace System;
using namespace System::Threading;

int main(array<String^>^ args)
{
    Console::WriteLine("UGV System Starting...");

    // Create shared memory objects
    SM_ThreadManagement^ SM_TM =
        gcnew SM_ThreadManagement();

    SM_Laser^ SM_LaserData =
        gcnew SM_Laser();

    // Create barrier for TMM and Laser
    SM_TM->ThreadBarrier = gcnew Barrier(2);

    // Create modules
    ThreadManagement^ TMM =
        gcnew ThreadManagement(SM_TM);

    Laser^ LaserModule =
        gcnew Laser(SM_TM, SM_LaserData);

    // Initialise shared memory before threads start
    if (TMM->setupSharedMemory() != SUCCESS ||
        LaserModule->setupShareMemory() != SUCCESS)
    {
        Console::WriteLine(
            "Shared memory initialisation failed"
        );

        return 1;
    }

    // Connect LiDAR before starting threads
    // Replace LASER_PORT with the actual port
    const int LASER_PORT = 23000;

    if (LaserModule->connect(
        gcnew String(WEEDER_ADDRESS),
        LASER_PORT) != SUCCESS)
    {
        Console::WriteLine(
            "LiDAR connection failed"
        );

        return 1;
    }

    // Create TMM thread
    Thread^ TMMThread =
        gcnew Thread(
            gcnew ThreadStart(
                TMM,
                &ThreadManagement::threadFunction
            )
        );

    // Create Laser thread
    Thread^ LaserThread =
        gcnew Thread(
            gcnew ThreadStart(
                LaserModule,
                &Laser::threadFunction
            )
        );

    // Start threads
    TMMThread->Start();
    LaserThread->Start();

    Console::WriteLine("UGV threads started");
    Console::WriteLine("Press 'q' to shutdown");

    // Wait for TMM to terminate
    TMMThread->Join();

    // Ensure all modules receive shutdown
    TMM->shutdownModules();

    // Wait for Laser to terminate
    LaserThread->Join();

    Console::WriteLine("All threads terminated");
    Console::WriteLine("UGV System Shutdown Complete");

    return 0;
}
