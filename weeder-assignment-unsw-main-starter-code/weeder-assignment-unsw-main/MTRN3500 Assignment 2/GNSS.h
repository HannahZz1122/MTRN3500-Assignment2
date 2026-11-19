#pragma once

#include "NetworkedModule.h"

using namespace System::Collections::Generic;

ref class GPS : public NetworkedModule
{
public:
    GPS(SM_ThreadManagement^ threadMemory, SM_GPS^ gpsMemory);

    error_state setupShareMemory();
    error_state processHeartbeats();
    error_state checkData();

    virtual error_state connect(String^ hostName, int portNumber) override;
    virtual error_state communicate() override;
    virtual error_state processSharedMemory() override;
    virtual bool getShutdownFlag() override;
    virtual void threadFunction() override;

private:
    SM_GPS^ gpsMemory_;
    List<Byte>^ pending_;
    array<Byte>^ frame_;
    UInt32 receivedCRC_;
    UInt32 calculatedCRC_;

    bool takeFrame();
};