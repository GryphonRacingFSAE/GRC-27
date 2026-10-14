from enum import Enum
from dataclasses import dataclass


#What is the game doing right now
class SourceState(Enum):
    ABSENT = "absent"
    MENU = "menu"
    LIVE = "live"
    PAUSED = "paused"
    REPLAY = "replay"

#What happened when I tried to read the game state
class ReadOutcome(Enum):
    NEW = "new"
    UNCHANGED = "unchanged"
    FAILED = "failed"

@dataclass(frozen=True)

#Holds the current game state
class Snapshot:
    frame_id: int | None
    state: SourceState
    read_outcome: ReadOutcome
    channels: dict

class Provider:

    #Attach to the game process and read the telemtry data
    def attach(self):
        raise NotImplementedError
    
    #Return a snapshot of the current game state
    def read(self):
        raise NotImplementedError
    #Reset the game state to a known state
    def reset(self):
        raise NotImplementedError
    #Close the connection to the game process
    def close(self):
        raise NotImplementedError