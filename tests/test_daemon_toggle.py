import unittest
import sys
import os

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.daemon import UniikiDaemon

class TestDaemonLanguageToggle(unittest.TestCase):
    def setUp(self):
        self.daemon = UniikiDaemon(mode='telex')
        self.daemon.is_running = True

    def test_initial_language_is_vi(self):
        self.assertEqual(self.daemon.lang, 'VI')

    def test_set_language(self):
        self.daemon.set_language('EN')
        self.assertEqual(self.daemon.lang, 'EN')
        self.daemon.set_language('VI')
        self.assertEqual(self.daemon.lang, 'VI')

    def test_toggle_language(self):
        toggled = self.daemon.toggle_language()
        self.assertEqual(toggled, 'EN')
        self.assertEqual(self.daemon.lang, 'EN')

        toggled = self.daemon.toggle_language()
        self.assertEqual(toggled, 'VI')
        self.assertEqual(self.daemon.lang, 'VI')

    def test_language_callback(self):
        callback_calls = []

        def on_change(new_lang):
            callback_calls.append(new_lang)

        self.daemon.on_language_changed = on_change
        self.daemon.toggle_language()
        self.daemon.toggle_language()

        self.assertEqual(callback_calls, ['EN', 'VI'])

    def test_ctrl_shift_hotkey_simulation(self):
        # Simulate Pynput Ctrl+Shift press sequence
        try:
            from pynput.keyboard import Key
            
            # Press Ctrl
            self.daemon._on_press(Key.ctrl_l)
            self.assertTrue(self.daemon.ctrl_pressed)
            self.assertFalse(self.daemon.ctrl_shift_combo_active)

            # Press Shift while Ctrl is held
            self.daemon._on_press(Key.shift_l)
            self.assertTrue(self.daemon.shift_pressed)
            self.assertTrue(self.daemon.ctrl_shift_combo_active)
            self.assertFalse(self.daemon.ctrl_shift_interrupted)

            # Release Shift -> Should trigger toggle (VI -> EN)
            self.daemon._on_release(Key.shift_l)
            self.assertEqual(self.daemon.lang, 'EN')
            self.assertFalse(self.daemon.ctrl_shift_combo_active)

            # Release Ctrl
            self.daemon._on_release(Key.ctrl_l)
            self.assertEqual(self.daemon.lang, 'EN')

            # Toggle back using Shift -> Ctrl sequence
            self.daemon._on_press(Key.shift_r)
            self.daemon._on_press(Key.ctrl_r)
            self.daemon._on_release(Key.ctrl_r)
            self.assertEqual(self.daemon.lang, 'VI')
            self.daemon._on_release(Key.shift_r)

        except ImportError:
            self.skipTest("pynput not installed")

    def test_ctrl_shift_shortcut_interrupted(self):
        # Simulate Ctrl+Shift+C (should NOT toggle language)
        try:
            from pynput.keyboard import Key, KeyCode
            
            initial_lang = self.daemon.lang

            self.daemon._on_press(Key.ctrl_l)
            self.daemon._on_press(Key.shift_l)
            # Intervening key 'c'
            self.daemon._on_press(KeyCode.from_char('c'))
            self.assertTrue(self.daemon.ctrl_shift_interrupted)

            self.daemon._on_release(KeyCode.from_char('c'))
            self.daemon._on_release(Key.shift_l)
            self.daemon._on_release(Key.ctrl_l)

            # Language should remain unchanged
            self.assertEqual(self.daemon.lang, initial_lang)

        except ImportError:
            self.skipTest("pynput not installed")

if __name__ == '__main__':
    unittest.main()
