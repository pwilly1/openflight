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

namespace {
constexpr U8 MODE_NORMAL = 0;
constexpr U8 MODE_SAFE = 1;
constexpr U8 FAULT_NONE = 0;
constexpr U8 FAULT_LOW_BATTERY = 1;
constexpr U8 FAULT_LINK_LOST = 2;
constexpr U8 FAULT_FROZEN_SENSOR = 3;
constexpr U8 FAULT_BAD_PACKETS = 4;
constexpr U16 LOW_BATTERY_MILLIVOLTS = 3300;
constexpr U8 REQUIRED_HEALTHY_SAMPLES = 3;
constexpr U8 BAD_PACKET_LIMIT = 3;
constexpr U8 UNCHANGED_SAMPLE_LIMIT = 3;
}

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

bool StatusManager::sendCommand(U8 code, U8 value, bool includeValue) {
    std::lock_guard<std::mutex> lock(m_serialMutex);
    if (!openLink() || m_pendingCommand != 0) { return false; }
    of_packet packet = {};
    packet.type = OF_MSG_COMMAND;
    packet.sequence = ++m_commandSequence;
    packet.payload_length = includeValue ? 2 : 1;
    packet.payload[0] = code;
    if (includeValue) { packet.payload[1] = value; }
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
    m_pendingValue = value;
    return true;
}

void StatusManager::enterSafeMode(U8 reason) {
    const bool entering = m_operatingMode != MODE_SAFE;
    m_operatingMode = MODE_SAFE;
    m_activeFault = reason;
    m_healthySamples = 0;
    this->tlmWrite_OperatingMode(m_operatingMode);
    this->tlmWrite_ActiveFault(m_activeFault);
    this->tlmWrite_HealthySamples(m_healthySamples);
    if (entering) { this->log_WARNING_HI_SafeModeEntered(reason); }
}

void StatusManager::noteHealthySample() {
    if (m_operatingMode != MODE_SAFE || m_activeFault == FAULT_NONE) { return; }
    if (m_healthySamples < REQUIRED_HEALTHY_SAMPLES) { ++m_healthySamples; }
    this->tlmWrite_HealthySamples(m_healthySamples);
    if (m_healthySamples == REQUIRED_HEALTHY_SAMPLES) {
        const U8 clearedFault = m_activeFault;
        m_activeFault = FAULT_NONE;
        this->tlmWrite_ActiveFault(m_activeFault);
        this->log_ACTIVITY_HI_FaultCleared(clearedFault);
    }
}

void StatusManager::checkLinkTimeout() {
    if (m_seenSensor && !m_linkLost && m_collectionEnabled &&
        std::chrono::steady_clock::now() - m_lastSensor > std::chrono::seconds(3)) {
        m_linkLost = true;
        this->log_WARNING_HI_SensorLinkLost();
        enterSafeMode(FAULT_LINK_LOST);
    }
}

void StatusManager::handlePacket(const of_packet& packet) {
    if (packet.type == OF_MSG_ACK && packet.payload_length == 2 &&
        packet.sequence == m_pendingSequence && packet.payload[0] == m_pendingCommand) {
        if (packet.payload[1] == 1) {
            if (m_pendingCommand == OF_CMD_START_COLLECTION ||
                m_pendingCommand == OF_CMD_STOP_COLLECTION) {
                m_collectionEnabled = m_pendingCommand == OF_CMD_START_COLLECTION ? 1 : 0;
                this->tlmWrite_CollectionEnabled(m_collectionEnabled);
                this->log_ACTIVITY_HI_CollectionChanged(m_collectionEnabled);
            } else {
                const U8 value = m_pendingCommand == OF_CMD_DROP_PACKETS ? m_pendingValue :
                                 (m_pendingCommand == OF_CMD_RESTORE_SENSOR ? 0 : 1);
                this->log_ACTIVITY_HI_FaultInjectionChanged(m_pendingCommand, value);
            }
        } else {
            this->log_WARNING_LO_FaultCommandRejected(m_pendingCommand);
        }
        m_pendingCommand = 0;
    } else if (packet.type == OF_MSG_SENSOR && packet.payload_length == 10) {
        auto read16 = [](const U8* p) -> U16 { return (static_cast<U16>(p[0]) << 8) | p[1]; };
        I16 temperature = static_cast<I16>(read16(&packet.payload[0]));
        I16 roll = static_cast<I16>(read16(&packet.payload[2]));
        I16 pitch = static_cast<I16>(read16(&packet.payload[4]));
        I16 yaw = static_cast<I16>(read16(&packet.payload[6]));
        U16 battery = read16(&packet.payload[8]);
        if (m_linkLost) { this->log_ACTIVITY_HI_SensorLinkRestored(); }
        ++m_sensorPackets;
        m_lastSensor = std::chrono::steady_clock::now();
        m_seenSensor = true;
        m_linkLost = false;
        m_badPacketStreak = 0;
        const bool unchanged = m_havePreviousSample &&
                               temperature == m_previousTemperature && roll == m_previousRoll &&
                               pitch == m_previousPitch && yaw == m_previousYaw &&
                               battery == m_previousBattery;
        m_unchangedSamples = unchanged ?
            static_cast<U8>(m_unchangedSamples < 255 ? m_unchangedSamples + 1 : 255) : 0;
        m_lowBatterySamples = battery < LOW_BATTERY_MILLIVOLTS ?
            static_cast<U8>(m_lowBatterySamples < 255 ? m_lowBatterySamples + 1 : 255) : 0;
        m_previousTemperature = temperature;
        m_previousRoll = roll;
        m_previousPitch = pitch;
        m_previousYaw = yaw;
        m_previousBattery = battery;
        m_havePreviousSample = true;
        this->tlmWrite_SensorTemperature(temperature);
        this->tlmWrite_SensorRoll(roll);
        this->tlmWrite_SensorPitch(pitch);
        this->tlmWrite_SensorYaw(yaw);
        this->tlmWrite_SensorBattery(battery);
        this->tlmWrite_SensorPackets(m_sensorPackets);
        this->log_ACTIVITY_LO_SensorReceived(packet.sequence, temperature, battery);
        if (m_lowBatterySamples >= 2) {
            enterSafeMode(FAULT_LOW_BATTERY);
        } else if (m_unchangedSamples >= UNCHANGED_SAMPLE_LIMIT) {
            enterSafeMode(FAULT_FROZEN_SENSOR);
        } else {
            noteHealthySample();
        }
    }
}

