"""
Uniiki Vietnamese Engine Core
Exact Minimal Diff Prefix Backspacing Math.
Formula: backspace_count = len(current_word) - prefix_len
Restores perfect full word rendering (chào, tôi, kiểm, bộ, tiếng, việt, trên).
"""

import unicodedata

TONES = {
    'NONE': 0,
    'SAC': 1,
    'HUYEN': 2,
    'HOI': 3,
    'NGA': 4,
    'NANG': 5
}

VOWEL_MATRIX = {
    ('a', 0): ['a', 'á', 'à', 'ả', 'ã', 'ạ'],
    ('a', 1): ['â', 'ấ', 'ầ', 'ẩ', 'ẫ', 'ậ'],
    ('a', 2): ['ă', 'ắ', 'ằ', 'ẳ', 'ẵ', 'ặ'],
    ('e', 0): ['e', 'é', 'è', 'ẻ', 'ẽ', 'ẹ'],
    ('e', 1): ['ê', 'ế', 'ề', 'ể', 'ễ', 'ệ'],
    ('i', 0): ['i', 'í', 'ì', 'ỉ', 'ĩ', 'ị'],
    ('o', 0): ['o', 'ó', 'ò', 'ỏ', 'õ', 'ọ'],
    ('o', 1): ['ô', 'ố', 'ồ', 'ổ', 'ỗ', 'ộ'],
    ('o', 3): ['ơ', 'ớ', 'ờ', 'ở', 'ỡ', 'ợ'],
    ('u', 0): ['u', 'ú', 'ù', 'ủ', 'ũ', 'ụ'],
    ('u', 3): ['ư', 'ứ', 'ừ', 'ử', 'ữ', 'ự'],
    ('y', 0): ['y', 'ý', 'ỳ', 'ỷ', 'ỹ', 'ỵ'],
}

CHAR_DECOMPOSE = {}
for (base_vowel, hat), char_list in VOWEL_MATRIX.items():
    for tone_idx, ch in enumerate(char_list):
        CHAR_DECOMPOSE[ch] = (base_vowel, hat, tone_idx)
        CHAR_DECOMPOSE[ch.upper()] = (base_vowel.upper(), hat, tone_idx)

CHAR_DECOMPOSE['đ'] = ('d', 'stroke', 0)
CHAR_DECOMPOSE['Đ'] = ('D', 'stroke', 0)

TELEX_TONE_KEYS = {
    's': TONES['SAC'], 'S': TONES['SAC'],
    'f': TONES['HUYEN'], 'F': TONES['HUYEN'],
    'r': TONES['HOI'], 'R': TONES['HOI'],
    'x': TONES['NGA'], 'X': TONES['NGA'],
    'j': TONES['NANG'], 'J': TONES['NANG'],
}

CONSONANT_CLUSTERS = {'scr', 'str', 'spl', 'spr', 'sch', 'thr', 'phr', 'chr'}
VN_INITIAL_CLUSTERS = {
    'ch', 'gh', 'gi', 'kh', 'ng', 'nh', 'ph', 'qu', 'th', 'tr',
    'ngh'
}
VN_VOWELS = 'aeiouyăâêôơưAEIOUYĂÂÊÔƠƯ'
VN_VALID_INITIALS = {
    '', 'b', 'c', 'ch', 'd', 'g', 'gh', 'gi', 'h', 'k', 'kh', 'l', 'm',
    'n', 'ng', 'ngh', 'nh', 'p', 'ph', 'q', 'qu', 'r', 's', 't', 'th',
    'tr', 'v', 'x'
}
VN_VALID_CODAS = {'', 'c', 'ch', 'm', 'n', 'ng', 'nh', 'p', 't'}
VN_VALID_NUCLEI = {
    'a0', 'a1', 'a2', 'e0', 'e1', 'i0', 'o0', 'o1', 'o3', 'u0', 'u3', 'y0',
    'a0i0', 'a0o0', 'a0u0', 'a0y0', 'a1u0', 'a1y0', 'e0o0', 'e1u0',
    'i0a0', 'i0e1', 'i0e1u0', 'i0u0', 'o0a0', 'o0a0i0', 'o0a0o0',
    'o0a0y0', 'o0a2', 'o0e0', 'o0e0o0', 'o0i0', 'o1i0', 'o3i0',
    'u0a0', 'u0o1', 'u0o1i0', 'u0i0', 'u0u0', 'u0y0', 'u0y0e1',
    'u0y0e1u0', 'u3a0', 'u3i0', 'u3o3', 'u3o3i0', 'u3u0',
    'y0e1', 'y0e1u0',
}
VN_NUCLEUS_BASES = {
    'a', 'e', 'i', 'o', 'u', 'y', 'ai', 'ao', 'au', 'ay', 'eo', 'eu',
    'ia', 'ie', 'ieu', 'iu', 'oa', 'oai', 'oao', 'oay', 'oe', 'oeo',
    'oi', 'ua', 'uo', 'uoi', 'ui', 'uu', 'uy', 'uye', 'uyeu', 'ye', 'yeu',
}

