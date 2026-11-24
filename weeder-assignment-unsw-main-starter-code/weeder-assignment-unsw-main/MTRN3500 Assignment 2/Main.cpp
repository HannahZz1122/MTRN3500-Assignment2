
#using <System.dll>

#include "TMM.h"
#include "LiDAR.h"
#include "SMObjects.h"
#include "GNSS.h"
#include "Controller.h"
#include "VC.h"
#include "Display.h"
#include "CrashAvoidance.h"

using namespace System;
using namespace System::Threading;

const int LASER_PORT = 23000;
const int VC_PORT = 25000;

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
    // Create barrier
    SM_TM->ThreadBarrier = gcnew Barrier(7);

    // Create modules
    ThreadManagement^ TMM =
        gcnew ThreadManagement(SM_TM);

    Laser^ LaserModule =
        gcnew Laser(SM_TM, SM_LaserData);

    GPS^ gpsModule = 
        gcnew GPS(SM_TM, gpsData);

    Controller^ controllerModule = 
        gcnew Controller(SM_TM, vehicleData, true);

	VehicleControl^ VCModule =
		gcnew VehicleControl(SM_TM, vehicleData);

	Display^ displayModule =
		gcnew Display(SM_TM, SM_LaserData);

	CrashAvoidance^ crashAvoidanceModule =
		gcnew CrashAvoidance(SM_TM, SM_LaserData, vehicleData);

    // Initialise shared memory before threads start
    if (TMM->setupSharedMemory() != SUCCESS ||
        LaserModule->setupShareMemory() != SUCCESS ||
        gpsModule->setupShareMemory() != SUCCESS ||
        crashAvoidanceModule->setupShareMemory() != SUCCESS
        )
    {
        Console::WriteLine(
            "Shared memory initialisation failed"
        );

        return 1;
    }

    // Connect LiDAR before starting threads
    if (LaserModule->connect(
        gcnew String(WEEDER_ADDRESS),
        LASER_PORT) != SUCCESS)
    {
        Console::WriteLine(
            "LiDAR connection failed"
        );

        return 1;
    }

	// Connect Vehicle Control before starting threads
    String^ weederHost = gcnew String(WEEDER_ADDRESS);
    error_state vcResult = VCModule->connect(weederHost, VC_PORT);

    if (vcResult == ERR_CONNECTION_ABORTED)
    {
        Console::WriteLine("VC connection aborted. Retrying once...");
        Thread::Sleep(300);
        vcResult = VCModule->connect(weederHost, VC_PORT);
    }

    if (vcResult != SUCCESS)
    {
        Console::WriteLine("Vehicle Control connection failed");
        return 1;
    }

	// Connect Display before starting threads
    if (displayModule->connect(gcnew String(DISPLAY_ADDRESS), 28000) != SUCCESS)
    {
        Console::WriteLine("MATLAB Display connection failed");
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

	// Create Vehicle Control thread
    Thread^ vehicleThread = gcnew Thread(
        gcnew ThreadStart(VCModule, &VehicleControl::threadFunction)
    );

	// Create Display thread
    Thread^ displayThread = gcnew Thread(
        gcnew ThreadStart(displayModule, &Display::threadFunction)
    );

	// Create Crash Avoidance thread
	Thread^ crashAvoidanceThread = gcnew Thread(
		gcnew ThreadStart(crashAvoidanceModule, &CrashAvoidance::threadFunction)
	);
    
    // Start threads
    TMMThread->Start();
    LaserThread->Start();
    gpsThread->Start();
    controllerThread->Start();
    vehicleThread->Start();
	displayThread->Start();
	crashAvoidanceThread->Start();

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

	// Wait for Vehicle Control to terminate
	vehicleThread->Join();

	// Wait for Display to terminate
	displayThread->Join();

	// Wait for Crash Avoidance to terminate
	crashAvoidanceThread->Join();

	// controllerModule is not a managed object, so we need to delete it manually
    delete controllerModule;
    Console::WriteLine("All threads terminated");
    Console::WriteLine("UGV System Shutdown Complete");

    return 0;
}
