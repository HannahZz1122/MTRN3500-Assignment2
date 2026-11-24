
#pragma once

#include "UGVModule.h"
#include "SMObjects.h"

using namespace System;
using namespace System::Threading;
using namespace System::Diagnostics;

ref class CrashAvoidance : public UGVModule
{
public:
    CrashAvoidance(
        SM_ThreadManagement^ threadMemory,
        SM_Laser^ laserMemory,
        SM_VehicleControl^ vehicleMemory
    );

    error_state setupShareMemory();

    virtual void threadFunction() override;
    virtual error_state processSharedMemory() override;
    virtual bool getShutdownFlag() override;

    error_state processHeartbeats();

    // Dynamic detection zone calculation
    void evaluateDetectionZone();

private:
    SM_Laser^ CA_SM_Laser_;
    SM_VehicleControl^ CA_SM_VC_;

    Stopwatch^ CA_Stopwatch;

    array<double>^ scanX_;
    array<double>^ scanY_;

    double Speed_;
    double Steering_;

    bool scanValid_;
    Int64 lastScanTicks_;

    // Results of the offline detection algorithm
    bool obstacleDetected_;
    double nearestDistance_;
};
