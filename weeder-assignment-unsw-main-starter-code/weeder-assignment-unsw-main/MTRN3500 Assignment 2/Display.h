#pragma once

#include "NetworkedModule.h"

ref class Display : public NetworkedModule
{
public:
    Display(SM_ThreadManagement^ threadMemory,
        SM_Laser^ laserMemory);

    virtual error_state connect(String^ hostName, int portNumber) override;
    virtual error_state communicate() override;

    virtual error_state processSharedMemory() override;
    virtual bool getShutdownFlag() override;
    virtual void threadFunction() override;

private:
    SM_Laser^ laserMemory_;
    array<System::Byte>^ dataX_;
    array<System::Byte>^ dataY_;
    array<double>^ pointsMm_;
    void processHeartbeats();
    void shutdownThreads();
};