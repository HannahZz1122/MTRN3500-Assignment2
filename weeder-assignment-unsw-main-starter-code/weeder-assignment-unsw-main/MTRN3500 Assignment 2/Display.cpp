#include "Display.h"

using namespace System::IO;

Display::Display(SM_ThreadManagement^ threadMemory,
    SM_Laser^ laserMemory)
{
    SM_TM_ = threadMemory;
    laserMemory_ = laserMemory;

    const int axisBytes = STANDARD_LASER_LENGTH * sizeof(double);
    pointsMm_ = gcnew array<double>(2 * STANDARD_LASER_LENGTH);
    dataX_ = gcnew array<Byte>(axisBytes);
    dataY_ = gcnew array<Byte>(axisBytes);
}

error_state Display::connect(String^ hostName, int portNumber)
{
    try
    {
        Client = gcnew TcpClient(hostName, portNumber);
        Client->NoDelay = true;
        Stream = Client->GetStream();
        Stream->WriteTimeout = 500;

        Console::WriteLine("Display connected to {0}:{1}", hostName, portNumber);
        return SUCCESS;
    }
    catch (Exception^ ex)
    {
        Console::WriteLine("Display connection error: {0}", ex->Message);

        if (Client != nullptr)
            Client->Close();

        Client = nullptr;
        Stream = nullptr;
        return ERR_CONNECTION;
    }
}

error_state Display::processSharedMemory()
{
    const int count = STANDARD_LASER_LENGTH;

    Monitor::Enter(laserMemory_->lockObject);
    try
    {
        for (int i = 0; i < count; ++i)
        {
            pointsMm_[i] = laserMemory_->x[i] * 1000.0;
            pointsMm_[count + i] = laserMemory_->y[i] * 1000.0;
        }
    }
    finally
    {
        Monitor::Exit(laserMemory_->lockObject);
    }

    // BlockCopy offsets and lengths are in bytes.
    const int axisBytes = dataX_->Length;
    Buffer::BlockCopy(pointsMm_, 0, dataX_, 0, axisBytes);
    Buffer::BlockCopy(pointsMm_, axisBytes, dataY_, 0, axisBytes);

    return SUCCESS;
}

error_state Display::communicate()
{
    if (Stream == nullptr)
        return ERR_CONNECTION;

    try
    {
        // The supplied MATLAB example sends all X values before all Y values.
        Stream->Write(dataX_, 0, dataX_->Length);
        Thread::Sleep(10);
        Stream->Write(dataY_, 0, dataY_->Length);
        return SUCCESS;
    }
    catch (IOException^ ex)
    {
        SocketException^ socketError =
            dynamic_cast<SocketException^>(ex->GetBaseException());

        if (socketError != nullptr)
        {
            Console::WriteLine(
                "[Display] Socket error: {0}",
                socketError->SocketErrorCode
            );
        }

        Console::WriteLine(
            "[Display] Send failed: {0}",
            ex->Message
        );

        return ERR_CONNECTION;
    }
}

void Display::processHeartbeats()
{
    Monitor::Enter(SM_TM_->lockObject);
    try
    {
        SM_TM_->heartbeat |= bit_DISPLAY;
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
    }
}

bool Display::getShutdownFlag()
{
    Monitor::Enter(SM_TM_->lockObject);
    try
    {
        return (SM_TM_->shutdown & bit_DISPLAY) != 0;
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
    }
}

void Display::shutdownThreads()
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
}

void Display::threadFunction()
{
    Console::WriteLine("Display thread starting");
    SM_TM_->ThreadBarrier->SignalAndWait();

    try
    {
        int sentFrames = 0;
        while (!getShutdownFlag())
        {
            if (processSharedMemory() != SUCCESS || communicate() != SUCCESS)
            {
                shutdownThreads();
                break;
            }
            ++sentFrames;
            //if (sentFrames == 1 || sentFrames % 20 == 0)
                // Console::WriteLine("Display frames sent: {0}", sentFrames);
            processHeartbeats();
            Thread::Sleep(50);
        }
    }
    finally
    {
        if (Client != nullptr)
            Client->Close();

        Stream = nullptr;
        Client = nullptr;
        Console::WriteLine("Display thread terminated");
    }
}