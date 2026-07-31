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
    's': TONES['SAC'],
    'f': TONES['HUYEN'],
    'r': TONES['HOI'],
    'x': TONES['NGA'],
    'j': TONES['NANG'],
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

    def set_mode(self, mode):
        self.mode = mode.lower()

    def process_key(self, char):
        if not char.isalnum() and char not in ['_']:
            self.raw_keys.append(char)
            return ('APPEND', 0, char)

        if self.mode == 'telex':
            return self._process_telex(char)
        else:
            self.raw_keys.append(char)
            return ('APPEND', 0, char)

    def _process_telex(self, char):
        current_word = self.get_current_word()
        
        test_keys = self.raw_keys + [char]
        new_word = self._evaluate_telex_sequence(test_keys)

        if new_word != current_word + char:
            prefix_len = 0
            min_len = min(len(current_word), len(new_word))
            while prefix_len < min_len and current_word[prefix_len] == new_word[prefix_len]:
                if current_word[prefix_len] in VN_VOWELS:
                    break
                prefix_len += 1

            backspace_count = len(current_word) - prefix_len
            insert_str = new_word[prefix_len:]
            self.raw_keys.append(char)
            return ('MODIFY', backspace_count, insert_str)
        else:
            self.raw_keys.append(char)
            return ('APPEND', 0, char)

    def get_current_word(self):
        return self._evaluate_telex_sequence(self.raw_keys)

    def _evaluate_telex_sequence(self, keys):
        if not keys:
            return ""

        raw_str = "".join(keys)

        protected_words = {
            'python', 'password', 'desktop', 'windows', 'google', 'version', 'raw',
            'javascript', 'typescript', 'telex', 'vni',
            'terminal', 'code', 'chrome', 'firefox', 'libreoffice',
            'telegram', 'discord', 'zalo', 'web', 'latinh', 'pre'
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
        is_double_w_escape = (
            len(raw_str) == 2 and all(c.lower() == 'w' for c in raw_str)
        )
        if (
            not is_double_w_escape
            and len(raw_str) >= 2
            and any(c.isupper() and c.lower() != 'w' for c in raw_str[1:])
        ):
            return raw_str

        res_chars = []
        active_tone = TONES['NONE']
        tone_count = {}
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
                if i + 2 < n and keys[i+1].lower() == 'd' and keys[i+2].lower() == 'd':
                    res_chars.append('D' if k.isupper() else 'd')
                    res_chars.append('D' if keys[i+2].isupper() else 'd')
                    i += 3
                    continue
                if i + 1 < n and keys[i+1].lower() == 'd':
                    res_chars.append('Đ' if k.isupper() else 'đ')
                    i += 2
                    continue
                elif i > 0 and keys[0].lower() == 'd' and res_chars and res_chars[0] in ['d', 'D'] and any(c in VN_VOWELS for c in "".join(res_chars)):
                    res_chars[0] = 'Đ' if res_chars[0].isupper() else 'đ'
                    i += 1
                    continue
                elif i > 0 and res_chars and res_chars[-1] in ['d', 'D'] and not any(c in VN_VOWELS for c in "".join(res_chars)):
                    prev_upper = res_chars[-1].isupper()
                    res_chars[-1] = 'Đ' if prev_upper else 'đ'
                    i += 1
                    continue
                else:
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
                        'a': {'a': 'â', 'á': 'ấ', 'à': 'ầ', 'ả': 'ẩ', 'ã': 'ẫ', 'ạ': 'ậ',
                              'A': 'Â', 'Á': 'Ấ', 'À': 'Ầ', 'Ả': 'Ẩ', 'Ã': 'Ẫ', 'Ạ': 'Ậ'},
                        'e': {'e': 'ê', 'é': 'ế', 'è': 'ề', 'ẻ': 'ể', 'ẽ': 'ễ', 'ẹ': 'ệ',
                              'E': 'Ê', 'É': 'Ế', 'È': 'Ề', 'Ẻ': 'Ể', 'Ẽ': 'Ễ', 'Ẹ': 'Ệ'},
                        'o': {'o': 'ô', 'ó': 'ố', 'ò': 'ồ', 'ỏ': 'ổ', 'õ': 'ỗ', 'ọ': 'ộ',
                              'O': 'Ô', 'Ó': 'Ố', 'Ò': 'Ồ', 'Ỏ': 'Ổ', 'Õ': 'Ỗ', 'Ọ': 'Ộ'},
                    }
                    res_chars[late_mark_idx] = mark_map[lk][res_chars[late_mark_idx]]
                    i += 1
                    continue

            # 3. 'w' hooks: aw -> ă, ow -> ơ, uw -> ư, uow -> ươ
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

                word_so_far = "".join(res_chars)
                if word_so_far.lower().endswith('uo'):
                    changes = [(len(res_chars) - 2, res_chars[-2]),
                               (len(res_chars) - 1, res_chars[-1])]
                    res_chars[-2] = 'Ư' if res_chars[-2].isupper() else 'ư'
                    res_chars[-1] = 'Ơ' if res_chars[-1].isupper() else 'ơ'
                    w_modifier = {'key': k, 'changes': changes, 'generated': False}
                    i += 1
                    continue
                elif word_so_far.lower().endswith('ua'):
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
                v_hook_map = {'a': 'ă', 'A': 'Ă', 'o': 'ơ', 'O': 'Ơ', 'u': 'ư', 'U': 'Ư'}
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
                    i += 2
                    continue

            # 4. Check Telex Tones (s, f, r, x, j, z)
            has_vowel_already = any(c in VN_VOWELS for c in "".join(res_chars))

            has_future_vowel = any(c in 'aeiouyAEIOUY' for c in keys[i+1:])

            if (lk in TELEX_TONE_KEYS or lk == 'z') and (has_vowel_already or (res_chars and has_future_vowel)):
                tail = "".join(res_chars).lower() + lk
                if not has_vowel_already and any(cluster.startswith(tail) for cluster in VN_INITIAL_CLUSTERS):
                    res_chars.append(k)
                    i += 1
                    continue
                if has_vowel_already and any(tail.endswith(cl) for cl in CONSONANT_CLUSTERS):
                    res_chars.append(k)
                    i += 1
                    continue

                tone_count[lk] = tone_count.get(lk, 0) + 1
                if tone_count[lk] > 1:
                    active_tone = TONES['NONE']
                    res_chars.append(k)
                    i += 1
                    continue

                if lk == 'z':
                    active_tone = TONES['NONE']
                else:
                    active_tone = TELEX_TONE_KEYS[lk]
                i += 1
                continue

            res_chars.append(k)
            i += 1

        word_str = "".join(res_chars)
        if active_tone != TONES['NONE']:
            word_str = self._apply_tone_to_word(word_str, active_tone)

        if (
            not w_escaped
            and word_str != raw_str
            and self._has_vietnamese_mark(word_str)
            and word_str.lower() != 'đ'
            and not self._is_potential_vietnamese_prefix(word_str)
            and not self._is_valid_vietnamese_syllable(word_str)
        ):
            return raw_str

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
        if pair_bases == 'uo' and pair_hats == '33':
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
