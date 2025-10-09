# © 2025 Unit Circle Inc.
# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(uccomm)

zephyr_library_sources(
  cb.c
  cobs.c
  cbor.c
  crc32c.c
  pcg32.c
)

zephyr_include_directories(.)

