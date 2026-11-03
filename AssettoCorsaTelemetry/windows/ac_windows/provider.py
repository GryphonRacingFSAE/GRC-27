from dataclasses import dataclass
from enum import Enum
from types import MappingProxyType


# What is the game doing right now
class SourceState(Enum):
    ABSENT = "absent"
    MENU = "menu"
    LIVE = "live"
    PAUSED = "paused"
    REPLAY = "replay"


# What happened when I tried to read the game state
class ReadOutcome(Enum):
    NEW = "new"
    UNCHANGED = "unchanged"
    FAILED = "failed"


# Holds one reading of the game: state, outcome, frame and channels
@dataclass(frozen=True)
class Snapshot:
    frame_id: int | None
    state: SourceState
    read_outcome: ReadOutcome
    channels: dict

    # Runs right after creation: keep a read-only copy of the channels
    def __post_init__(self):
        object.__setattr__(self, "channels", MappingProxyType(dict(self.channels)))


# The shape every provider (fake or real) must follow
class Provider:
    # Connect to the source (the game's memory, or the fake script)
    def attach(self):
        raise NotImplementedError

    # Return one Snapshot promptly
    def read(self):
        raise NotImplementedError

    # Start fresh, like after the game restarts
    def reset(self):
        raise NotImplementedError

    # Release everything. Safe to call more than once.
    def close(self):
        raise NotImplementedError