
#pragma once

#include "NetworkedModule.h"

ref class VehicleControl : public NetworkedModule
{
public:
    VehicleControl(SM_ThreadManagement^ threadMemory,
        SM_VehicleControl^ vehicleMemory);

    virtual void threadFunction() override;
    virtual error_state processSharedMemory() override;
    virtual bool getShutdownFlag() override;

    virtual error_state connect(String^ hostName, int portNumber) override;
    virtual error_state communicate() override;

    error_state processHeartbeats();
    void shutdownThreads();
    error_state checkData();

private:
    SM_VehicleControl^ VC_SM_VC_;
    int watchdog_;
    double Speed_;
    double Steering_;

    void closeConnection();
};