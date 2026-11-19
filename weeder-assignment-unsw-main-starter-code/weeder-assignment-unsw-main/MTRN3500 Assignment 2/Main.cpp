
#using <System.dll>

#include "TMM.h"
#include "LiDAR.h"
#include "SMObjects.h"
#include "GNSS.h"
#include "Controller.h"

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

    SM_GPS^ gpsData = 
        gcnew SM_GPS();

    SM_VehicleControl^ vehicleData = 
        gcnew SM_VehicleControl();
    // Create barrier for TMM and Laser
    SM_TM->ThreadBarrier = gcnew Barrier(4);

    // Create modules
    ThreadManagement^ TMM =
        gcnew ThreadManagement(SM_TM);

    Laser^ LaserModule =
        gcnew Laser(SM_TM, SM_LaserData);

    GPS^ gpsModule = 
        gcnew GPS(SM_TM, gpsData);

    Controller^ controllerModule = 
        gcnew Controller(SM_TM, vehicleData, true);
    // Initialise shared memory before threads start
    if (TMM->setupSharedMemory() != SUCCESS ||
        LaserModule->setupShareMemory() != SUCCESS ||
        gpsModule->setupShareMemory() != SUCCESS)
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

	// Create GPS thread
    Thread^ gpsThread = 
        gcnew Thread(
            gcnew ThreadStart(
                gpsModule,
                &GPS::threadFunction
            )
    );

	// Create Controller thread
    Thread^ controllerThread = gcnew Thread(
        gcnew ThreadStart(controllerModule, &Controller::threadFunction)
    );
    // Start threads
    TMMThread->Start();
    LaserThread->Start();
    gpsThread->Start();
    controllerThread->Start();

    Console::WriteLine("UGV threads started");
    Console::WriteLine("Press 'q' to shutdown");

    // Wait for TMM to terminate
    TMMThread->Join();

    // Ensure all modules receive shutdown
    TMM->shutdownModules();

    // Wait for Laser to terminate
    LaserThread->Join();

    // Wait for GPS to terminate
    gpsThread->Join();

	// Wait for Controller to terminate
	controllerThread->Join();
	// controllerModule is not a managed object, so we need to delete it manually
    delete controllerModule;
    Console::WriteLine("All threads terminated");
    Console::WriteLine("UGV System Shutdown Complete");

    return 0;
}