class VietnameseEngine:
    def __init__(self, mode='telex', modern_tone=True):
        self.mode = mode.lower()
        self.modern_tone = modern_tone
        self.reset_buffer()

    def reset_buffer(self):
        self.raw_keys = []
        self.history_stack = []

    def set_mode(self, mode):
        self.mode = mode.lower()

    def process_key(self, char):
        if not char.isalnum() and char not in ['_']:
            # Punctuation ends the active Vietnamese word. Keeping it in the
            # raw buffer makes a later reevaluation drop separators such as
            # '/', '.', '@' and ':' and can also corrupt the next word.
            self.reset_buffer()
            return ('APPEND', 0, char)

        if self.mode == 'telex':
            return self._process_telex(char)
        else:
            self.raw_keys.append(char)
            current_word = "".join(self.raw_keys)
            self.history_stack.append((list(self.raw_keys), current_word))
            return ('APPEND', 0, char)

    def process_backspace(self):
        if not self.history_stack or not self.raw_keys:
            self.reset_buffer()
            return ('FORWARD', 0, '')

        # Pop current state snapshot
        _, current_word = self.history_stack.pop()
        popped_key = self.raw_keys.pop()

        if self.history_stack:
            prev_raw_keys, prev_word = self.history_stack[-1]
            if current_word.lower().endswith(popped_key.lower()):
                new_word = current_word[:-len(popped_key)]
                self.history_stack[-1] = (list(self.raw_keys), new_word)
            else:
                new_word = prev_word
        else:
            new_word = ''

        if new_word != current_word:
            prefix_len = 0
            min_len = min(len(current_word), len(new_word))
            while prefix_len < min_len and current_word[prefix_len] == new_word[prefix_len]:
                prefix_len += 1

            backspace_count = len(current_word) - prefix_len
            insert_str = new_word[prefix_len:]
            return ('MODIFY', backspace_count, insert_str)
        else:
            return ('MODIFY', 0, '')

    def _evaluate_raw_escaped(self, keys):
        if not keys:
            return ""

        escaped_set = {'d', 'w', 'a', 'e', 'o', 's', 'f', 'r', 'x', 'j', 'z'}
        res = []
        i = 0
        n = len(keys)
        while i < n:
            k = keys[i]
            lk = k.lower()
            if i + 1 < n and lk in escaped_set and keys[i + 1].lower() == lk:
                res.append(k)
                i += 2
            else:
                res.append(k)
                i += 1
        return "".join(res)

    def _is_valid_initial_cluster(self, chars):
        if not chars:
            return True
        vowel_idx = None
        for idx, ch in enumerate(chars):
            if ch in VN_VOWELS or ch in CHAR_DECOMPOSE:
                base, hat, tone = CHAR_DECOMPOSE.get(ch, (ch, 0, 0))
                if hat != 'stroke':
                    vowel_idx = idx
                    break
            elif ch.lower() == 'w':
                prefix = "".join([c.lower() for c in chars[:idx]])
                collapsed = ""
                for k, c in enumerate(prefix):
                    if k + 1 < len(prefix) and c == prefix[k+1]:
                        continue
                    collapsed += c
                if collapsed and collapsed in VN_VALID_INITIALS:
                    vowel_idx = idx
                    break

        if vowel_idx is None:
            initial = "".join(chars)
        else:
            initial = "".join(chars[:vowel_idx])

        normalized = []
        for c in initial:
            if c in ['đ', 'Đ']:
                normalized.append('d')
            else:
                normalized.append(c.lower())
        initial_str = "".join(normalized)
        return initial_str in VN_VALID_INITIALS

    def _transform_vowel_hook(self, char):
        hook_map = {
            'u': 'ư', 'U': 'Ư',
            'ú': 'ứ', 'Ú': 'Ứ',
            'ù': 'ừ', 'Ù': 'Ừ',
            'ủ': 'ử', 'Ủ': 'Ử',
            'ũ': 'ữ', 'Ũ': 'Ữ',
            'ụ': 'ự', 'Ụ': 'Ự',
            'o': 'ơ', 'O': 'Ơ',
            'ó': 'ớ', 'Ó': 'Ớ',
            'ò': 'ờ', 'Ò': 'Ờ',
            'ỏ': 'ở', 'Ỏ': 'Ở',
            'õ': 'ỡ', 'Õ': 'Ỡ',
            'ọ': 'ợ', 'Ọ': 'Ợ',
            'a': 'ă', 'A': 'Ă',
            'á': 'ắ', 'Á': 'Ắ',
            'à': 'ằ', 'À': 'Ằ',
            'ả': 'ẳ', 'Ả': 'Ẳ',
            'ã': 'ẵ', 'Ã': 'Ẵ',
            'ạ': 'ặ', 'Ạ': 'Ặ',
        }
        return hook_map.get(char, char)

    def _process_telex(self, char):
        current_word = self.get_current_word()
        
        test_keys = self.raw_keys + [char]
        new_word = self._evaluate_telex_sequence(test_keys)

        if new_word != current_word + char:
            prefix_len = 0
            min_len = min(len(current_word), len(new_word))
            while prefix_len < min_len and current_word[prefix_len] == new_word[prefix_len]:
                prefix_len += 1

            backspace_count = len(current_word) - prefix_len
            insert_str = new_word[prefix_len:]
            self.raw_keys.append(char)
            self.history_stack.append((list(self.raw_keys), new_word))
            return ('MODIFY', backspace_count, insert_str)
        else:
            self.raw_keys.append(char)
            self.history_stack.append((list(self.raw_keys), new_word))
            return ('APPEND', 0, char)

    def get_current_word(self):
        return self._evaluate_telex_sequence(self.raw_keys)

    def _split_raw_segments(self, keys):
        if not keys:
            return []

        valid_codas = ('c', 'ch', 'm', 'n', 'ng', 'nh', 'p', 't')
        segments = []
        segment_start = 0
        has_vowel = False
        coda = ''

        def is_coda_prefix(value):
            return any(candidate.startswith(value) for candidate in valid_codas)

        for index, key in enumerate(keys):
            lower = key.lower()
            if lower == 'w':
                if not has_vowel:
                    has_vowel = True
                continue
            if lower in TELEX_TONE_KEYS or lower == 'z':
                if has_vowel:
                    continue
            if lower in 'aeiouy':
                if has_vowel and coda:
                    segments.append((segment_start, index))
                    segment_start = index
                has_vowel = True
                coda = ''
                continue
            if not lower.isalpha():
                if index > segment_start:
                    segments.append((segment_start, index))
                segment_start = index + 1
                has_vowel = False
                coda = ''
                continue
            if not has_vowel:
                continue

            next_coda = coda + lower
            if not is_coda_prefix(next_coda):
                segments.append((segment_start, index))
                segment_start = index
                has_vowel = False
                coda = ''
                continue
            coda = next_coda

        if segment_start < len(keys):
            segments.append((segment_start, len(keys)))
        return segments

    def _evaluate_telex_sequence(self, keys):
        if not keys:
            return ""

        raw_str = "".join(keys)
        raw_lower = raw_str.lower()
        protected_words = {
            'python', 'password', 'desktop', 'windows', 'google', 'version',
            'linux', 'raw', 'javascript', 'typescript', 'telex', 'vni',
            'terminal', 'code', 'chrome', 'firefox', 'libreoffice',
            'telegram', 'discord', 'zalo', 'web', 'latinh', 'pre', 'test',
            'best'
        }
        if raw_lower in protected_words:
            return raw_str
        protected_roots = {
            'tele', 'type', 'java', 'chrome', 'fire', 'libre', 'discord',
            'zalo', 'web', 'latin', 'terminal', 'pre'
        }
        if raw_str.isascii() and any(
            raw_lower.startswith(root) for root in protected_roots
        ):
            return raw_str

        # Telex also permits the second d to be typed after the vowel cluster
        # (doodj -> độ, dawjdng -> đặng). Only treat it as the
        # onset modifier when removing it produces an actual marked Vietnamese
        # syllable; this keeps ordinary text such as "dod" literal.
        if raw_lower.startswith('d') and not raw_lower.startswith('dd'):
            vowel_seen = False
            for index in range(1, len(keys)):
                lower = keys[index].lower()
                if lower in 'aeiouy':
                    vowel_seen = True
                    continue
                if lower != 'd' or not vowel_seen:
                    continue
                if any(key.lower() in 'aeiouy' for key in keys[index + 1:]):
                    continue

                without_modifier = keys[:index] + keys[index + 1:]
                candidate = self._evaluate_telex_sequence(without_modifier)
                if (
                    candidate
                    and candidate[0].lower() == 'd'
                    and self._has_vietnamese_mark(candidate)
                    and self._is_valid_vietnamese_syllable(candidate)
                ):
                    stroke = 'Đ' if candidate[0].isupper() else 'đ'
                    return stroke + candidate[1:]

        segments = self._split_raw_segments(keys)
        if len(segments) <= 1:
            return self._evaluate_telex_segment(keys)

        rendered = []
        previous_segment_transformed = False
        for segment_index, (start, end) in enumerate(segments):
            segment_keys = keys[start:end]
            leading_d = 0
            while (
                leading_d < len(segment_keys)
                and segment_keys[leading_d].lower() == 'd'
            ):
                leading_d += 1

            if (
                segment_index > 0
                and not previous_segment_transformed
                and leading_d >= 2
            ):
                segment_rendered = ''.join(segment_keys[1:leading_d])
                segment_rendered += self._evaluate_telex_segment(
                    segment_keys[leading_d:]
                )
            else:
                segment_rendered = self._evaluate_telex_segment(segment_keys)

            rendered.append(segment_rendered)
            previous_segment_transformed = any(
                ord(char) >= 128 for char in segment_rendered
            )
        return ''.join(rendered)

    def _evaluate_telex_segment(self, keys):
        if not keys:
            return ""

        raw_str = "".join(keys)

        protected_words = {
            'python', 'password', 'desktop', 'windows', 'google', 'version',
            'linux', 'raw',
            'javascript', 'typescript', 'telex', 'vni',
            'terminal', 'code', 'chrome', 'firefox', 'libreoffice',
            'telegram', 'discord', 'zalo', 'web', 'latinh', 'pre', 'test',
            'best'
        }
        protected_roots = {
            'tele', 'type', 'java', 'chrome', 'fire', 'libre',
            'discord', 'zalo', 'web', 'latin', 'terminal', 'pre'
        }
        raw_lower = raw_str.lower()
        if raw_lower in protected_words:
            return raw_str
        if raw_str.lower().startswith('w'):
            pass
        elif raw_str.isascii() and any(
            raw_lower.startswith(root) for root in protected_roots
        ):
            return raw_str

        # Protection for URLs, emails, code, numbers
        if any(c in raw_str for c in ['.', '/', '@', ':', '-']) or any(c.isdigit() for c in raw_str):
            return raw_str

        # CamelCase protection
        seen_lower = False
        has_upper_after_lower = False
        for ch in raw_str:
            if ch.islower():
                seen_lower = True
            elif seen_lower and ch.isupper() and ch.lower() != 'w':
                has_upper_after_lower = True
                break
        if has_upper_after_lower:
            return raw_str

        res_chars = []
        active_tone = TONES['NONE']
        active_tone_action = None
        w_modifier = None
        w_escaped = False

        i = 0
        n = len(keys)
        
        while i < n:
            k = keys[i]
            lk = k.lower()

            if w_modifier is not None and lk in 'aeiouy':
                w_modifier = None

            # 1. 'dd' -> 'đ', 'ddd' -> 'dd' escape
            if lk == 'd':
                if i == 0:
                    run_end = i
                    while run_end < n and keys[run_end].lower() == 'd':
                        run_end += 1
                    run_length = run_end - i
                    if run_length % 2 == 0:
                        res_chars.append('Đ' if keys[0].isupper() else 'đ')
                    else:
                        res_chars.append(keys[0])
                    for literal in range((run_length - 1) // 2):
                        res_chars.append(keys[run_length - 1 - literal])
                    i = run_end
                    continue
                res_chars.append(k)
                i += 1
                continue

            # 2. Consecutive double vowels: aa -> â, ee -> ê, oo -> ô
            if lk in ['a', 'e', 'o']:
                if i + 1 < n and keys[i+1].lower() == lk:
                    if lk == 'o' and res_chars and res_chars[-1] in ['ư', 'Ư']:
                        res_chars.append('Ơ' if k.isupper() else 'ơ')
                        i += 2
                        continue
                    hat_map = {'a': 'â', 'e': 'ê', 'o': 'ô'}
                    hat_c = hat_map[lk].upper() if k.isupper() else hat_map[lk]
                    res_chars.append(hat_c)
                    i += 2
                    continue
                elif res_chars and res_chars[-1] in ['â', 'Â', 'ê', 'Ê', 'ô', 'Ô']:
                    last_c = res_chars[-1]
                    hat_to_base = {'â': 'a', 'Â': 'A', 'ê': 'e', 'Ê': 'E', 'ô': 'o', 'Ô': 'O'}
                    if hat_to_base.get(last_c, '').lower() == lk:
                        res_chars[-1] = hat_to_base[last_c]
                        res_chars.append(k)
                        i += 1
                        continue

                late_mark_idx = self._find_late_mark_target(res_chars, lk)
                if late_mark_idx is not None:
                    mark_map = {
                        'a': {'a': 'â', 'ă': 'â', 'á': 'ấ', 'à': 'ầ', 'ả': 'ẩ', 'ã': 'ẫ', 'ạ': 'ậ',
                              'A': 'Â', 'Ă': 'Â', 'Á': 'Ấ', 'À': 'Ầ', 'Ả': 'Ẩ', 'Ã': 'Ẫ', 'Ạ': 'Ậ'},
                        'e': {'e': 'ê', 'é': 'ế', 'è': 'ề', 'ẻ': 'ể', 'ẽ': 'ễ', 'ẹ': 'ệ',
                              'E': 'Ê', 'É': 'Ế', 'È': 'Ề', 'Ẻ': 'Ể', 'Ẽ': 'Ễ', 'Ẹ': 'Ệ'},
                        'o': {'o': 'ô', 'ơ': 'ô', 'ó': 'ố', 'ò': 'ồ', 'ỏ': 'ổ', 'õ': 'ỗ', 'ọ': 'ộ',
                              'O': 'Ô', 'Ơ': 'Ô', 'Ó': 'Ố', 'Ò': 'Ồ', 'Ỏ': 'Ổ', 'Õ': 'Ỗ', 'Ọ': 'Ộ'},
                    }
                    res_chars[late_mark_idx] = mark_map[lk][res_chars[late_mark_idx]]
                    i += 1
                    continue

            # 3. 'w' hooks: aw -> ă, ow -> ơ, uw -> ư, uow -> ươ, uocw -> ước, etc.
            if lk == 'w':
                if w_modifier is not None:
                    if w_modifier['generated']:
                        del res_chars[w_modifier['changes'][0][0]]
                    else:
                        for idx, previous_char in w_modifier['changes']:
                            res_chars[idx] = previous_char
                    apply_nang = (
                        active_tone == TONES['NONE']
                        and self._should_apply_nang_on_w_escape(res_chars)
                    )
                    res_chars.append('w' if apply_nang else w_modifier['key'])
                    if apply_nang:
                        active_tone = TONES['NANG']
                    w_modifier = None
                    w_escaped = True
                    i += 1
                    continue

                # Check if res_chars contains 'uo' sequence (with optional coda/tones)
                u_idx = None
                o_idx = None
                for idx in range(len(res_chars) - 1, -1, -1):
                    ch = res_chars[idx]
                    ch_base, _hat, _tone = CHAR_DECOMPOSE.get(ch, (ch, 0, 0))
                    if ch_base.lower() in ['o', 'ơ']:
                        o_idx = idx
                    elif ch_base.lower() in ['u', 'ư'] and o_idx is not None:
                        # Ensure u and o are adjacent or separated only by non-vowel modifiers
                        if idx == o_idx - 1 or all(res_chars[k] not in VN_VOWELS and res_chars[k] not in CHAR_DECOMPOSE for k in range(idx + 1, o_idx)):
                            u_idx = idx
                            break
                        else:
                            o_idx = None

                if u_idx is not None and o_idx is not None:
                    u_char = res_chars[u_idx]
                    o_char = res_chars[o_idx]
                    changes = [(u_idx, u_char), (o_idx, o_char)]

                    res_chars[u_idx] = self._transform_vowel_hook(u_char)
                    res_chars[o_idx] = self._transform_vowel_hook(o_char)
                    w_modifier = {'key': k, 'changes': changes, 'generated': False}
                    i += 1
                    continue

                word_so_far = "".join(res_chars)
                if word_so_far.lower().endswith('ua'):
                    changes = [(len(res_chars) - 2, res_chars[-2]),
                               (len(res_chars) - 1, res_chars[-1])]
                    res_chars[-2] = 'Ư' if res_chars[-2].isupper() else 'ư'
                    w_modifier = {'key': k, 'changes': changes, 'generated': False}
                    i += 1
                    continue
                elif word_so_far.lower().endswith('uoi'):
                    changes = [(len(res_chars) - 3, res_chars[-3]),
                               (len(res_chars) - 2, res_chars[-2])]
                    res_chars[-3] = 'Ư' if res_chars[-3].isupper() else 'ư'
                    res_chars[-2] = 'Ơ' if res_chars[-2].isupper() else 'ơ'
                    w_modifier = {'key': k, 'changes': changes, 'generated': False}
                    i += 1
                    continue
                
                hook_applied = False
                v_hook_map = {
                    'a': 'ă', 'A': 'Ă', 'â': 'ă', 'Â': 'Ă',
                    'o': 'ơ', 'O': 'Ơ', 'ô': 'ơ', 'Ô': 'Ơ',
                    'u': 'ư', 'U': 'Ư'
                }
                for idx in range(len(res_chars) - 1, -1, -1):
                    if res_chars[idx] in v_hook_map:
                        previous_char = res_chars[idx]
                        res_chars[idx] = v_hook_map[res_chars[idx]]
                        w_modifier = {
                            'key': k,
                            'changes': [(idx, previous_char)],
                            'generated': False,
                        }
                        hook_applied = True
                        break
                if hook_applied:
                    i += 1
                    continue
                elif not any(c in VN_VOWELS for c in "".join(res_chars)):
                    generated_idx = len(res_chars)
                    res_chars.append('Ư' if k.isupper() else 'ư')
                    w_modifier = {
                        'key': k,
                        'changes': [(generated_idx, None)],
                        'generated': True,
                    }
                    i += 1
                    continue
                if i + 1 < n and keys[i + 1].lower() == 'w':
                    res_chars.append(k)
                    w_escaped = True
                    i += 2
                    continue

            # 4. Check Telex Tones (s, f, r, x, j, z)
            has_vowel_already = any(c in VN_VOWELS for c in "".join(res_chars))

            has_future_vowel = any(c in 'aeiouyAEIOUY' for c in keys[i+1:])

            if (lk in TELEX_TONE_KEYS or lk == 'z') and not has_vowel_already and i + 1 < n and keys[i + 1].lower() == lk:
                res_chars.append(k)
                w_escaped = True
                i += 2
                continue

            if lk == 'z':
                if active_tone != TONES['NONE']:
                    active_tone = TONES['NONE']
                    active_tone_action = None
                else:
                    res_chars.append(k)
                i += 1
                continue

            if lk in TELEX_TONE_KEYS and (has_vowel_already or (res_chars and has_future_vowel)) and self._is_valid_initial_cluster(res_chars):
                tail = "".join(res_chars).lower() + lk
                if not has_vowel_already and any(cluster.startswith(tail) for cluster in VN_INITIAL_CLUSTERS):
                    res_chars.append(k)
                    i += 1
                    continue
                if has_vowel_already and any(tail.endswith(cl) for cl in CONSONANT_CLUSTERS):
                    res_chars.append(k)
                    i += 1
                    continue

                if (
                    active_tone_action is not None
                    and active_tone_action['trigger'] == lk
                ):
                    active_tone = TONES['NONE']
                    has_future_literal_consonant = False
                    for future_key in keys[i + 1:]:
                        future = future_key.lower()
                        if future in 'aeiouy':
                            break
                        if (
                            future.isalpha()
                            and future != 'w'
                            and future not in TELEX_TONE_KEYS
                            and future != 'z'
                        ):
                            has_future_literal_consonant = True
                            break
                    if has_future_literal_consonant:
                        res_chars.append(k)
                    active_tone_action = None
                else:
                    active_tone = (
                        TONES['NONE'] if lk == 'z' else TELEX_TONE_KEYS[lk]
                    )
                    active_tone_action = {
                        'type': 'tone',
                        'raw_start': i,
                        'raw_end': i + 1,
                        'source': ''.join(res_chars),
                        'result': '',
                        'trigger': lk,
                    }
                i += 1
                continue

            res_chars.append(k)
            i += 1

        word_str = "".join(res_chars)
        if active_tone != TONES['NONE']:
            word_str = self._apply_tone_to_word(word_str, active_tone)

        raw_is_d_run = bool(raw_str) and all(char.lower() == 'd' for char in raw_str)
        if (
            not raw_is_d_run
            and not w_escaped
            and word_str != raw_str
            and self._has_vietnamese_mark(word_str)
            and word_str.lower() != 'đ'
            and not self._is_potential_vietnamese_prefix(word_str)
            and not self._is_valid_vietnamese_syllable(word_str)
        ):
            return self._evaluate_raw_escaped(keys)

        return word_str

        return word_str

    def _should_apply_nang_on_w_escape(self, chars):
        if len(chars) < 3 or chars[-1].lower() != 'c':
            return False

        vowels = []
        for ch in chars:
            if ch in CHAR_DECOMPOSE and CHAR_DECOMPOSE[ch][1] != 'stroke':
                vowels.append(CHAR_DECOMPOSE[ch])
        if len(vowels) < 2:
            return False

        prev_base, prev_hat, _prev_tone = vowels[-2]
        cur_base, cur_hat, _cur_tone = vowels[-1]
        return (
            prev_base.lower() == 'u'
            and prev_hat == 0
            and cur_base.lower() == 'o'
            and cur_hat == 0
        )

    def _has_vietnamese_mark(self, word):
        for ch in word:
            if ch in CHAR_DECOMPOSE:
                _base, hat, tone = CHAR_DECOMPOSE[ch]
                if hat == 'stroke' or hat != 0 or tone != 0:
                    return True
        return False

    def _is_potential_vietnamese_prefix(self, word):
        if word.lower() == 'đ':
            return True

        vowel_indices = [
            idx for idx, ch in enumerate(word)
            if ch in CHAR_DECOMPOSE and CHAR_DECOMPOSE[ch][1] != 'stroke'
        ]
        if not vowel_indices:
            return False

        first_vowel = vowel_indices[0]
        last_vowel = vowel_indices[-1]

        initial = []
        for ch in word[:first_vowel]:
            if ch in CHAR_DECOMPOSE and CHAR_DECOMPOSE[ch][1] == 'stroke':
                initial.append('d')
            elif ch.isalpha() and ch.lower() not in 'aeiouy':
                initial.append(ch.lower())
            else:
                return False

        initial_text = ''.join(initial)
        nucleus_start = first_vowel
        if (
            initial_text == 'q'
            and first_vowel < last_vowel
            and CHAR_DECOMPOSE[word[first_vowel]][0].lower() == 'u'
            and CHAR_DECOMPOSE[word[first_vowel]][1] == 0
        ):
            initial_text = 'qu'
            nucleus_start += 1
        elif (
            initial_text == 'g'
            and first_vowel < last_vowel
            and CHAR_DECOMPOSE[word[first_vowel]][0].lower() == 'i'
            and CHAR_DECOMPOSE[word[first_vowel]][1] == 0
        ):
            initial_text = 'gi'
            nucleus_start += 1

        if initial_text not in VN_VALID_INITIALS:
            return False

        bases = []
        for ch in word[nucleus_start:last_vowel + 1]:
            if ch not in CHAR_DECOMPOSE or CHAR_DECOMPOSE[ch][1] == 'stroke':
                return False
            bases.append(CHAR_DECOMPOSE[ch][0].lower())
        base_text = ''.join(bases)
        if not any(nucleus.startswith(base_text) for nucleus in VN_NUCLEUS_BASES):
            return False

        trailing = []
        for ch in word[last_vowel + 1:]:
            if not ch.isalpha() or ch.lower() in VN_VOWELS.lower() or ch in CHAR_DECOMPOSE:
                return False
            trailing.append(ch.lower())
        trailing_text = ''.join(trailing)
        return any(coda.startswith(trailing_text) for coda in VN_VALID_CODAS)

    def _is_valid_vietnamese_syllable(self, word):
        vowel_indices = [
            idx for idx, ch in enumerate(word)
            if ch in CHAR_DECOMPOSE and CHAR_DECOMPOSE[ch][1] != 'stroke'
        ]
        if not vowel_indices:
            return False

        first_vowel = vowel_indices[0]
        last_vowel = vowel_indices[-1]

        initial = []
        for ch in word[:first_vowel]:
            if ch in CHAR_DECOMPOSE and CHAR_DECOMPOSE[ch][1] == 'stroke':
                initial.append('d')
            elif ch.isalpha() and ch.lower() not in 'aeiouy':
                initial.append(ch.lower())
            else:
                return False

        initial_text = ''.join(initial)
        nucleus_start = first_vowel
        if (
            initial_text == 'q'
            and first_vowel < last_vowel
            and CHAR_DECOMPOSE[word[first_vowel]][0].lower() == 'u'
            and CHAR_DECOMPOSE[word[first_vowel]][1] == 0
        ):
            initial_text = 'qu'
            nucleus_start += 1
        elif (
            initial_text == 'g'
            and first_vowel < last_vowel
            and CHAR_DECOMPOSE[word[first_vowel]][0].lower() == 'i'
            and CHAR_DECOMPOSE[word[first_vowel]][1] == 0
        ):
            initial_text = 'gi'
            nucleus_start += 1

        if initial_text not in VN_VALID_INITIALS:
            return False

        nucleus = []
        for ch in word[nucleus_start:last_vowel + 1]:
            if ch not in CHAR_DECOMPOSE or CHAR_DECOMPOSE[ch][1] == 'stroke':
                return False
            base, hat, _tone = CHAR_DECOMPOSE[ch]
            nucleus.append(f'{base.lower()}{hat}')
        if ''.join(nucleus) not in VN_VALID_NUCLEI:
            return False

        coda = []
        for ch in word[last_vowel + 1:]:
            if not ch.isalpha() or ch.lower() in VN_VOWELS.lower() or ch in CHAR_DECOMPOSE:
                return False
            coda.append(ch.lower())
        return ''.join(coda) in VN_VALID_CODAS

    def _find_late_mark_target(self, chars, base_vowel):
        markable = {
            'a': set('aáàảãạAÁÀẢÃẠ'),
            'e': set('eéèẻẽẹEÉÈẺẼẸ'),
            'o': set('oóòỏõọOÓÒỎÕỌ'),
        }
        if base_vowel not in markable:
            return None

        skipped_trailing_consonant = False
        for idx in range(len(chars) - 1, -1, -1):
            ch = chars[idx]
            if ch not in VN_VOWELS and ch not in CHAR_DECOMPOSE:
                if not skipped_trailing_consonant and ch.isalpha():
                    skipped_trailing_consonant = True
                    continue
                break
            if ch in markable[base_vowel]:
                return idx

        return None

    def _apply_tone_to_word(self, word, tone):
        vowels_info = []
        for idx, ch in enumerate(word):
            if ch in CHAR_DECOMPOSE:
                base, hat, current_tone = CHAR_DECOMPOSE[ch]
                if hat != 'stroke':
                    vowels_info.append((idx, ch, base, hat, current_tone))

        if not vowels_info:
            return word

        # Handle ay + tone SAC -> â + SAC
        bases = "".join([v[2].lower() for v in vowels_info])
        if bases == "ay" and tone == 1: # tone 1 = SAC (thấy, mấy, đấy)
            v_idx, ch, base, hat, _ = vowels_info[0]
            is_upper = ch.isupper()
            lookup_key = (base.lower(), 1) # hat 1 = â
            if lookup_key in VOWEL_MATRIX:
                new_char = VOWEL_MATRIX[lookup_key][tone]
                if is_upper:
                    new_char = new_char.upper()
                word = word[:v_idx] + new_char + word[v_idx+1:]
                return word

        target_idx = self._find_tone_vowel_index(word, vowels_info)
        v_idx, ch, base, hat, current_tone = vowels_info[target_idx]

        base_lower = base.lower()
        is_upper = ch.isupper()
        
        lookup_key = (base_lower, hat)
        if lookup_key in VOWEL_MATRIX:
            new_char = VOWEL_MATRIX[lookup_key][tone]
            if is_upper:
                new_char = new_char.upper()
            return word[:v_idx] + new_char + word[v_idx+1:]

        return word

    def _find_tone_vowel_index(self, word, vowels_info):
        n = len(vowels_info)
        if n == 1:
            return 0

        last_vowel_pos = vowels_info[-1][0]
        last_syllable_vowels = [v for v in vowels_info if v[0] >= last_vowel_pos - 2]

        pair_bases = "".join([v[2].lower() for v in vowels_info[:2]])
        pair_hats = "".join([str(v[3]) for v in vowels_info[:2]])
        if pair_bases == 'uo' and (pair_hats == '33' or pair_hats == '00'):
            return 1

        word_lower = word.lower()
        first_vowel_idx = vowels_info[0][0]
        if first_vowel_idx > 0:
            prefix = word_lower[:first_vowel_idx]
            if (
                (prefix.endswith('q') and vowels_info[0][2].lower() == 'u')
                or (prefix.endswith('g') and vowels_info[0][2].lower() == 'i')
            ) and n > 1:
                return 1

        if n >= 2 and vowels_info[-1][2].lower() in ['i', 'y', 'u']:
            return n - 2
        
        for i, v in enumerate(vowels_info):
            if v in last_syllable_vowels:
                hat = v[3]
                if hat in [1, 2, 3]:
                    return i

        target_v = last_syllable_vowels[-1] if len(last_syllable_vowels) > 1 and (last_vowel_pos < len(word) - 1) else last_syllable_vowels[0]
        return vowels_info.index(target_v)
