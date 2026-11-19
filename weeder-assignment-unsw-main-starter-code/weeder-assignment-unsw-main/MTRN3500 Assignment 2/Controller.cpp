#include "Controller.h"

namespace
{
    void writeVehicleCommand(SM_VehicleControl^ memory,
        double speed, double steering)
    {
        Monitor::Enter(memory->lockObject);
        try
        {
            memory->Speed = speed;
            memory->Steering = steering;
        }
        finally
        {
            Monitor::Exit(memory->lockObject);
        }
    }
}

Controller::Controller(SM_ThreadManagement^ threadMemory,
    SM_VehicleControl^ vehicleMemory,
    bool useKeyboard)
{
    SM_TM_ = threadMemory;
    Controller_SM_VC_ = vehicleMemory;
    useKeyboard_ = useKeyboard;

    // 0 = Xbox controller, 1 = keyboard
    Controller_Input_ = new ControllerInterface(1, useKeyboard_ ? 1 : 0);
}

Controller::~Controller()
{
    this->!Controller();
}

Controller::!Controller()
{
    delete Controller_Input_;
    Controller_Input_ = nullptr;
}

error_state Controller::processSharedMemory()
{
    controllerState state = Controller_Input_->GetState();

    if (!state.isConnected)
    {
        writeVehicleCommand(Controller_SM_VC_, 0.0, 0.0);
        return ERR_NO_DATA;
    }

    // The Xbox B button requests shutdown for all modules.
    if (!useKeyboard_ && state.buttonB)
    {
        shutdownThreads();
        return SUCCESS;
    }

    // Keyboard mode fills rightThumbY; Xbox mode fills the triggers.
    double speed = useKeyboard_
        ? state.rightThumbY
        : state.rightTrigger - state.leftTrigger;

    double steering = state.rightThumbX * 40.0;

    writeVehicleCommand(Controller_SM_VC_, speed, steering);
    return SUCCESS;
}

error_state Controller::processHeartbeats()
{
    Monitor::Enter(SM_TM_->lockObject);
    try
    {
        SM_TM_->heartbeat |= bit_CONTROLLER;
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
    }

    return SUCCESS;
}

bool Controller::getShutdownFlag()
{
    Monitor::Enter(SM_TM_->lockObject);
    try
    {
        return (SM_TM_->shutdown & bit_CONTROLLER) != 0;
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
    }
}

void Controller::shutdownThreads()
{
    writeVehicleCommand(Controller_SM_VC_, 0.0, 0.0);

    Monitor::Enter(SM_TM_->lockObject);
    try
    {
        SM_TM_->shutdown |= bit_ALL;
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
    }
}

void Controller::threadFunction()
{
    Console::WriteLine("Controller thread starting");
    SM_TM_->ThreadBarrier->SignalAndWait();

    while (!getShutdownFlag())
    {
        processSharedMemory();
        processHeartbeats();
        Thread::Sleep(20);
    }

    writeVehicleCommand(Controller_SM_VC_, 0.0, 0.0);
    Console::WriteLine("Controller thread terminated");
}