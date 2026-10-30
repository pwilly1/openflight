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
    void run_handler(FwIndexType portNum, U32 context) override;
    bool openLink();
    bool sendCommand(U8 code);
    void handlePacket(const of_packet& packet);

    U8 m_collectionEnabled = 0;
    U32 m_statusRequests = 0;
    int m_serialFd = -1;
    std::mutex m_serialMutex;
    of_packet_stream m_stream = {};
    U16 m_commandSequence = 0;
    U16 m_pendingSequence = 0;
    U8 m_pendingCommand = 0;
    U32 m_sensorPackets = 0;
    U32 m_badPackets = 0;
    bool m_seenSensor = false;
    bool m_linkLost = false;
    std::chrono::steady_clock::time_point m_lastSensor;
};

}  // namespace OpenFlight

#endif
