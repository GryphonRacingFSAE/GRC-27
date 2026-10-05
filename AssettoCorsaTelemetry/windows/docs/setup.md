# Windows telemetry setup notes

## Prerequisites
- Git, VS Code, Python 3.13 (64-bit)
- Installed version: Python 3.13.16 (64-bit, checked with struct.calcsize('P') * 8 = 64)

## Commands
Run from the repo root (GRC-27):
    git switch -c ac/windows-w01-setup

Run from AssettoCorsaTelemetry/windows:
    py -3.13 -m venv .venv
    .\.venv\Scripts\python.exe --version
    .\.venv\Scripts\python.exe -m ac_windows.first_reading
    .\.venv\Scripts\python.exe -m unittest discover -s tests -v

## Expected output
First program:
    waiting
    0.0 km/h
    36.0 km/h

Tests: 3 tests run, all OK.

## Problem I solved
VS Code's run button used Python 3.14 instead of the project's 3.13.
I now run everything with .\.venv\Scripts\python.exe so the version is always 3.13.