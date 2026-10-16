from ac_windows.provider import Provider

class FakeProvider(Provider):
    def __init__(self, script):
        self.script = script
        self.position = 0

    def read(self):
        item = self.script[self.position]
        self.position += 1
        return item