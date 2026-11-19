
#using <System.dll>
#include "Laser.h"

using namespace System;
using namespace System::Threading;
using namespace System::Diagnostics;
using namespace System::Net::Sockets;
using namespace System::Text;
using namespace System::IO;

// Constructor
Laser::Laser(SM_ThreadManagement^ SM_TM,
    SM_Laser^ SM_laser)
{
    SM_TM_ = SM_TM;
    Laser_SM_Laser_ = SM_laser;

    Laser_Stopwatch = gcnew Stopwatch();
    Laser_PrintStopwatch = gcnew Stopwatch();

    Client = nullptr;
    Stream = nullptr;

    ReadData = gcnew array<unsigned char>(4096);
}


// Shared Memory Setup
error_state Laser::setupShareMemory()
{
    if (SM_TM_ == nullptr || Laser_SM_Laser_ == nullptr)
        return ERR_SM;

    return SUCCESS;
}


// Main Thread Function
void Laser::threadFunction()
{
    Console::WriteLine("Laser Thread is starting");

    // Wait for all threads
    SM_TM_->ThreadBarrier->SignalAndWait();

    Laser_Stopwatch->Start();
    Laser_PrintStopwatch->Start();

    while (!getShutdownFlag())
    {
        // Heartbeat
        if (processHeartbeats() != SUCCESS)
            break;

        // LiDAR communication
        if (communicate() == SUCCESS &&
            checkData() == SUCCESS)
        {
            processSharedMemory();
        }

        Thread::Sleep(20);
    }

    // Close TCP connection
    if (Stream != nullptr)
    {
        Stream->Close();
        Stream = nullptr;
    }

    if (Client != nullptr)
    {
        Client->Close();
        Client = nullptr;
    }

    Console::WriteLine("LiDAR Thread is terminated");
}


// Heartbeat Management
error_state Laser::processHeartbeats()
{
    Monitor::Enter(SM_TM_->lockObject);

    try
    {
        if ((SM_TM_->heartbeat & bit_LASER) == 0)
        {
            SM_TM_->heartbeat |= bit_LASER;
            Laser_Stopwatch->Restart();
        }
        else
        {
            if (Laser_Stopwatch->ElapsedMilliseconds
                > CRASH_LIMIT)
            {
                shutdownThreads();
                return ERR_NO_DATA;
            }
        }
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
    }

    return SUCCESS;
}


// Shutdown All Threads
void Laser::shutdownThreads()
{
    Monitor::Enter(SM_TM_->lockObject);

    try
    {
        SM_TM_->shutdown = bit_ALL;
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
    }
}


// Check Shutdown Flag
bool Laser::getShutdownFlag()
{
    return (SM_TM_->shutdown & bit_LASER) != 0;
}


// Send TCP Command
void Laser::sendCommand(String^ command)
{
    array<unsigned char>^ SendData =
        Encoding::ASCII->GetBytes(
            "\x02" + command + "\x03"
        );

    Stream->Write(
        SendData, 0, SendData->Length
    );
}


// Read Complete LiDAR Telegram
String^ Laser::readTelegram()
{
    StringBuilder^ message = gcnew StringBuilder();

    int data;

    // Wait for STX
    do
    {
        data = Stream->ReadByte();

        if (data == -1)
            throw gcnew IOException("Connection closed");

    } while (data != 0x02);

    // Read until ETX
    while (true)
    {
        data = Stream->ReadByte();

        if (data == -1)
            throw gcnew IOException("Connection closed");

        if (data == 0x03)
            break;

        message->Append((wchar_t)data);

        if (message->Length > 32768)
            throw gcnew IOException("Telegram too large");
    }

    return message->ToString();
}


// Establish TCP Connection
error_state Laser::connect(String^ hostName,
    int portNumber)
{
    try
    {
        Client = gcnew TcpClient();

        Client->Connect(hostName, portNumber);

        Stream = Client->GetStream();

        Stream->ReadTimeout = 1000;
        Stream->WriteTimeout = 1000;

        // Start measurement
        sendCommand("sMN LMCstartmeas");
        String^ response = readTelegram();

        if (!response->StartsWith("sAN LMCstartmeas"))
            throw gcnew IOException(
                "Start measurement not acknowledged"
            );

        // Configure scanning
        sendCommand(
            "sMN mLMPsetscancfg +5000 +1 +5000 0 +1800000"
        );

        response = readTelegram();

        if (!response->StartsWith("sAN mLMPsetscancfg"))
            throw gcnew IOException(
                "Scan configuration not acknowledged"
            );

        // Enable scan output
        sendCommand("sEN LMDscandata 1");

        response = readTelegram();

        if (!response->StartsWith("sEA LMDscandata"))
            throw gcnew IOException(
                "Scan output not acknowledged"
            );

        Console::WriteLine("LiDAR TCP connected");

        return SUCCESS;
    }
    catch (Exception^ ex)
    {
        Console::WriteLine(
            "LiDAR connection error: {0}",
            ex->Message
        );

        if (Stream != nullptr)
            Stream->Close();

        if (Client != nullptr)
            Client->Close();

        Stream = nullptr;
        Client = nullptr;

        return ERR_CONNECTION;
    }
}


