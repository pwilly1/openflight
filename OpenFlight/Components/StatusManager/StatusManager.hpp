// ======================================================================
// \title  StatusManager.hpp
// \author prwil
// \brief  hpp file for StatusManager component implementation class
// ======================================================================

#ifndef OpenFlight_StatusManager_HPP
#define OpenFlight_StatusManager_HPP

#include "OpenFlight/Components/StatusManager/StatusManagerComponentAc.hpp"

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

    U8 m_collectionEnabled = 0;
    U32 m_statusRequests = 0;
};

}  // namespace OpenFlight

#endif
