# Provider interface

A provider supplies readings of the game's state: the fake provider now, and the real Assetto Corsa reader later. Both follow the same interface, so code that uses a provider (like the sampler) does not need to know which one it has.

Code: `ac_windows/provider.py` (interface), `ac_windows/fake_provider.py` (fake), `ac_windows/scenarios.py` (scripted stories).

## Functions

| Function | What it does |
|---|---|
| `attach()` | Connect to the source. The fake has nothing real to connect to. |
| `read()` | Return one `Snapshot` promptly. |
| `reset()` | Start over, so old values are never reused as fresh data. |
| `close()` | Release anything held. Safe to call more than once. |

## What `read()` returns

A `Snapshot`, which cannot be changed after it is made.

| Field | Meaning |
|---|---|
| `frame_id` | The source's frame number, or `None` if there is none |
| `state` | What the game is doing: `absent`, `menu`, `live`, `paused` or `replay` |
| `read_outcome` | What happened on this read: `new`, `unchanged` or `failed` |
| `channels` | Measurements by name, for example `{"speed_kph": 36.0, "rpm": None}` |

## State and outcome are separate

`state` describes the game and `read_outcome` describes the read attempt. A paused game is not a broken reader: it reports `paused` with `unchanged`. A live game can still have a `failed` read. Unchanged values can still come from a new frame, and a repeated frame is not a new measurement.

## Zero and unavailable

`0.0` is a real measurement, such as a stationary car. `None` means the value is unavailable. They are never interchangeable.

## Rules

- Snapshots are frozen. A snapshot you hold does not change when the provider reads again.
- `read()` returns promptly and never waits on the network.
- `close()` is safe to call repeatedly, and safe to call before `attach()`.
- When a script ends, the fake reports `absent` with `failed`, no frame and no channels. It never repeats an old reading as new.
- The channel names here are placeholders until the #16 contract fixes the final names and units.

## Scenarios

`startup_and_drive`, `pause_and_resume`, `menu_then_drive`, `repeated_frame`, `missing_channel`, `source_loss`, `game_exit`.

## Run it

From `AssettoCorsaTelemetry\windows`:

    .\.venv\Scripts\python.exe -m ac_windows.preview pause_and_resume 5
    .\.venv\Scripts\python.exe -m unittest discover -s tests -v