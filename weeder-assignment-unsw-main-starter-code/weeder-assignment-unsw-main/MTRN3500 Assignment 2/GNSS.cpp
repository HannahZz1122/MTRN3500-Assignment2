#include "GNSS.h"

using namespace System::IO;

namespace
{
    constexpr int GNSS_PORT = 24000;
    constexpr int BESTUTM_ID = 726;
    constexpr int BESTUTM_BODY_SIZE = 80;
    constexpr int MIN_HEADER_SIZE = 28;
    constexpr int CRC_SIZE = 4;
    constexpr UInt32 CRC_POLYNOMIAL = 0xEDB88320u;

    UInt32 calculateCRC(array<Byte>^ data, int length)
    {
        UInt32 crc = 0;
        for (int i = 0; i < length; ++i)
        {
            crc ^= data[i];
            for (int bit = 0; bit < 8; ++bit)
            {
                const bool lowBit = (crc & 1u) != 0;
                crc >>= 1;
                if (lowBit)
                    crc ^= CRC_POLYNOMIAL;
            }
        }
        return crc;
    }
}

GPS::GPS(SM_ThreadManagement^ threadMemory, SM_GPS^ gpsMemory)
{
    SM_TM_ = threadMemory;
    gpsMemory_ = gpsMemory;
    ReadData = gcnew array<Byte>(1024);
    pending_ = gcnew List<Byte>();
}

error_state GPS::setupShareMemory()
{
    return SM_TM_ != nullptr && gpsMemory_ != nullptr
        ? SUCCESS : ERR_SM;
}

error_state GPS::connect(String^ hostName, int portNumber)
{
    try
    {
        Client = gcnew TcpClient();
        auto connection = Client->ConnectAsync(hostName, portNumber);
        if (!connection->Wait(1500))
            throw gcnew TimeoutException("TCP connection timed out");

        Stream = Client->GetStream();
        Stream->ReadTimeout = 250;
        pending_->Clear();
        Console::WriteLine("GNSS connected to {0}:{1}", hostName, portNumber);
        return SUCCESS;
    }
    catch (Exception^ ex)
    {
        Console::WriteLine("GNSS connection error: {0}", ex->GetBaseException()->Message);
        if (Client != nullptr)
            Client->Close();
        Client = nullptr;
        Stream = nullptr;
        return ERR_CONNECTION;
    }
}

error_state GPS::processHeartbeats()
{
    Monitor::Enter(SM_TM_->lockObject);
    try
    {
        SM_TM_->heartbeat |= bit_GPS;
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
    }
    return SUCCESS;
}

bool GPS::getShutdownFlag()
{
    Monitor::Enter(SM_TM_->lockObject);
    try
    {
        return (SM_TM_->shutdown & bit_GPS) != 0;
    }
    finally
    {
        Monitor::Exit(SM_TM_->lockObject);
    }
}

bool GPS::takeFrame()
{
    while (pending_->Count >= 3)
    {
        int start = 0;
        while (start + 2 < pending_->Count &&
            (pending_[start] != 0xAA || pending_[start + 1] != 0x44 ||
                pending_[start + 2] != 0x12))
        {
            ++start;
        }
        // Preserve any incomplete sync sequence at the end of the buffer.
        pending_->RemoveRange(0, start);
        if (pending_->Count < 10)
            return false;

        const int headerLength = pending_[3];
        if (headerLength < MIN_HEADER_SIZE)
        {
            pending_->RemoveAt(0);
            continue;
        }

        const int bodyLength = pending_[8] | (pending_[9] << 8);
        const int frameLength = headerLength + bodyLength + CRC_SIZE;
        if (pending_->Count < frameLength)
            return false;

        frame_ = pending_->GetRange(0, frameLength)->ToArray();
        pending_->RemoveRange(0, frameLength);
        return true;
    }
    return false;
}

