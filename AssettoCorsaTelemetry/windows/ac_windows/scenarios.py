from ac_windows.provider import Snapshot, SourceState, ReadOutcome

# Game not running, then driving stopped, then moving
startup_and_drive = [
    Snapshot(
        frame_id=None,
        state=SourceState.ABSENT,
        read_outcome=ReadOutcome.FAILED,
        channels={},
    ),
    Snapshot(
        frame_id=1,
        state=SourceState.LIVE,
        read_outcome=ReadOutcome.NEW,
        channels={"speed_kph": 0.0, "rpm": None},
    ),
    Snapshot(
        frame_id=2,
        state=SourceState.LIVE,
        read_outcome=ReadOutcome.NEW,
        channels={"speed_kph": 36.0, "rpm": None},
    ),
]

# Driving, then paused (nothing new arrives), then driving again
pause_and_resume = [
    Snapshot(
        frame_id=1,
        state=SourceState.LIVE,
        read_outcome=ReadOutcome.NEW,
        channels={"speed_kph": 20.0, "rpm": None},
    ),
    Snapshot(
        frame_id=1,
        state=SourceState.PAUSED,
        read_outcome=ReadOutcome.UNCHANGED,
        channels={"speed_kph": 20.0, "rpm": None},
    ),
    Snapshot(
        frame_id=2,
        state=SourceState.LIVE,
        read_outcome=ReadOutcome.NEW,
        channels={"speed_kph": 25.0, "rpm": None},
    ),
]

# Game open on a menu, then driving starts
menu_then_drive = [
    Snapshot(
        frame_id=None,
        state=SourceState.MENU,
        read_outcome=ReadOutcome.UNCHANGED,
        channels={},
    ),
    Snapshot(
        frame_id=1,
        state=SourceState.LIVE,
        read_outcome=ReadOutcome.NEW,
        channels={"speed_kph": 0.0, "rpm": None},
    ),
]

# Same frame read twice, then a new frame that happens to have the same speed
repeated_frame = [
    Snapshot(
        frame_id=1,
        state=SourceState.LIVE,
        read_outcome=ReadOutcome.NEW,
        channels={"speed_kph": 10.0, "rpm": None},
    ),
    Snapshot(
        frame_id=1,
        state=SourceState.LIVE,
        read_outcome=ReadOutcome.UNCHANGED,
        channels={"speed_kph": 10.0, "rpm": None},
    ),
    Snapshot(
        frame_id=2,
        state=SourceState.LIVE,
        read_outcome=ReadOutcome.NEW,
        channels={"speed_kph": 10.0, "rpm": None},
    ),
]

# A channel is available at first, then goes missing (None), then comes back
missing_channel = [
    Snapshot(
        frame_id=1,
        state=SourceState.LIVE,
        read_outcome=ReadOutcome.NEW,
        channels={"speed_kph": 30.0, "rpm": 3000.0},
    ),
    Snapshot(
        frame_id=2,
        state=SourceState.LIVE,
        read_outcome=ReadOutcome.NEW,
        channels={"speed_kph": 31.0, "rpm": None},
    ),
    Snapshot(
        frame_id=3,
        state=SourceState.LIVE,
        read_outcome=ReadOutcome.NEW,
        channels={"speed_kph": 32.0, "rpm": 3100.0},
    ),
]

# Data disappears for a moment, then recovers
source_loss = [
    Snapshot(
        frame_id=1,
        state=SourceState.LIVE,
        read_outcome=ReadOutcome.NEW,
        channels={"speed_kph": 40.0, "rpm": None},
    ),
    Snapshot(
        frame_id=None,
        state=SourceState.ABSENT,
        read_outcome=ReadOutcome.FAILED,
        channels={},
    ),
    Snapshot(
        frame_id=2,
        state=SourceState.LIVE,
        read_outcome=ReadOutcome.NEW,
        channels={"speed_kph": 42.0, "rpm": None},
    ),
]

# Driving, then the game closes and stays gone
game_exit = [
    Snapshot(
        frame_id=5,
        state=SourceState.LIVE,
        read_outcome=ReadOutcome.NEW,
        channels={"speed_kph": 50.0, "rpm": None},
    ),
    Snapshot(
        frame_id=None,
        state=SourceState.ABSENT,
        read_outcome=ReadOutcome.FAILED,
        channels={},
    ),
    Snapshot(
        frame_id=None,
        state=SourceState.ABSENT,
        read_outcome=ReadOutcome.FAILED,
        channels={},
    ),
]

# Lookup by name, so the preview command can pick a scenario
SCENARIOS = {
    "startup_and_drive": startup_and_drive,
    "pause_and_resume": pause_and_resume,
    "menu_then_drive": menu_then_drive,
    "repeated_frame": repeated_frame,
    "missing_channel": missing_channel,
    "source_loss": source_loss,
    "game_exit": game_exit,
}