// Receive and Convert LiDAR Data
error_state Laser::communicate()
{
    if (Stream == nullptr)
        return ERR_CONNECTION;

    try
    {
        String^ LMDscandata = nullptr;

        // Receive one scan telegram
        for (int attempt = 0; attempt < 10; attempt++)
        {
            String^ message = readTelegram();

            if (message->Contains("LMDscandata") &&
                message->Contains("DIST1"))
            {
                LMDscandata = message;
                break;
            }
        }

        if (LMDscandata == nullptr)
            return ERR_NO_DATA;

        // Split message into fields
        array<wchar_t>^ Space =
            gcnew array<wchar_t>{ ' ' };

        array<String^>^ StringArray =
            LMDscandata->Split(
                Space,
                StringSplitOptions::RemoveEmptyEntries
            );

        // Find DIST1 channel
        int index = -1;

        for (int i = 0; i < StringArray->Length; i++)
        {
            if (StringArray[i] == "DIST1")
            {
                index = i;
                break;
            }
        }

        if (index < 0 ||
            StringArray->Length < index + 6)
            return ERR_INVALID_DATA;

        // Extract scanning parameters
        double StartAngle =
            (double)(Int32)Convert::ToUInt32(
                StringArray[index + 3], 16
            ) / 10000.0;

        double Resolution =
            Convert::ToInt32(
                StringArray[index + 4], 16
            ) / 10000.0;

        int NumberData =
            Convert::ToInt32(
                StringArray[index + 5], 16
            );

        if (NumberData != STANDARD_LASER_LENGTH)
            return ERR_INVALID_DATA;

        if (StringArray->Length < index + 6 + NumberData)
            return ERR_INVALID_DATA;

        array<double>^ RangeX =
            gcnew array<double>(NumberData);

        array<double>^ RangeY =
            gcnew array<double>(NumberData);

        // Polar to Cartesian conversion
        for (int i = 0; i < NumberData; i++)
        {
            double Range =
                Convert::ToInt32(
                    StringArray[index + 6 + i], 16
                );

            // mm to metres
            Range = Range / 1000.0;

            // Degrees to radians
            double Angle =
                (StartAngle + i * Resolution)
                * Math::PI / 180.0;

            RangeX[i] = Range * Math::Sin(Angle);
            RangeY[i] = -Range * Math::Cos(Angle);
        }

        // Write to shared memory
        Monitor::Enter(Laser_SM_Laser_->lockObject);

        try
        {
            for (int i = 0; i < NumberData; i++)
            {
                Laser_SM_Laser_->x[i] = RangeX[i];
                Laser_SM_Laser_->y[i] = RangeY[i];
            }
        }
        finally
        {
            Monitor::Exit(Laser_SM_Laser_->lockObject);
        }

        return SUCCESS;
    }
    catch (IOException^)
    {
        return ERR_NO_DATA;
    }
    catch (Exception^ ex)
    {
        Console::WriteLine(
            "LiDAR data error: {0}",
            ex->Message
        );

        return ERR_INVALID_DATA;
    }
}


// Check Data
error_state Laser::checkData()
{
    // Data length and conversion have been
    // validated in communicate()

    return SUCCESS;
}


// Process Shared Memory and Print Data
error_state Laser::processSharedMemory()
{
    // Print once per second
    if (Laser_PrintStopwatch->ElapsedMilliseconds < 1000)
        return SUCCESS;

    Laser_PrintStopwatch->Restart();

    array<double>^ x =
        gcnew array<double>(STANDARD_LASER_LENGTH);

    array<double>^ y =
        gcnew array<double>(STANDARD_LASER_LENGTH);

    // Copy a consistent scan
    Monitor::Enter(Laser_SM_Laser_->lockObject);

    try
    {
        Array::Copy(
            Laser_SM_Laser_->x, x,
            STANDARD_LASER_LENGTH
        );

        Array::Copy(
            Laser_SM_Laser_->y, y,
            STANDARD_LASER_LENGTH
        );
    }
    finally
    {
        Monitor::Exit(Laser_SM_Laser_->lockObject);
    }

    Console::WriteLine("\nLiDAR Data (361 Points)");
    Console::WriteLine("Point\tX (m)\t\tY (m)");

    for (int i = 0; i < STANDARD_LASER_LENGTH; i++)
    {
        Console::WriteLine(
            "{0}\t{1:F3}\t\t{2:F3}",
            i, x[i], y[i]
        );
    }

    return SUCCESS;
}
