// ======================================================================
// \title  StatusManager.cpp
// \author prwil
// \brief  cpp file for StatusManager component implementation class
// ======================================================================

#include "OpenFlight/Components/StatusManager/StatusManager.hpp"
#include <cstdlib>
#include <cerrno>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace OpenFlight {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

StatusManager ::StatusManager(const char* const compName) : StatusManagerComponentBase(compName) {}

StatusManager ::~StatusManager() { if (m_serialFd >= 0) { close(m_serialFd); } }

bool StatusManager::openLink() {
    if (m_serialFd >= 0) { return true; }
    const char* path = std::getenv("OPENFLIGHT_SENSOR_PTY");
    if (path == nullptr || *path == '\0') { return false; }
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { return false; }
    struct termios options;
    if (tcgetattr(fd, &options) != 0) { close(fd); return false; }
    cfmakeraw(&options);
    if (tcsetattr(fd, TCSANOW, &options) != 0) { close(fd); return false; }
    m_serialFd = fd;
    return true;
}

bool StatusManager::sendCommand(U8 code) {
    std::lock_guard<std::mutex> lock(m_serialMutex);
    if (!openLink()) { return false; }
    of_packet packet = {};
    packet.type = OF_MSG_COMMAND;
    packet.sequence = ++m_commandSequence;
    packet.payload_length = 1;
    packet.payload[0] = code;
    uint8_t frame[OF_PACKET_MAX_FRAME];
    size_t length = 0;
    if (of_packet_encode(&packet, frame, sizeof(frame), &length) != OF_PACKET_OK) { return false; }
    ssize_t count = write(m_serialFd, frame, length);
    if (count != static_cast<ssize_t>(length)) {
        close(m_serialFd); m_serialFd = -1;
        return false;
    }
    m_pendingSequence = packet.sequence;
    m_pendingCommand = code;
    return true;
}

void StatusManager::handlePacket(const of_packet& packet) {
    if (packet.type == OF_MSG_ACK && packet.payload_length == 2 &&
        packet.sequence == m_pendingSequence && packet.payload[0] == m_pendingCommand) {
        if (packet.payload[1] == 1) {
            m_collectionEnabled = m_pendingCommand == 1 ? 1 : 0;
            this->tlmWrite_CollectionEnabled(m_collectionEnabled);
            this->log_ACTIVITY_HI_CollectionChanged(m_collectionEnabled);
        }
        m_pendingCommand = 0;
    } else if (packet.type == OF_MSG_SENSOR && packet.payload_length == 10) {
        auto read16 = [](const U8* p) -> U16 { return (static_cast<U16>(p[0]) << 8) | p[1]; };
        I16 temperature = static_cast<I16>(read16(&packet.payload[0]));
        I16 roll = static_cast<I16>(read16(&packet.payload[2]));
        I16 pitch = static_cast<I16>(read16(&packet.payload[4]));
        I16 yaw = static_cast<I16>(read16(&packet.payload[6]));
        U16 battery = read16(&packet.payload[8]);
        ++m_sensorPackets;
        m_lastSensor = std::chrono::steady_clock::now();
        m_seenSensor = true;
        m_linkLost = false;
        this->tlmWrite_SensorTemperature(temperature);
        this->tlmWrite_SensorRoll(roll);
        this->tlmWrite_SensorPitch(pitch);
        this->tlmWrite_SensorYaw(yaw);
        this->tlmWrite_SensorBattery(battery);
        this->tlmWrite_SensorPackets(m_sensorPackets);
        this->log_ACTIVITY_LO_SensorReceived(packet.sequence, temperature, battery);
    }
}

void StatusManager::run_handler(FwIndexType portNum, U32 context) {
    (void)portNum; (void)context;
    std::lock_guard<std::mutex> lock(m_serialMutex);
    if (!openLink()) {
        if (m_seenSensor && !m_linkLost && m_collectionEnabled &&
            std::chrono::steady_clock::now() - m_lastSensor > std::chrono::seconds(3)) {
            m_linkLost = true;
            this->log_WARNING_HI_SensorLinkLost();
        }
        return;
    }
    U8 bytes[256];
    ssize_t count;
    while ((count = read(m_serialFd, bytes, sizeof(bytes))) > 0) {
        for (ssize_t i = 0; i < count; ++i) {
            of_packet packet = {};
            int result = of_packet_stream_feed(&m_stream, bytes[i], &packet);
            if (result == 1) { handlePacket(packet); }
            else if (result < 0) {
                ++m_badPackets;
                this->tlmWrite_BadPackets(m_badPackets);
                this->log_WARNING_HI_SensorPacketRejected(result);
            }
        }
    }
    if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        close(m_serialFd); m_serialFd = -1; m_stream.used = 0;
    }
    if (m_seenSensor && !m_linkLost && m_collectionEnabled &&
        std::chrono::steady_clock::now() - m_lastSensor > std::chrono::seconds(3)) {
        m_linkLost = true;
        this->log_WARNING_HI_SensorLinkLost();
    }
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void StatusManager ::GET_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    ++this->m_statusRequests;
    this->tlmWrite_CollectionEnabled(this->m_collectionEnabled);
    this->tlmWrite_StatusRequests(this->m_statusRequests);
    this->log_ACTIVITY_HI_StatusReported(this->m_collectionEnabled, this->m_statusRequests);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StatusManager ::START_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->cmdResponse_out(opCode, cmdSeq, sendCommand(1) ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void StatusManager ::STOP_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->cmdResponse_out(opCode, cmdSeq, sendCommand(2) ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

}  // namespace OpenFlight
