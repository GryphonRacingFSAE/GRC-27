def describe_speed(speed):
    #None means no reading is available yet
    if speed is None:
        return "waiting"
    return f"{speed:.1f} km/h"

if __name__ == "__main__":
    #Only runs when this file is executed directly, not when imported
    readings = [None, 0.0, 36.0]
    for reading in readings:
        print(describe_speed(reading))