error_state GPS::communicate()
{
    frame_ = nullptr;
    if (Stream == nullptr)
        return ERR_CONNECTION;

    // A previous TCP read may already have supplied another whole frame.
    if (takeFrame())
        return SUCCESS;

    try
    {
        int count = Stream->Read(ReadData, 0, ReadData->Length);
        if (count == 0)
        {
            Console::WriteLine("GNSS connection closed by server");
            return ERR_CONNECTION;
        }

        for (int i = 0; i < count; ++i)
            pending_->Add(ReadData[i]);

        return takeFrame() ? SUCCESS : ERR_NO_DATA;
    }
    catch (IOException^ ex)
    {
        SocketException^ socketError = dynamic_cast<SocketException^>(ex->InnerException);
        if (socketError != nullptr && socketError->SocketErrorCode == SocketError::TimedOut)
            Console::WriteLine(
                "[GNSS] Socket error: {0}",
                socketError->SocketErrorCode
            );
            return ERR_NO_DATA;

        Console::WriteLine("GNSS read error: {0}", ex->Message);
        return ERR_CONNECTION;
    }
}

error_state GPS::checkData()
{
    if (frame_ == nullptr)
        return ERR_NO_DATA;

    const int headerLength = frame_[3];
    const int crcOffset = frame_->Length - CRC_SIZE;
    const int messageId = BitConverter::ToUInt16(frame_, 4);
    const int bodyLength = crcOffset - headerLength;

    if (messageId != BESTUTM_ID || bodyLength != BESTUTM_BODY_SIZE)
    {
        Console::WriteLine("GNSS unexpected message: ID {0}, payload {1} bytes",
            messageId, bodyLength);
        return ERR_INVALID_DATA;
    }

    receivedCRC_ = BitConverter::ToUInt32(frame_, crcOffset);
    calculatedCRC_ = calculateCRC(frame_, crcOffset);
    if (receivedCRC_ != calculatedCRC_)
    {
        Console::WriteLine("GNSS CRC mismatch: received {0:X8}, calculated {1:X8}",
            receivedCRC_, calculatedCRC_);
        return ERR_INVALID_DATA;
    }
    return SUCCESS;
}

error_state GPS::processSharedMemory()
{
    const int headerLength = frame_[3];
    const double northing = BitConverter::ToDouble(frame_, headerLength + 16);
    const double easting = BitConverter::ToDouble(frame_, headerLength + 24);
    const double height = BitConverter::ToDouble(frame_, headerLength + 32);

    if (Double::IsNaN(northing) || Double::IsInfinity(northing) ||
        Double::IsNaN(easting) || Double::IsInfinity(easting) ||
        Double::IsNaN(height) || Double::IsInfinity(height))
    {
        Console::WriteLine("GNSS position contains a non-finite value");
        return ERR_INVALID_DATA;
    }

    Monitor::Enter(gpsMemory_->lockObject);
    try
    {
        gpsMemory_->Northing = northing;
        gpsMemory_->Easting = easting;
        gpsMemory_->Height = height;
    }
    finally
    {
        Monitor::Exit(gpsMemory_->lockObject);
    }

    Console::WriteLine(
        "GNSS N: {0:F3} m | E: {1:F3} m | H: {2:F3} m | CRC RX: {3:X8} | CRC calc: {4:X8}",
        northing, easting, height, receivedCRC_, calculatedCRC_);
    return SUCCESS;
}

void GPS::threadFunction()
{
    Console::WriteLine("GNSS thread starting");
    SM_TM_->ThreadBarrier->SignalAndWait();

    try
    {
        // Release the startup barrier before making an optional sensor connection.
        if (setupShareMemory() != SUCCESS || getShutdownFlag() ||
            connect(gcnew String(WEEDER_ADDRESS), GNSS_PORT) != SUCCESS)
            return;

        while (!getShutdownFlag())
        {
            processHeartbeats();
            error_state result = communicate();
            if (result == ERR_CONNECTION)
                break;

            if (result == SUCCESS && checkData() == SUCCESS)
                processSharedMemory();
        }
    }
    finally
    {
        if (Client != nullptr)
            Client->Close();
        Stream = nullptr;
        Client = nullptr;
        Console::WriteLine("GNSS thread terminated");
    }
}
