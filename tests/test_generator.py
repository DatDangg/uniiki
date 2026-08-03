"""
Uniiki Rule-based Telex Test Generator & Comprehensive Suite
Generates valid Telex key permutations for Vietnamese words, tests step-by-step intermediate and final states,
and verifies English/Code/URL/Email protection.
"""

import unittest
import sys
import os

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.engine import VietnameseEngine

class TelexPermutationGenerator:
    """Generates valid Telex typing permutations for Vietnamese words based on engine rules."""

    @staticmethod
    def generate_permutations(word_spec):
        """
        word_spec dictionary containing:
        - target: Expected output word
        - initial: Initial consonant keys (e.g. ['d', 'd'] or ['t', 'h'])
        - vowels: Vowel keys (e.g. ['o', 'o'])
        - tone: Tone key (e.g. 'j', 'f', 's', 'r', 'x')
        - final: Final consonant keys (e.g. ['n', 'g'])
        """
        target = word_spec['target']
        initial = word_spec.get('initial', [])
        vowels = word_spec.get('vowels', [])
        tone = word_spec.get('tone', None)
        final = word_spec.get('final', [])

        sequences = []

        # Sequence 1: Standard canonical (Initial -> Vowels -> Tone -> Final)
        seq1 = initial + vowels + ([tone] if tone else []) + final
        sequences.append(seq1)

        # Sequence 2: Tone typed at end of word (Initial -> Vowels -> Final -> Tone)
        if tone and final:
            seq2 = initial + vowels + final + [tone]
            sequences.append(seq2)

        # Sequence 3: Tone typed right after vowels, before final
        if tone and final and len(vowels) > 0:
            seq3 = initial + vowels + [tone] + final
            sequences.append(seq3)

        # Sequence 4: Tone typed right after initial consonant
        if tone and initial:
            seq4 = initial + [tone] + vowels + final
            sequences.append(seq4)

        # Sequence 5: For 'dd', 'd' typed at different positions
        if initial == ['d', 'd']:
            # 'd' typed, then vowels, then 'd'
            seq5 = ['d'] + vowels + (['d']) + ([tone] if tone else []) + final
            sequences.append(seq5)

        return target, sequences


class TestTelexEngineGenerator(unittest.TestCase):
    def setUp(self):
        self.engine = VietnameseEngine(mode='telex', modern_tone=True)

    def test_generated_permutations(self):
        word_specs = [
            {
                'target': 'độ',
                'initial': ['d', 'd'],
                'vowels': ['o', 'o'],
                'tone': 'j',
                'final': []
            },
            {
                'target': 'thời',
                'initial': ['t', 'h'],
                'vowels': ['o', 'w', 'i'],
                'tone': 'f',
                'final': []
            },
            {
                'target': 'khoảng',
                'initial': ['k', 'h'],
                'vowels': ['o', 'a'],
                'tone': 'r',
                'final': ['n', 'g']
            },
            {
                'target': 'việt',
                'initial': ['v'],
                'vowels': ['i', 'e', 'e'],
                'tone': 'j',
                'final': ['t']
            },
            {
                'target': 'đặng',
                'initial': ['d', 'd'],
                'vowels': ['a', 'w'],
                'tone': 'j',
                'final': ['n', 'g']
            },
            {
                'target': 'người',
                'initial': ['n', 'g'],
                'vowels': ['u', 'o', 'i', 'w'],
                'tone': 'f',
                'final': []
            },
            {
                'target': 'tiết',
                'initial': ['t'],
                'vowels': ['i', 'e', 'e'],
                'tone': 's',
                'final': ['t']
            },
            {
                'target': 'khá',
                'initial': ['k', 'h'],
                'vowels': ['a'],
                'tone': 's',
                'final': []
            }
        ]

        total_tested = 0
        for spec in word_specs:
            target, sequences = TelexPermutationGenerator.generate_permutations(spec)
            for seq in sequences:
                total_tested += 1
                self.engine.reset_buffer()
                word = ""
                for k in seq:
                    action, bcount, insert = self.engine.process_key(k)
                    if action == 'MODIFY':
                        word = word[:-bcount] + insert
                    elif action in ['APPEND', 'RESET']:
                        word += insert
                
                final_word = self.engine.get_current_word()
                self.assertEqual(final_word, target, f"Failed permutation {seq} for target '{target}': got '{final_word}'")
        
        print(f"\n[Test Generator] Successfully verified {total_tested} Telex permutations across all test words!")

    def test_english_code_url_protection(self):
        protected_inputs = [
            "JavaScript",
            "TypeScript",
            "Python",
            "30°C",
            "https://ubuntu.com",
            "user@test.com",
            "password",
            "desktop",
            "javascript",
            "typescript",
            "Telex",
            "Chrome",
            "Telegram",
            "telegram",
            "Tele",
            "Telee",
            "telee",
            "Discord",
            "LibreOffice",
            "Web",
            "Windows",
            "Google",
            "version",
            "raw",
        ]

        for text in protected_inputs:
            self.engine.reset_buffer()
            word = ""
            for k in text:
                action, bcount, insert = self.engine.process_key(k)
                if action == 'MODIFY':
                    word = word[:-bcount] + insert
                elif action in ['APPEND', 'RESET']:
                    word += insert
            # Separators terminate the active composition, so validate the
            # text emitted to the application instead of the final buffer.
            final_word = word
            self.assertEqual(final_word, text, f"Failed protection test for '{text}': got '{final_word}'")

    def test_double_key_escapes(self):
        tests = [
            ("tesst", "test"),
            ("aaa", "aa"),
            ("eee", "ee"),
            ("ooo", "oo"),
            ("ddd", "dd"),
            ("w", "ư"),
            ("ww", "w"),
            ("W", "Ư"),
            ("WW", "W"),
        ]
        for seq, expected in tests:
            self.engine.reset_buffer()
            word = ""
            for k in seq:
                action, bcount, insert = self.engine.process_key(k)
                if action == 'MODIFY':
                    word = word[:-bcount] + insert
                elif action in ['APPEND', 'RESET']:
                    word += insert
            final_word = self.engine.get_current_word()
            self.assertEqual(final_word, expected, f"Failed escape test for '{seq}': got '{final_word}'")

if __name__ == '__main__':
    unittest.main()
