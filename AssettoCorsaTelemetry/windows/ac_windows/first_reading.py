def describe_speed(speed):
    if speed is None:
        return "waiting"
    return f"{speed:.1f} km/h"

if __name__ == "__main__":
    readings = [None, 0.0, 36.0]
    for reading in readings:
        print(describe_speed(reading))