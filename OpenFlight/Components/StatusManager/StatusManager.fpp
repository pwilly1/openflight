module OpenFlight {
    @ Handles ground requests for the flight computer's initial status slice.
    active component StatusManager {
        @ Poll the sensor serial link once per second.
        sync input port run: Svc.Sched
        @ Publish the current collection state and request count.
        async command GET_STATUS opcode 0

        @ Enable simulated sensor collection.
        async command START_COLLECTION opcode 1

        @ Disable simulated sensor collection.
        async command STOP_COLLECTION opcode 2

        @ Corrupt the next sensor packet after its CRC is calculated.
        async command INJECT_CORRUPT_PACKET opcode 3

        @ Drop a bounded number of sensor packets.
        async command INJECT_DROP_PACKETS(count: U8) opcode 4

        @ Report a persistent critical battery value.
        async command INJECT_LOW_BATTERY opcode 5

        @ Hold all sensor values constant while sequence numbers advance.
        async command INJECT_FREEZE_SENSOR opcode 6

        @ Clear all simulated sensor faults.
        async command RESTORE_SENSOR opcode 7

        @ Leave safe mode after three healthy samples and explicit approval.
        async command RECOVER_SAFE_MODE opcode 8

        @ Emitted when status is requested.
        event StatusReported(enabled: U8, requests: U32) severity activity high format "Collection enabled: {}; status requests: {}"

        @ Emitted when collection state changes.
        event CollectionChanged(enabled: U8) severity activity high format "Collection enabled: {}"
        event SensorReceived(sequence: U16, temperature: I16, battery: U16) severity activity low format "Sensor sequence: {}; temperature centi C: {}; battery mV: {}"
        event SensorPacketRejected(reason: I32) severity warning high format "Sensor packet rejected: {}"
        event SensorLinkLost() severity warning high format "Sensor link lost"
        event SensorLinkRestored() severity activity high format "Sensor link restored"
        event FaultInjectionChanged(code: U8, value: U8) severity activity high format "Fault injection code: {}; value: {}"
        event FaultCommandRejected(code: U8) severity warning low format "Sensor rejected fault command: {}"
        event SafeModeEntered(reason: U8) severity warning high format "Safe mode entered; reason: {}"
        event FaultCleared(reason: U8) severity activity high format "Fault cleared after healthy samples; reason: {}"
        event RecoveryRejected(healthySamples: U8, activeFault: U8) severity warning low format "Safe-mode recovery rejected; healthy samples: {}; active fault: {}"
        event SafeModeExited() severity activity high format "Safe mode exited by operator command"

        @ One when simulated collection is enabled, zero otherwise.
        telemetry CollectionEnabled: U8

        @ Number of ground status requests handled since boot.
        telemetry StatusRequests: U32
        telemetry SensorTemperature: I16
        telemetry SensorRoll: I16
        telemetry SensorPitch: I16
        telemetry SensorYaw: I16
        telemetry SensorBattery: U16
        telemetry SensorPackets: U32
        telemetry BadPackets: U32
        @ Zero for normal operation, one for safe mode.
        telemetry OperatingMode: U8
        @ Zero for no active fault; 1 low battery, 2 link loss, 3 frozen sensor, 4 bad packets.
        telemetry ActiveFault: U8
        telemetry HealthySamples: U8

        time get port timeCaller
        import Fw.Command
        import Fw.Event
        import Fw.Channel
    }
}
