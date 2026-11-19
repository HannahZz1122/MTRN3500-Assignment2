#pragma once
#using <System.dll>
#include "SMObjects.h"

using namespace System;
using namespace System::Threading;
using namespace System::Diagnostics;

ref class GPS
{
public:
	GPS(SM_ThreadManagement^ SM_TM, SM_GPS^ SM_GPS);

	error_state setupShareMemory();
	void threadFunction();
	error_state processHeartbeats();
	void shutdownThreads();
	bool getShutdownFlag();
	error_state communicate();
	error_state checkData();
	error_state processSharedMemory();

private:
	SM_ThreadManagement^ GPS_SM_TM_;
	SM_GPS^ GPS_SM_GPS_;
	Stopwatch^ GPS_Stopwatch;
};