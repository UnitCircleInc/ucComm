# Character classes and statemachine from:
#   https://tools.ietf.org/html/rfc3629 section 4 BNF
#
# Implementation inspired from:
#  http://bjoern.hoehrmann.de/utf-8/decoder/dfa/
#
# This implemenation does:
# * Reduction of the character_class table by handling the
# * character class 0x00-0x7f as a special case with an if statement.
# * Not "encoding" the error state in the state machine.
# * Using character class length as multiplier vs 16 when looking up
#   state transitions.
#
# Together these reduce the memory by 184 bytes.
#   400 (256 + 9 * 16) vs  216 (128 + 8 * 11)

cc_st_2   = 0
cc_st_3a  = 1
cc_st_3b  = 2
cc_st_3c  = 3
cc_st_4a  = 4
cc_st_4b  = 5
cc_st_4c  = 6
cc_tail_a = 7
cc_tail_b = 8
cc_tail_c = 9
cc_error  = 10
character_classes = {
  cc_tail_a: ((0x80, 0x8f),),
  cc_tail_b: ((0x90, 0x9f),),
  cc_tail_c: ((0xa0, 0xbf),),
  cc_error:  ((0xc0, 0xc1), (0xf5, 0xff)),
  cc_st_2:   ((0xc2, 0xdf),),
  cc_st_3a:  ((0xe0, 0xe0),),
  cc_st_3b:  ((0xe1, 0xec), (0xee, 0xef)),
  cc_st_3c:  ((0xed, 0xed),),
  cc_st_4a:  ((0xf0, 0xf0),),
  cc_st_4b:  ((0xf1, 0xf3),),
  cc_st_4c:  ((0xf4, 0xf4),),
}

# Default transitions for unspecific inputs is to error state
# This state does not need to be encoded as it is the error case
# and we fail immediately on detecting it.
# If it is last state in table then we can not store it.
accept_state  = 0
tail_1_state  = 1
tail_2a_state = 2
tail_2b_state = 3
tail_2c_state = 4
tail_3a_state = 5
tail_3b_state = 6
tail_3c_state = 7
error_state   = 8

states = {
    accept_state: {
      cc_st_2:   tail_1_state,
      cc_st_3a:  tail_2b_state,
      cc_st_3b:  tail_2a_state,
      cc_st_3c:  tail_2c_state,
      cc_st_4a:  tail_3a_state,
      cc_st_4b:  tail_3b_state,
      cc_st_4c:  tail_3c_state,
    },
    tail_1_state: {
      cc_tail_a: accept_state,
      cc_tail_c: accept_state,
      cc_tail_b: accept_state
    },
    tail_2a_state: {
      cc_tail_a: tail_1_state,
      cc_tail_b: tail_1_state,
      cc_tail_c: tail_1_state
    },
    tail_2b_state: {
      cc_tail_c: tail_1_state
    },
    tail_2c_state: {
      cc_tail_a: tail_1_state,
      cc_tail_b: tail_1_state
    },
    tail_3a_state: {
      cc_tail_b: tail_2a_state,
      cc_tail_c: tail_2a_state
    },
    tail_3b_state: {
      cc_tail_a: tail_2a_state,
      cc_tail_b: tail_2a_state,
      cc_tail_c: tail_2a_state
    },
    tail_3c_state: {
      cc_tail_a: tail_2a_state
    },
}

# Generate character class table
cc = [None]*128

for k, v in character_classes.items():
  for s, e in v:
    for i in range(s, e+1):
      if i >= 0x80:
        cc[i - 0x80] = k

# Generate state transition table
ccl = len(character_classes.keys())
sl = len(states.keys())
stt = [error_state*ccl]*(ccl*sl)
for k, v in states.items():
  for k2, v2 in v.items():
    stt[k*ccl + k2] = v2*ccl

# Reference implementations
def utf8valid(b):
  state = accept_state*ccl
  for v in b:
    if v < 0x80:
      if state != accept_state*ccl:
        return False
    else:
      state = stt[state + cc[v - 0x80]]
      if state == error_state*ccl:
        return False
  return state == accept_state*ccl

