# Windows companion setup

Tested with Python 3.13.16 (64-bit) on Windows. The game does not need to be running.

## First-time setup
From the folder where you keep projects:

    git clone https://github.com/GryphonRacingFSAE/GRC-27.git
    Set-Location GRC-27\AssettoCorsaTelemetry\windows

From AssettoCorsaTelemetry\windows:

    py -3.13 -m venv .venv
    .\.venv\Scripts\python.exe --version
    .\.venv\Scripts\python.exe -c "import struct; print(struct.calcsize('P') * 8)"
    .\.venv\Scripts\python.exe -m pip install -r requirements.txt

Expected: Python 3.13.16 and 64. The install step installs nothing for now, because there are no dependencies yet.

## Run the checks
From AssettoCorsaTelemetry\windows:

    .\.venv\Scripts\python.exe -m unittest discover -s tests -v

Expected: 4 tests, all OK. They check that a missing reading and a real zero stay different, and that importing a module starts nothing.

## Try the example program
From AssettoCorsaTelemetry\windows:

    .\.venv\Scripts\python.exe -m ac_windows.first_reading

Expected output:

    waiting
    0.0 km/h
    36.0 km/h

## Working on a ticket
From the repo root:

    git switch main
    git pull --ff-only
    git switch -c <your-ticket-branch>

Commit small changes, push, and open a draft PR that links the issue and lists the commands and results. Do not merge your own PR.

## Folder layout
- `ac_windows/` holds the code
- `tests/` holds the tests
- `docs/` holds these notes
- `contracts/`, `fixtures/` and `tools/` are shared folders next to `windows/`. Use them, do not copy them here.