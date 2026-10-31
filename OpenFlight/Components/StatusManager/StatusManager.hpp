// ======================================================================
// \title  StatusManager.hpp
// \author prwil
// \brief  hpp file for StatusManager component implementation class
// ======================================================================

#ifndef OpenFlight_StatusManager_HPP
#define OpenFlight_StatusManager_HPP

#include "OpenFlight/Components/StatusManager/StatusManagerComponentAc.hpp"
#include "../../../protocol/packet.h"
#include <mutex>
#include <chrono>

namespace OpenFlight {

class StatusManager final : public StatusManagerComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct StatusManager object
    explicit StatusManager(const char* const compName);

    //! Destroy StatusManager object
    ~StatusManager();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    void GET_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void START_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void STOP_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void INJECT_CORRUPT_PACKET_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void INJECT_DROP_PACKETS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U8 count) override;
    void INJECT_LOW_BATTERY_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void INJECT_FREEZE_SENSOR_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void RESTORE_SENSOR_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void RECOVER_SAFE_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    bool openLink();
    bool sendCommand(U8 code, U8 value = 0, bool includeValue = false);
    void handlePacket(const of_packet& packet);
    void enterSafeMode(U8 reason);
    void noteHealthySample();
    void checkLinkTimeout();

    U8 m_collectionEnabled = 0;
    U32 m_statusRequests = 0;
    int m_serialFd = -1;
    std::mutex m_serialMutex;
    of_packet_stream m_stream = {};
    U16 m_commandSequence = 0;
    U16 m_pendingSequence = 0;
    U8 m_pendingCommand = 0;
    U8 m_pendingValue = 0;
    U32 m_sensorPackets = 0;
    U32 m_badPackets = 0;
    bool m_seenSensor = false;
    bool m_linkLost = false;
    U8 m_operatingMode = 0;
    U8 m_activeFault = 0;
    U8 m_healthySamples = 0;
    U8 m_lowBatterySamples = 0;
    U8 m_unchangedSamples = 0;
    U8 m_badPacketStreak = 0;
    bool m_havePreviousSample = false;
    I16 m_previousTemperature = 0;
    I16 m_previousRoll = 0;
    I16 m_previousPitch = 0;
    I16 m_previousYaw = 0;
    U16 m_previousBattery = 0;
    std::chrono::steady_clock::time_point m_lastSensor;
};

}  // namespace OpenFlight

#endif
