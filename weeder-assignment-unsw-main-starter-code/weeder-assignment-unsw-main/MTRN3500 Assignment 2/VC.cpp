#include "VC.h"

using namespace System::Globalization;
using namespace System::IO;

namespace
{
    constexpr int IO_TIMEOUT_MS = 1000;
    constexpr int CONNECT_TIMEOUT_MS = 1500;
    constexpr int COMMAND_INTERVAL_MS = 20;
    constexpr int MAX_AUTH_REPLY_LENGTH = 64;
}

VehicleControl::VehicleControl(SM_ThreadManagement^ threadMemory,
    SM_VehicleControl^ vehicleMemory)
{
    SM_TM_ = threadMemory;
    VC_SM_VC_ = vehicleMemory;
    Speed_ = 0.0;
    Steering_ = 0.0;
    watchdog_ = 0;
}

error_state VehicleControl::connect(String^ hostName, int portNumber)
{
    closeConnection();
    try
    {
        Client = gcnew TcpClient();
        auto connection = Client->ConnectAsync(hostName, portNumber);
        if (!connection->Wait(CONNECT_TIMEOUT_MS))
            throw gcnew TimeoutException("TCP connection timed out.");

        Client->NoDelay = true;
        Stream = Client->GetStream();
        Stream->WriteTimeout = IO_TIMEOUT_MS;

        String^ id = "5539653";
        array<Byte>^ auth = Encoding::ASCII->GetBytes(id + "\n");
        Stream->Write(auth, 0, auth->Length);

        StringBuilder^ reply = gcnew StringBuilder();
        Stopwatch^ timer = Stopwatch::StartNew();
        while (true)
        {
            int remaining = IO_TIMEOUT_MS - static_cast<int>(timer->ElapsedMilliseconds);
            if (remaining <= 0)
                throw gcnew TimeoutException("Authentication reply timed out.");
            Stream->ReadTimeout = remaining;

            int value = Stream->ReadByte();
            if (value == -1)
                throw gcnew EndOfStreamException("Server closed the connection during authentication.");
            if (value == '\n')
                break;
            if (reply->Length == MAX_AUTH_REPLY_LENGTH)
                throw gcnew IOException("Authentication reply exceeded the expected length.");

            reply->Append(static_cast<wchar_t>(value));
        }

        String^ response = reply->ToString()->Trim();
        if (!response->StartsWith("OK", StringComparison::Ordinal))
            throw gcnew IOException("Server did not acknowledge authentication.");

        watchdog_ = 0;
        Console::WriteLine("Vehicle Control authenticated on {0}:{1}", hostName, portNumber);
        return SUCCESS;
    }
    catch (Exception^ ex)
    {
        Console::WriteLine("Vehicle Control connection error: {0}", ex->GetBaseException()->Message);
        closeConnection();
        return ERR_CONNECTION;
    }
}

error_state VehicleControl::processSharedMemory()
{
    Monitor::Enter(VC_SM_VC_->lockObject);
    try
    {
        Speed_ = VC_SM_VC_->Speed;
        Steering_ = VC_SM_VC_->Steering;
    }
    finally
    {
        Monitor::Exit(VC_SM_VC_->lockObject);
    }

    return checkData();
}

error_state VehicleControl::checkData()
{
    if (Double::IsNaN(Speed_) || Double::IsInfinity(Speed_) ||
        Double::IsNaN(Steering_) || Double::IsInfinity(Steering_) ||
        Math::Abs(Speed_) > 1.0 || Math::Abs(Steering_) > 40.0)
    {
        Console::WriteLine("Vehicle Control command is outside the permitted range.");
        return ERR_INVALID_DATA;
    }

    return SUCCESS;
}

error_state VehicleControl::communicate()
{
    if (Stream == nullptr)
        return ERR_CONNECTION;

    int watchdog = watchdog_;
    String^ command = String::Format(CultureInfo::InvariantCulture,
        "# {0:F3} {1:F3} {2} #", Steering_, Speed_, watchdog);
    array<Byte>^ bytes = Encoding::ASCII->GetBytes(command);

    try
    {
        Stream->Write(bytes, 0, bytes->Length);
        Console::WriteLine("VC TX: [{0}]", command);
        watchdog_ ^= 1;
        return SUCCESS;
    }
    catch (IOException^ ex)
    {
        Console::WriteLine("Vehicle Control send error: {0}", ex->Message);
        return ERR_CONNECTION;
    }
}

error_state VehicleControl::processHeartbeats()
{
    Monitor::Enter(SM_TM_->lockObject);
    try
    {
        SM_TM_->heartbeat |= bit_VC;
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
    }

    return SUCCESS;
}

bool VehicleControl::getShutdownFlag()
{
    Monitor::Enter(SM_TM_->lockObject);
    try
    {
        return (SM_TM_->shutdown & bit_VC) != 0;
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
    }
}

void VehicleControl::shutdownThreads()
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

void VehicleControl::closeConnection()
{
    if (Client != nullptr)
        Client->Close();
    Stream = nullptr;
    Client = nullptr;
}

void VehicleControl::threadFunction()
{
    Console::WriteLine("Vehicle Control thread starting");
    SM_TM_->ThreadBarrier->SignalAndWait();

    try
    {
        while (!getShutdownFlag())
        {
            if (processSharedMemory() != SUCCESS || communicate() != SUCCESS)
            {
                shutdownThreads();
                break;
            }

            processHeartbeats();
            Thread::Sleep(COMMAND_INTERVAL_MS);
        }
    }
    finally
    {
        // Make one best-effort neutral write before closing the socket.
        Speed_ = 0.0;
        Steering_ = 0.0;
        try
        {
            if (Stream != nullptr)
                communicate();
        }
        finally
        {
            closeConnection();
            Console::WriteLine("Vehicle Control thread terminated");
        }
    }
}
