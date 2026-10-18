from ac_windows.provider import Provider


class FakeProvider(Provider):
    def __init__(self, script):
        self.script = script
        self.position = 0
        self.closed = False

    # Connect to the source. The fake has nothing real to connect to.
    def attach(self):
        self.closed = False

    # Return the next snapshot from the script and move the bookmark forward
    def read(self):
        item = self.script[self.position]
        self.position += 1
        return item

    # Start the story over from the beginning
    def reset(self):
        self.position = 0

    # Let go of anything held. Safe to call more than once.
    def close(self):
        self.closed = True