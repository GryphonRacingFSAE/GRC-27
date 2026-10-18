import unittest

from ac_windows.fake_provider import FakeProvider
from ac_windows.provider import ReadOutcome, SourceState
from ac_windows.scenarios import SCENARIOS, pause_and_resume, startup_and_drive


# Play a script for a number of steps and return every snapshot
def play(script, steps):
    provider = FakeProvider(script)
    provider.attach()
    results = [provider.read() for _ in range(steps)]
    provider.close()
    return results


class TestReplay(unittest.TestCase):
    def test_two_replays_are_identical(self):
        for name, script in SCENARIOS.items():
            with self.subTest(scenario=name):
                self.assertEqual(
                    play(script, len(script)), play(script, len(script))
                )

    def test_reset_replays_from_the_start(self):
        p = FakeProvider(pause_and_resume)
        first = [p.read() for _ in range(3)]
        p.reset()
        second = [p.read() for _ in range(3)]
        self.assertEqual(first, second)


class TestStatesAreDistinguishable(unittest.TestCase):
    def test_valid_zero_is_not_missing(self):
        snap = play(startup_and_drive, 2)[1]
        self.assertEqual(snap.channels["speed_kph"], 0.0)
        self.assertIsNone(snap.channels["rpm"])

    def test_pause_is_not_a_failure(self):
        snap = play(pause_and_resume, 2)[1]
        self.assertEqual(snap.state, SourceState.PAUSED)
        self.assertEqual(snap.read_outcome, ReadOutcome.UNCHANGED)

    def test_failed_read_has_no_data(self):
        snap = play(startup_and_drive, 1)[0]
        self.assertEqual(snap.read_outcome, ReadOutcome.FAILED)
        self.assertIsNone(snap.frame_id)
        self.assertEqual(snap.channels, {})

    def test_end_of_script_reports_absent(self):
        snap = play(pause_and_resume, 4)[3]
        self.assertEqual(snap.state, SourceState.ABSENT)
        self.assertEqual(snap.read_outcome, ReadOutcome.FAILED)


class TestSnapshotsDoNotChange(unittest.TestCase):
    def test_held_snapshot_stays_the_same(self):
        p = FakeProvider(pause_and_resume)
        held = p.read()
        before = (held.frame_id, held.state, held.read_outcome, dict(held.channels))
        p.read()
        p.read()
        p.read()
        after = (held.frame_id, held.state, held.read_outcome, dict(held.channels))
        self.assertEqual(before, after)


class TestCleanup(unittest.TestCase):
    def test_close_twice_is_safe(self):
        p = FakeProvider(pause_and_resume)
        p.attach()
        p.close()
        p.close()
        self.assertTrue(p.closed)

    def test_close_without_attach_is_safe(self):
        p = FakeProvider(pause_and_resume)
        p.close()
        self.assertTrue(p.closed)


if __name__ == "__main__":
    unittest.main()