# A slight variant of the reference that retuns true
# reguardless of end state - indicating that the byte sequence is a valid
# prefix of a character.
def utf8prefix_valid(b):
  state = accept_state*ccl
  for v in b:
    if v < 0x80:
      if state != accept_state*ccl:
        return False
    else:
      state = stt[state + cc[v - 0x80]]
      if state == error_state*ccl:
        return False
  return True

# Utility for building recursive tree
def update(d, v):
  if len(v) > 0:
    if v[0] not in d:
      d[v[0]] = {}
    update(d[v[0]], v[1:])

# Genreate all possible valid 21 bit sequences
# Filter out the invalid code values
# Returns a recusrive dictionary where the keys at each layer are the
# valid characters at that point in the tree.
def gen_all_valid():
  valid = {}
  for i in range(1<<21):
    # Encoder is simple
    if i < 0x80:
      v = bytes((i,))
    elif i < 0x800:
      v = bytes(((i>>6)+0xc0, (i & 0x3f)+0x80))
    elif i < 0x10000:
      v = bytes(((i >> 12) + 0xe0, ((i >> 6) & 0x3f) + 0x80, (i & 0x3f) + 0x80))
    else:
      v = bytes(((i >> 18) + 0xf0, ((i >> 12) & 0x3f) + 0x80, ((i >> 6) & 0x3f) + 0x80, (i & 0x3f) + 0x80))

    # The following are not valid according RFC3629 section 3
    if (0xd800 <= i and i < 0xe000) or (i > 0x10ffff):
      pass
    else:
      update(valid, v)
  return valid

# Walks the recusive dicionary testing that the complement set is not a
# valid prefix and if we are at the end of the recusion that the character is
# valid.
good_count = 0
bad_count = 0
def check_prefix(prefix, d):
  global good_count
  global bad_count
  for x in set(range(256)) - set(d.keys()):
    bad_count += 1
    assert not utf8prefix_valid(prefix + bytes((x,)))
  for x in set(d.keys()):
    if len(d[x]) > 0:
      check_prefix(prefix + bytes((x,)), d[x])
    else:
      good_count += 1
      assert utf8valid(prefix + bytes((x,)))

# Perform exhaustive testing on is_valid_utf8
def test_all():
  valid = gen_all_valid()
  check_prefix(b'', valid)
  global good_count
  global bad_count
  assert good_count == 1112064
  assert bad_count  == 3389197

if __name__ == '__main__':
  import hashlib
  with open(__file__) as f:
    b = f.read()
  cs = hashlib.sha256(b.encode('utf8')).digest()
  print(f"""// AUTOGENERATED BY utf8valid.py version:
//   {cs.hex()}
//
// Implementation inspired from:
//   http://bjoern.hoehrmann.de/utf-8/decoder/dfa/
// Details taken from:
//   https://tools.ietf.org/html/rfc3629 section 4 BNF

#pragma once
""")
  print(f"#define UTF8_ACCEPT {accept_state * ccl}")
  print(f"#define UTF8_ERROR {error_state * ccl}")
  print()
  print("static const uint8_t utf8cc[] = {")
  for i, x in enumerate(cc):
    print(f"{x:2d},", end='')
    if (i + 1) % 16 == 8:
      print(" ", end='')
    elif (i + 1) % 16 == 0:
      print()
  print("};")
  print()

  print("static const uint8_t utf8stt[] = {")
  for i, x in enumerate(stt):
    print(f"{x:2d},", end='')
    if (i + 1) % (ccl*2) == ccl:
      print(" ", end='')
    elif (i + 1) % (ccl*2) == 0:
      print()
  print("};")
  print("""
static bool is_valid_utf8(const char *str, size_t len) {
  uint32_t type;
  uint32_t state = UTF8_ACCEPT;

  while (len-- > 0) {
    uint8_t c = (uint8_t) *str++;
    if (c < 0x80) {
      if (state != UTF8_ACCEPT) return false;
    }
    else {
      type = utf8cc[c-0x80];
      state = utf8stt[state + type];
      if (state == UTF8_ERROR) break;
    }
  }
  return state == UTF8_ACCEPT;
}""")

