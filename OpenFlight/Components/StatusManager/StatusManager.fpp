module OpenFlight {
    @ Handles ground requests for the flight computer's initial status slice.
    active component StatusManager {
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

        @ One when simulated collection is enabled, zero otherwise.
        telemetry CollectionEnabled: U8

        @ Number of ground status requests handled since boot.
        telemetry StatusRequests: U32

        time get port timeCaller
        import Fw.Command
        import Fw.Event
        import Fw.Channel
    }
}
