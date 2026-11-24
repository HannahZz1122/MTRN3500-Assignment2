
#include "CrashAvoidance.h"

using namespace System;
using namespace System::Threading;
using namespace System::Diagnostics;

namespace
{
    // Simulation-only parameters, requiring validation
    const double DETECTION_DISTANCE_M = 1.0;
    const double FRONT_HALF_ANGLE_DEG = 25.0;
    const double STEERING_SHIFT_FACTOR = 0.5;
    const Int64 SCAN_TIMEOUT_TICKS =
        1000LL * TimeSpan::TicksPerMillisecond;

    const int LOOP_INTERVAL_MS = 20;
}

// Constructor
CrashAvoidance::CrashAvoidance(
    SM_ThreadManagement^ threadMemory,
    SM_Laser^ laserMemory,
    SM_VehicleControl^ vehicleMemory)
{
    SM_TM_ = threadMemory;
    CA_SM_Laser_ = laserMemory;
    CA_SM_VC_ = vehicleMemory;

    CA_Stopwatch = gcnew Stopwatch();

    scanX_ = gcnew array<double>(STANDARD_LASER_LENGTH);
    scanY_ = gcnew array<double>(STANDARD_LASER_LENGTH);

    Speed_ = 0.0;
    Steering_ = 0.0;
    scanValid_ = false;
    lastScanTicks_ = 0;

    obstacleDetected_ = false;
    nearestDistance_ = Double::PositiveInfinity;
}

// Shared memory setup
error_state CrashAvoidance::setupShareMemory()
{
    if (SM_TM_ == nullptr ||
        CA_SM_Laser_ == nullptr ||
        CA_SM_VC_ == nullptr)
    {
        return ERR_SM;
    }

    return SUCCESS;
}

// Read inputs and publish avoidance status
error_state CrashAvoidance::processSharedMemory()
{
    if (setupShareMemory() != SUCCESS)
        return ERR_SM;

    Monitor::Enter(CA_SM_Laser_->lockObject);
    try
    {
        Array::Copy(CA_SM_Laser_->x, scanX_,
            STANDARD_LASER_LENGTH);

        Array::Copy(CA_SM_Laser_->y, scanY_,
            STANDARD_LASER_LENGTH);

        scanValid_ = CA_SM_Laser_->scanValid;
        lastScanTicks_ = CA_SM_Laser_->lastScanTicks;
    }
    finally
    {
        Monitor::Exit(CA_SM_Laser_->lockObject);
    }

    Monitor::Enter(CA_SM_VC_->lockObject);
    try
    {
        Speed_ = CA_SM_VC_->Speed;
        Steering_ = CA_SM_VC_->Steering;
    }
    finally
    {
        Monitor::Exit(CA_SM_VC_->lockObject);
    }

    Int64 age = DateTime::UtcNow.Ticks - lastScanTicks_;

    bool freshScan =
        scanValid_ &&
        lastScanTicks_ > 0 &&
        age >= 0 &&
        age <= SCAN_TIMEOUT_TICKS;

    bool validInput =
        !Double::IsNaN(Speed_) &&
        !Double::IsInfinity(Speed_) &&
        !Double::IsNaN(Steering_) &&
        !Double::IsInfinity(Steering_) &&
        Math::Abs(Speed_) <= 1.0 &&
        Math::Abs(Steering_) <= 40.0;

    if (freshScan && validInput)
        evaluateDetectionZone();
    else
        obstacleDetected_ = false;

    // No valid scan must never mean "path clear".
    bool inhibit = !freshScan ||
        !validInput ||
        obstacleDetected_;

    Monitor::Enter(CA_SM_VC_->lockObject);
    try
    {
        CA_SM_VC_->MotionInhibited = inhibit;
        CA_SM_VC_->AvoidanceReady = freshScan && validInput;
    }
    finally
    {
        Monitor::Exit(CA_SM_VC_->lockObject);
    }

    return (freshScan && validInput)
        ? SUCCESS
        : ERR_NO_DATA;
}

// Steering-aware detection zone
void CrashAvoidance::evaluateDetectionZone()
{
    obstacleDetected_ = false;
    nearestDistance_ = Double::PositiveInfinity;

    // Stationary: retain detection information.
    // Reverse travel is not assessed by this front LiDAR.
    if (Speed_ < 0.0)
        return;

    // Assumption: positive Steering means turning right.
    // Positive LiDAR Y means left, so shift is negative.
    double shiftedCentreDeg =
        -Steering_ * STEERING_SHIFT_FACTOR;

    for (int i = 0; i < STANDARD_LASER_LENGTH; ++i)
    {
        double x = scanX_[i];
        double y = scanY_[i];

        if (Double::IsNaN(x) || Double::IsNaN(y) ||
            Double::IsInfinity(x) || Double::IsInfinity(y))
            continue;

        if (x <= 0.0)
            continue;

        double distance = Math::Sqrt(x * x + y * y);

        if (distance <= 0.0 ||
            distance > DETECTION_DISTANCE_M)
            continue;

        // 0 degrees is straight forward (+X).
        // Positive angles point to vehicle left (+Y).
        double angleDeg =
            Math::Atan2(y, x) * 180.0 / Math::PI;

        bool inFront =
            Math::Abs(angleDeg) <= FRONT_HALF_ANGLE_DEG;

        bool inSteeredZone =
            Math::Abs(angleDeg - shiftedCentreDeg)
            <= FRONT_HALF_ANGLE_DEG;

        if (inFront || inSteeredZone)
        {
            obstacleDetected_ = true;

            nearestDistance_ =
                Math::Min(nearestDistance_, distance);
        }
    }
}

// Heartbeat
error_state CrashAvoidance::processHeartbeats()
{
    Monitor::Enter(SM_TM_->lockObject);
    try
    {
        SM_TM_->heartbeat |= bit_CRASHAVOIDANCE;
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
    }

    return SUCCESS;
}

// Shutdown flag
bool CrashAvoidance::getShutdownFlag()
{
    Monitor::Enter(SM_TM_->lockObject);
    try
    {
        return (SM_TM_->shutdown & bit_CRASHAVOIDANCE) != 0;
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
    }
}

// Thread
void CrashAvoidance::threadFunction()
{
    Console::WriteLine("Crash Avoidance thread starting");

    SM_TM_->ThreadBarrier->SignalAndWait();
    CA_Stopwatch->Start();

    while (!getShutdownFlag())
    {
        error_state result = processSharedMemory();

        // No valid scan is reported as a separate state.
        // The shared motion-inhibit flag remains active.
        if (result == ERR_SM)
        {
            Monitor::Enter(SM_TM_->lockObject);
            try
            {
                SM_TM_->shutdown |= bit_ALL;
            }
            finally
            {
                Monitor::Exit(SM_TM_->lockObject);
            }

            break;
        }

        processHeartbeats();
        Thread::Sleep(LOOP_INTERVAL_MS);
    }

    // Leave the shared state inhibited on exit.
    Monitor::Enter(CA_SM_VC_->lockObject);
    try
    {
        CA_SM_VC_->MotionInhibited = true;
        CA_SM_VC_->AvoidanceReady = false;
    }
    finally
    {
        Monitor::Exit(CA_SM_VC_->lockObject);
    }

    Console::WriteLine("Crash Avoidance thread terminated");
}
