import unittest

from dashboard.server import parse_channel_line, parse_event_line


class DashboardParserTests(unittest.TestCase):
    def test_parses_observed_channel_json(self) -> None:
        parsed = parse_channel_line(
            '{"time":"2026-10-31T14:30:52.514830",'
            '"name":"OpenFlight.statusManager.SensorTemperature",'
            '"display_text":2489}'
        )
        self.assertEqual(parsed, ("SensorTemperature", 2489, "2026-10-31T14:30:52.514830"))

    def test_ignores_gds_info_output(self) -> None:
        self.assertIsNone(parse_channel_line("[INFO] Autodetected artifacts root"))

    def test_parses_observed_event_text(self) -> None:
        parsed = parse_event_line(
            "2026-10-31T14:31:13.725945: "
            "OpenFlight.statusManager.StatusReported (268455936) "
            "(2(0)-1793471473:725945) EventSeverity.ACTIVITY_HI : "
            "Collection enabled: 1; status requests: 1"
        )
        self.assertIsNotNone(parsed)
        assert parsed is not None
        self.assertEqual(parsed["name"], "StatusReported")
        self.assertEqual(parsed["severity"], "ACTIVITY_HI")
        self.assertEqual(parsed["message"], "Collection enabled: 1; status requests: 1")


if __name__ == "__main__":
    unittest.main()
