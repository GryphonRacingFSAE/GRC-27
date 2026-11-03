import unittest

from ac_windows.first_reading import describe_speed


class TestDescribeSpeed(unittest.TestCase):
    def test_missing_data(self):
        #checks if both none and 0.0 are equal
        self.assertEqual(describe_speed(None), "waiting")

    def test_valid_zero(self):
        self.assertEqual(describe_speed(0.0), "0.0 km/h")

    def test_positive_value(self):
        self.assertEqual(describe_speed(18.5), "18.5 km/h")


if __name__ == "__main__":
    unittest.main()