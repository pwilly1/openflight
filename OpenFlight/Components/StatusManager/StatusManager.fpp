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

        @ Emitted when status is requested.
        event StatusReported(enabled: U8, requests: U32) severity activity high format "Collection enabled: {}; status requests: {}"

        @ Emitted when collection state changes.
        event CollectionChanged(enabled: U8) severity activity high format "Collection enabled: {}"
        event SensorReceived(sequence: U16, temperature: I16, battery: U16) severity activity low format "Sensor sequence: {}; temperature centi C: {}; battery mV: {}"
        event SensorPacketRejected(reason: I32) severity warning high format "Sensor packet rejected: {}"
        event SensorLinkLost() severity warning high format "Sensor link lost"

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

        time get port timeCaller
        import Fw.Command
        import Fw.Event
        import Fw.Channel
    }
}
