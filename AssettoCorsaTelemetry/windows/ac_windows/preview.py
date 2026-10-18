import argparse

from ac_windows.fake_provider import FakeProvider
from ac_windows.scenarios import SCENARIOS


# Accept only whole numbers that are 1 or more
def positive_int(text):
    value = int(text)
    if value < 1:
        raise argparse.ArgumentTypeError("steps must be 1 or more")
    return value


def main():
    parser = argparse.ArgumentParser(
        description="Play a FAKE telemetry scenario. Synthetic data, not from the game."
    )
    parser.add_argument("scenario", choices=sorted(SCENARIOS))
    parser.add_argument("steps", type=positive_int)
    args = parser.parse_args()

    print("SYNTHETIC PREVIEW: fake data, not from Assetto Corsa")
    provider = FakeProvider(SCENARIOS[args.scenario])
    provider.attach()
    try:
        for step in range(1, args.steps + 1):
            snap = provider.read()
            print(
                f"step {step}: state={snap.state.value} "
                f"outcome={snap.read_outcome.value} "
                f"frame={snap.frame_id} channels={snap.channels}"
            )
    finally:
        provider.close()


# Only runs when you start this file directly, not when it is imported
if __name__ == "__main__":
    main()