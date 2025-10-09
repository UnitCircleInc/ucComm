
# CBOR

Implementation of [RFC 8949](https://www.rfc-editor.org/rfc/rfc8949.html)

See [CBOR.io](https://cbor.io) for more info on __CBOR__.

## Deterministic Encoding

This encoder:

* uses _prefered serialization_ for ints, lengths in major types 2-5, and tags.  The application is not able to override this behaviour.
* enables the application to encode definite length items (for text, bytes, arrays and maps), but does not enforce this behviour.
* enables the applicaiton to encode maps with bytewise lexiograhic ordering of keys, but does not enfore this behavoir.
* Native floating values (float16, float32, float64) are encoded using _prefered serialization__ for float values (tag 7, ai 24, 25, 26).
* Native integer values (int8, int16, int32, int64 and uint8, uint16, uint32, and uint64) are encoded using _prefered serialization__ for integer values (tags 0 an 1).

The decoder:

* Accepts ints, lengths in major types 2-5, and tags in non-_prefered serialization_.  E.g. `1800` for the value 0 (vs `00`).
* Does not provide an error indication to the application when maps contain repeated keys.  When using `cbor_read_map` the values for all keys (including the repeated ones) are returned.  When using `cbor_unpack` the value for the first matching key (in the byte stream) is returned (any others are not accessable).
* Accepts `NaN` and `±Infinity` for floating point values.
* Accepts the following tags:
    * 0 (datetime) with a string value in ISO8801 format.
    * 1 (datetime) with a float or int value
    * 4 (decimal)
    * 24 (encoded)
    * 30 (rational)
    * 55799 (self descriptive)
* API requries the user specify the output "C" type when converting values.
    * For integer valued inputs that cannot be represented in the "C" type, e.g. a value of 257 when asking for "C" type of uint8 - a `CBOR_RANGE_ERROR` is returned.
    * For float valued inputs overflows or underflows will result in the output value being respresented as either `±Infinity` or `0`.
    * If output type is float/double input types of uint/nint/decimal/rational will automatically be converted to the request float type.
* Text strings are checked for valid UTF-8 a.  Controlled with `CBOR_CHECK_UTF8` define.
* Arrays can nest upto a default depth of 4.  Can be increased with `CBOR_MAX_RECURSION` define.


# COBS

Implementation of [COBS](http://www.stuartcheshire.org/papers/COBSforToN.pdf)

API allows for "in-place" computation to reuse input buffer as output buffer to reduce buffer memory required.

For encode:

```C
   // MAX_FRAME_SIZE is the maximum size of a frame before encoding/framing
   // 2 + for frame characters
   static uint8_t buffer[2 + COBS_ENC_SIZE(MAX_FRAME_SIZE)];

   // Place so input data is at right end of buffer
   uint8_t* in = &buffer[2 + COBS_ENC_SIZE(MAX_FRAME_SIZE) - n_in];

   // Place output so can prefix encoded data with frame character
   uint8_t* out = &buffer[1];

   // Fill in - e.g. memmove(in, data, n_in);

   // even though n_out will be larger than n_in,
   // internally the output pointer will not catch the input pointer
   // so safe to re-use the buffer/memory.
   size_t n_out = cobs_enc(out, in, n_in);

   // Add frame markers
   buffer[0] = 0x00;
   buffer[n_out + 1] = 0x00;
   size_t n_total = n_out + 2;
```

For decode:

```C
   static uint8_t buffer[COBS_ENC_SIZE(MAX_FRAME_SIZE)];

   // Place input starting a - e.g. memmove(buffer, rx, n_in);

   // n_out will be smaller than n_in
   // internally the output pointer will not catch the input pointer
   // so ok to make input and output buffers the same.
   size_t n_out = cobs_dec(buffer, buffer, n_in);
```

# CRC32C

API allows for incremental computation of the CRC, e.g.:

```C
  uint32_t crc = CRC32C_INIT;
  crc = crc32c_update(crc, data1, data1_n);
  crc = crc32c_update(crc, data2, data2_n);
  ...
```

If the crc is appended to a packet (in little endian format), then CRC can be checked with the following approach, avoiding the convertion of the crc back to a `uint32_t` value.

```C
  uint8_t* data; // input data buffer with crc appended in LE order.
  size_t   data_n; // length of input data buffer including crc bytes.
  bool crc_ok = crc32c_update(CRC32C_INIT, data, data_n) == CRC32C_OK_REM;
  if ((data_n >= 4) && crc_ok) {
    // data[0..data_n-4] has no detectable errors.
  }
  else {
    // data is too short or data[0..data_n-4] has detectable errors.
  }
```


[CRC32C](http://users.ece.cmu.edu/~koopman/crc/crc32.html) can detect:

* all 1 bit errors for packet lengths up to 2147483615 bits.
* all 2 bit errors for packet lengths up to 2147483615 bits.
* all 3 bit errors for packet lengths up to 5243 bits.
* all 4 bit errors for packet lengths up to 5243 bits.

which should be sufficent for most application and is realtively low overhead.

[Cyclic Redundancy Code (CRC) Polynomial Selection For Embedded Networks](http://users.ece.cmu.edu/~koopman/roses/dsn04/koopman04_crc_poly_embedded.pdf) has details on how to evaluate polynomials for different contexts (packet sizes, etc.).

# CB (Circular Buffer)

Provides a light weight SPSC byte oriented queue that is thread safe.

