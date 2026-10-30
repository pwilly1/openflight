// ======================================================================
// \title  StatusManager.cpp
// \author prwil
// \brief  cpp file for StatusManager component implementation class
// ======================================================================

#include "OpenFlight/Components/StatusManager/StatusManager.hpp"

namespace OpenFlight {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

StatusManager ::StatusManager(const char* const compName) : StatusManagerComponentBase(compName) {}

StatusManager ::~StatusManager() {}

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
    this->m_collectionEnabled = 1;
    this->tlmWrite_CollectionEnabled(this->m_collectionEnabled);
    this->log_ACTIVITY_HI_CollectionChanged(this->m_collectionEnabled);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StatusManager ::STOP_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_collectionEnabled = 0;
    this->tlmWrite_CollectionEnabled(this->m_collectionEnabled);
    this->log_ACTIVITY_HI_CollectionChanged(this->m_collectionEnabled);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace OpenFlight