void StatusManager::run_handler(FwIndexType portNum, U32 context) {
    (void)portNum; (void)context;
    std::lock_guard<std::mutex> lock(m_serialMutex);
    if (!openLink()) {
        checkLinkTimeout();
        return;
    }
    // Check for an overdue sensor link before consuming newly arrived data.
    // Otherwise, the first packet after an outage can reset m_lastSensor and
    // hide a timeout that elapsed between scheduler invocations.
    checkLinkTimeout();
    U8 bytes[256];
    ssize_t count;
    while ((count = read(m_serialFd, bytes, sizeof(bytes))) > 0) {
        for (ssize_t i = 0; i < count; ++i) {
            of_packet packet = {};
            int result = of_packet_stream_feed(&m_stream, bytes[i], &packet);
            if (result == 1) { handlePacket(packet); }
            else if (result < 0) {
                ++m_badPackets;
                if (m_badPacketStreak < 255) { ++m_badPacketStreak; }
                this->tlmWrite_BadPackets(m_badPackets);
                this->log_WARNING_HI_SensorPacketRejected(result);
                if (m_badPacketStreak >= BAD_PACKET_LIMIT) { enterSafeMode(FAULT_BAD_PACKETS); }
            }
        }
    }
    if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        close(m_serialFd); m_serialFd = -1; m_stream.used = 0;
    }
    checkLinkTimeout();
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void StatusManager ::GET_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    std::lock_guard<std::mutex> lock(m_serialMutex);
    ++this->m_statusRequests;
    this->tlmWrite_CollectionEnabled(this->m_collectionEnabled);
    this->tlmWrite_StatusRequests(this->m_statusRequests);
    this->tlmWrite_OperatingMode(this->m_operatingMode);
    this->tlmWrite_ActiveFault(this->m_activeFault);
    this->tlmWrite_HealthySamples(this->m_healthySamples);
    this->log_ACTIVITY_HI_StatusReported(this->m_collectionEnabled, this->m_statusRequests);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StatusManager ::START_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->cmdResponse_out(opCode, cmdSeq, sendCommand(OF_CMD_START_COLLECTION) ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void StatusManager ::STOP_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->cmdResponse_out(opCode, cmdSeq, sendCommand(OF_CMD_STOP_COLLECTION) ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void StatusManager::INJECT_CORRUPT_PACKET_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->cmdResponse_out(opCode, cmdSeq, sendCommand(OF_CMD_CORRUPT_NEXT_PACKET) ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void StatusManager::INJECT_DROP_PACKETS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U8 count) {
    if (count == 0) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->cmdResponse_out(opCode, cmdSeq, sendCommand(OF_CMD_DROP_PACKETS, count, true) ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void StatusManager::INJECT_LOW_BATTERY_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->cmdResponse_out(opCode, cmdSeq, sendCommand(OF_CMD_LOW_BATTERY) ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void StatusManager::INJECT_FREEZE_SENSOR_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->cmdResponse_out(opCode, cmdSeq, sendCommand(OF_CMD_FREEZE_SENSOR) ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void StatusManager::RESTORE_SENSOR_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->cmdResponse_out(opCode, cmdSeq, sendCommand(OF_CMD_RESTORE_SENSOR) ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void StatusManager::RECOVER_SAFE_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    std::lock_guard<std::mutex> lock(m_serialMutex);
    if (m_operatingMode != MODE_SAFE || m_activeFault != FAULT_NONE ||
        m_healthySamples < REQUIRED_HEALTHY_SAMPLES) {
        this->log_WARNING_LO_RecoveryRejected(m_healthySamples, m_activeFault);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    m_operatingMode = MODE_NORMAL;
    m_healthySamples = 0;
    this->tlmWrite_OperatingMode(m_operatingMode);
    this->tlmWrite_HealthySamples(m_healthySamples);
    this->log_ACTIVITY_HI_SafeModeExited();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace OpenFlight
