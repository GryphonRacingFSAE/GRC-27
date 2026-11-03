import contextlib
import importlib
import io
import unittest


class TestImportIsSafe(unittest.TestCase):
    def test_import_prints_nothing(self):
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            import ac_windows.first_reading as module

            importlib.reload(module)
        self.assertEqual(buffer.getvalue(), "")