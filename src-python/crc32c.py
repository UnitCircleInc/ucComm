#! /usr/bin/env python

# Copyright 2017-2018 Unit Circle Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Implementation of CRC-32C from:
#   http://users.ece.cmu.edu/~koopman/crc/crc32.html

CRC32C_POLY = 0x82f63b78 # rev(0x11EDC6F41) // 2 or 0x105ec76f1//2
def build_idx(idx):
  rb = idx
  for _ in range(8):
    if rb & 1 != 0:
      rb = (rb >> 1) ^ CRC32C_POLY
    else:
      rb = rb >> 1
  return rb

tab = [build_idx(idx) for idx in range(256)]

def crc32c_update(c, d):
  c = c ^ 0xffffffff
  for v in d:
    c = (c >> 8) ^ tab[(c & 0xff) ^ v]
  return c ^ 0xffffffff

# Value of crc(0, a+struct.pack('I', crc(0, a))) for any string a
CRC_OK_REM = crc32c_update(0, b'\x00'*4)

def build_c(f):
  print('''/*
 * Copyright 2017-2018 Unit Circle Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

static const uint32_t crc32c_tab[256] = {
 ''', file=f, end='')
  for i, v in enumerate(tab):
    print(" 0x%08x," % v, file=f, end='')
    if (i + 1) % 4 == 0:
      print(file=f)
      if (i + 1) < len(tab):
        print(' ', file=f, end='')
  print('};', file=f)
  print('#define CRC32C_OK_REM (0x%08x)' % CRC_OK_REM, file=f)

def test_crc32c():
  print(hex(crc32c_update(0, b'hello there')))
  print(hex(crc32c_update(0, b'\x00')))
  print(hex(crc32c_update(0, b'\xff')))

  c = 0
  c = crc32c_update(c, b'hello ')
  c = crc32c_update(c, b'there')
  print(hex(c))

if __name__ == '__main__':
  #test_crc32c()
  #with open('../src-uc/crc32c_tab.h', 'wt') as f:
  import sys
  with sys.stdout as f:
    build_c(f)

