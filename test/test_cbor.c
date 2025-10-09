
#include <math.h>
#include <string.h>
#include "greatest.h"
#include "cbor.h"

#define UINT(v_) {.type = CBOR_TYPE_UINT, .value.uint_v = (v_) }

#define DECHEX(v_) ((((v_) >= '0') && ((v_) <= '9')) ? ((v_) - '0') : \
                    ((((v_) >= 'a') && ((v_) <= 'f')) ? ((v_) - 'a' + 10) : \
                      ((((v_) >= 'A') && ((v_) <= 'F')) ? ((v_) - 'A' + 10) : \
                     255)))
static size_t dechex(size_t nr, uint8_t* r, const char* s) {
  size_t n = 0;
  while ((s[0] != '\0') && (s[1] != '\0') && (n < nr)) {
    uint8_t d1 = DECHEX(s[0]);
    uint8_t d2 = DECHEX(s[1]);
    if ((d1 > 15) || (d2 > 15)) return 0;
    r[n++] = d1 << 4 | d2;
    s += 2;
  }
  if (s[0] != '\0') return 0; // indicates error in decoding
  return n;
}

TEST test_int64(void) {
  struct {
    int64_t v;
    const char* encoded;
    bool canonical;
    cbor_error_t error;
  } values[] = {
    {              0, "00",                 true,  CBOR_ERROR_NONE },
    {              1, "01",                 true,  CBOR_ERROR_NONE },
    {           24-1, "17",                 true,  CBOR_ERROR_NONE },
    {           24  , "1818",               true,  CBOR_ERROR_NONE },
    {           24+1, "1819",               true,  CBOR_ERROR_NONE },
    {  (1ll <<  8)-1, "18FF",               true,  CBOR_ERROR_NONE },
    {  (1ll <<  8)  , "190100",             true,  CBOR_ERROR_NONE },
    {  (1ll <<  8)+1, "190101",             true,  CBOR_ERROR_NONE },
    {  (1ll << 16)-1, "19FFFF",             true,  CBOR_ERROR_NONE },
    {  (1ll << 16)  , "1A00010000",         true,  CBOR_ERROR_NONE },
    {  (1ll << 16)+1, "1A00010001",         true,  CBOR_ERROR_NONE },
    {  (1ll << 32)-1, "1AFFFFFFFF",         true,  CBOR_ERROR_NONE },
    {  (1ll << 32)  , "1B0000000100000000", true,  CBOR_ERROR_NONE },
    {  (1ll << 32)+1, "1B0000000100000001", true,  CBOR_ERROR_NONE },
    { 9223372036854775807, "1B7FFFFFFFFFFFFFFF", true,  CBOR_ERROR_NONE },
    {            0ll, "1800",               false, CBOR_ERROR_NONE },
    {            1ll, "190001",             false, CBOR_ERROR_NONE },
    {            2ll, "1a00000002",         false, CBOR_ERROR_NONE },
    {            3ll, "1b0000000000000003", false, CBOR_ERROR_NONE },
    {              0, "1C",                 false, CBOR_ERROR_INVALID_AI },
    {              0, "1D",                 false, CBOR_ERROR_INVALID_AI },
    {              0, "1E",                 false, CBOR_ERROR_INVALID_AI },
    {              0, "1F",                 false, CBOR_ERROR_INVALID_AI },

    {             -1, "20",                 true,  CBOR_ERROR_NONE },
    {             -2, "21",                 true,  CBOR_ERROR_NONE },
    {          -24  , "37",                 true,  CBOR_ERROR_NONE },
    {          -24-1, "3818",               true,  CBOR_ERROR_NONE },
    {          -24-2, "3819",               true,  CBOR_ERROR_NONE },
    { -(1ll <<  8)  , "38FF",               true,  CBOR_ERROR_NONE },
    { -(1ll <<  8)-1, "390100",             true,  CBOR_ERROR_NONE },
    { -(1ll <<  8)-2, "390101",             true,  CBOR_ERROR_NONE },
    { -(1ll << 16)  , "39FFFF",             true,  CBOR_ERROR_NONE },
    { -(1ll << 16)-1, "3A00010000",         true,  CBOR_ERROR_NONE },
    { -(1ll << 16)-2, "3A00010001",         true,  CBOR_ERROR_NONE },
    { -(1ll << 32)  , "3AFFFFFFFF",         true,  CBOR_ERROR_NONE },
    { -(1ll << 32)-1, "3B0000000100000000", true,  CBOR_ERROR_NONE },
    { -(1ll << 32)-2, "3B0000000100000001", true,  CBOR_ERROR_NONE },
    { -(1ull << 63)  , "3B7FFFFFFFFFFFFFFF", true,  CBOR_ERROR_NONE },
    {              0, "3C",                 false, CBOR_ERROR_INVALID_AI },
    {              0, "3D",                 false, CBOR_ERROR_INVALID_AI },
    {              0, "3E",                 false, CBOR_ERROR_INVALID_AI },
    {              0, "3F",                 false, CBOR_ERROR_INVALID_AI },

    {           -1ll, "3800",               false, CBOR_ERROR_NONE },
    {           -2ll, "390001",             false, CBOR_ERROR_NONE },
    {           -3ll, "3A00000002",         false, CBOR_ERROR_NONE },
    {           -4ll, "3B0000000000000003", false, CBOR_ERROR_NONE },

    {              0, "1B8000000000000000", false, CBOR_ERROR_RANGE },
    {              0, "1BFFFFFFFFFFFFFFFF", false, CBOR_ERROR_RANGE },
    {              0, "3B8000000000000000", false, CBOR_ERROR_RANGE },
    {              0, "3BFFFFFFFFFFFFFFFF", false, CBOR_ERROR_RANGE },
  };

  for (size_t i = 0; i < sizeof(values)/sizeof(*values); i++) {
    cbor_stream_t s;
    cbor_error_t e;
    uint8_t b[20];
    uint8_t encoded[20];
    size_t  encoded_n = dechex(sizeof(encoded), encoded, values[i].encoded);

    if (values[i].canonical) {
      cbor_init(&s, b, sizeof(b));
      e = cbor_pack(&s, "q", values[i].v);
      ASSERT_EQ_FMT(CBOR_ERROR_NONE, e, "%d");
      ASSERT_EQ_FMT(encoded_n, cbor_read_avail(&s), "%zu");
      ASSERT_MEM_EQ(encoded, b, encoded_n);
    }

    int64_t v;
    cbor_init(&s, encoded, encoded_n);
    e = cbor_unpack(&s, "q", &v);
    ASSERT_EQ_FMT(values[i].error, e, "%d");
    if (values[i].error == CBOR_ERROR_NONE) {
      ASSERT_EQ_FMT(values[i].v, v, "%lld");
    }
  }
  PASS();
}

TEST test_uint64(void) {
  struct {
    uint64_t v;
    const char* encoded;
    bool canonical;
    cbor_error_t error;
  } values[] = {
    {               0, "00",                 true,  CBOR_ERROR_NONE },
    {               1, "01",                 true,  CBOR_ERROR_NONE },
    {            24-1, "17",                 true,  CBOR_ERROR_NONE },
    {            24  , "1818",               true,  CBOR_ERROR_NONE },
    {            24+1, "1819",               true,  CBOR_ERROR_NONE },
    {  (1ull <<  8)-1, "18FF",               true,  CBOR_ERROR_NONE },
    {  (1ull <<  8)  , "190100",             true,  CBOR_ERROR_NONE },
    {  (1ull <<  8)+1, "190101",             true,  CBOR_ERROR_NONE },
    {  (1ull << 16)-1, "19FFFF",             true,  CBOR_ERROR_NONE },
    {  (1ull << 16)  , "1A00010000",         true,  CBOR_ERROR_NONE },
    {  (1ull << 16)+1, "1A00010001",         true,  CBOR_ERROR_NONE },
    {  (1ull << 32)-1, "1AFFFFFFFF",         true,  CBOR_ERROR_NONE },
    {  (1ull << 32)  , "1B0000000100000000", true,  CBOR_ERROR_NONE },
    {  (1ull << 32)+1, "1B0000000100000001", true,  CBOR_ERROR_NONE },
    { 18446744073709551615ull, "1BFFFFFFFFFFFFFFFF", true,  CBOR_ERROR_NONE },
    {            0ll, "1800",               false, CBOR_ERROR_NONE },
    {            1ll, "190001",             false, CBOR_ERROR_NONE },
    {            2ll, "1a00000002",         false, CBOR_ERROR_NONE },
    {            3ll, "1b0000000000000003", false, CBOR_ERROR_NONE },
    {              0, "1C",                 false, CBOR_ERROR_INVALID_AI },
    {              0, "1D",                 false, CBOR_ERROR_INVALID_AI },
    {              0, "1E",                 false, CBOR_ERROR_INVALID_AI },
    {              0, "1F",                 false, CBOR_ERROR_INVALID_AI },
  };

  for (size_t i = 0; i < sizeof(values)/sizeof(*values); i++) {
    cbor_stream_t s;
    cbor_error_t e;
    uint8_t b[20];
    uint8_t encoded[20];
    size_t  encoded_n = dechex(sizeof(encoded), encoded, values[i].encoded);

    if (values[i].canonical) {
      cbor_init(&s, b, sizeof(b));
      e = cbor_pack(&s, "Q", values[i].v);
      ASSERT_EQ_FMT(CBOR_ERROR_NONE, e, "%d");
      ASSERT_EQ_FMT(encoded_n, cbor_read_avail(&s), "%zu");
      ASSERT_MEM_EQ(encoded, b, encoded_n);
    }

    int64_t v;
    cbor_init(&s, encoded, encoded_n);
    e = cbor_unpack(&s, "Q", &v);
    ASSERT_EQ_FMT(values[i].error, e, "%d");
    if (values[i].error == CBOR_ERROR_NONE) {
      ASSERT_EQ_FMT(values[i].v, v, "%llu");
    }
  }
  PASS();
}

TEST test_simple(void) {
  cbor_stream_t s;
  cbor_error_t e;
  uint8_t b[20];
  uint8_t encoded[20];
  size_t  encoded_n;
  bool v;

  // False
  encoded_n = dechex(sizeof(encoded), encoded, "F4");
  cbor_init(&s, b, sizeof(b));
  e = cbor_pack(&s, "?", false);
  ASSERT_EQ_FMT(CBOR_ERROR_NONE, e, "%d");
  ASSERT_EQ_FMT(encoded_n, cbor_read_avail(&s), "%zu");
  ASSERT_MEM_EQ(encoded, b, encoded_n);

  cbor_init(&s, encoded, encoded_n);
  e = cbor_unpack(&s, "?", &v);
  ASSERT_EQ_FMT(CBOR_ERROR_NONE, e, "%d");
  ASSERT_EQ_FMT(false, v, "%d");

  // True
  encoded_n = dechex(sizeof(encoded), encoded, "F5");
  cbor_init(&s, b, sizeof(b));
  e = cbor_pack(&s, "?", true);
  ASSERT_EQ_FMT(CBOR_ERROR_NONE, e, "%d");
  ASSERT_EQ_FMT(encoded_n, cbor_read_avail(&s), "%zu");
  ASSERT_MEM_EQ(encoded, b, encoded_n);

  cbor_init(&s, encoded, encoded_n);
  e = cbor_unpack(&s, "?", &v);
  ASSERT_EQ_FMT(CBOR_ERROR_NONE, e, "%d");
  ASSERT_EQ_FMT(true, v, "%d");

  // Null/None
  encoded_n = dechex(sizeof(encoded), encoded, "F6");
  cbor_init(&s, b, sizeof(b));
  e = cbor_write_null(&s);
  ASSERT_EQ_FMT(CBOR_ERROR_NONE, e, "%d");
  ASSERT_EQ_FMT(encoded_n, cbor_read_avail(&s), "%zu");
  ASSERT_MEM_EQ(encoded, b, encoded_n);

  cbor_init(&s, encoded, encoded_n);
  e = cbor_read_null(&s);
  ASSERT_EQ_FMT(CBOR_ERROR_NONE, e, "%d");

  // Undefined
  encoded_n = dechex(sizeof(encoded), encoded, "F7");
  cbor_init(&s, b, sizeof(b));
  e = cbor_write_undefined(&s);
  ASSERT_EQ_FMT(CBOR_ERROR_NONE, e, "%d");
  ASSERT_EQ_FMT(encoded_n, cbor_read_avail(&s), "%zu");
  ASSERT_MEM_EQ(encoded, b, encoded_n);

  cbor_init(&s, encoded, encoded_n);
  e = cbor_read_undefined(&s);
  ASSERT_EQ_FMT(CBOR_ERROR_NONE, e, "%d");

  struct {
    uint8_t v;
    const char* encoded;
    bool canonical;
    cbor_error_t error;
  } values[] = {
    {    0, "E0",                 true,  CBOR_ERROR_NONE },
    {    1, "E1",                 true,  CBOR_ERROR_NONE },
    {   19, "F3",                 true,  CBOR_ERROR_NONE },
    {   32, "F820",               true,  CBOR_ERROR_NONE },
    {  255, "F8FF",               true,  CBOR_ERROR_NONE },
    {    0, "F800",               false, CBOR_ERROR_BAD_SIMPLE_VALUE },
    {    0, "F81F",               false, CBOR_ERROR_BAD_SIMPLE_VALUE },
    {    0, "FC",                 false, CBOR_ERROR_INVALID_AI },
    {    0, "FD",                 false, CBOR_ERROR_INVALID_AI },
    {    0, "FE",                 false, CBOR_ERROR_INVALID_AI },
    {    0, "FF",                 false, CBOR_ERROR_UNEXPECTED_BREAK },
  };

  for (size_t i = 0; i < sizeof(values)/sizeof(*values); i++) {
    cbor_stream_t s;
    cbor_error_t e;
    uint8_t b[20];
    uint8_t encoded[20];
    size_t  encoded_n = dechex(sizeof(encoded), encoded, values[i].encoded);

    if (values[i].canonical) {
      cbor_init(&s, b, sizeof(b));
      e = cbor_write_simple(&s, values[i].v);
      ASSERT_EQ_FMT(CBOR_ERROR_NONE, e, "%d");
      ASSERT_EQ_FMT(encoded_n, cbor_read_avail(&s), "%zu");
      ASSERT_MEM_EQ(encoded, b, encoded_n);
    }

    uint8_t v;
    cbor_init(&s, encoded, encoded_n);
    e = cbor_read_simple(&s, &v);
    ASSERT_EQ_FMT(values[i].error, e, "%d");
    if (values[i].error == CBOR_ERROR_NONE) {
      ASSERT_EQ_FMT(values[i].v, v, "%u");
    }
  }
  PASS();
}

TEST test_float(void) {
  cbor_stream_t s;
  cbor_error_t e;
  uint8_t b[20];
  uint8_t encoded[20];
  size_t  encoded_n;

  struct {
    double v;
    const char* encoded;
    bool canonical;
    cbor_error_t error;
  } values[] = {
    {         0.0, "f90000",               true,  CBOR_ERROR_NONE },
    {        -0.0, "f98000",               true,  CBOR_ERROR_NONE },
    {         1.0, "f93c00",               true,  CBOR_ERROR_NONE },
    {        -1.0, "f9bc00",               true,  CBOR_ERROR_NONE },
    {  (1<<11)-1., "f967ff",               true,  CBOR_ERROR_NONE },
    { -(1<<11)+1., "f9e7ff",               true,  CBOR_ERROR_NONE },
    {  (1<<12)-1., "fa457ff000",           true,  CBOR_ERROR_NONE },
    { -(1<<12)+1., "fac57ff000",           true,  CBOR_ERROR_NONE },
    {  (1<<24)-1., "fa4b7fffff",           true,  CBOR_ERROR_NONE },
    { -(1<<24)+1., "facb7fffff",           true,  CBOR_ERROR_NONE },
    {  (1<<25)-1., "fb417ffffff0000000",   true,  CBOR_ERROR_NONE },
    { -(1<<25)+1., "fbc17ffffff0000000",   true,  CBOR_ERROR_NONE },
    {   INFINITY, "f97c00",               true,  CBOR_ERROR_NONE },
    {  -INFINITY, "f9fc00",               true,  CBOR_ERROR_NONE },
    {        NAN, "f97e00",               true,  CBOR_ERROR_NONE },
    {   INFINITY, "fa7f800000",           false, CBOR_ERROR_NONE },
    {  -INFINITY, "faff800000",           false, CBOR_ERROR_NONE },
    {        NAN, "fa7fc00000",           false, CBOR_ERROR_NONE },
    {   INFINITY, "fb7ff0000000000000",   false, CBOR_ERROR_NONE },
    {  -INFINITY, "fbfff0000000000000",   false, CBOR_ERROR_NONE },
    {        NAN, "fb7ff8000000000000",   false, CBOR_ERROR_NONE },
  };

  for (size_t i = 0; i < sizeof(values)/sizeof(*values); i++) {
    cbor_stream_t s;
    cbor_error_t e;
    uint8_t b[20];
    uint8_t encoded[20];
    size_t  encoded_n = dechex(sizeof(encoded), encoded, values[i].encoded);

    if (values[i].canonical) {
      cbor_init(&s, b, sizeof(b));
      e = cbor_write_float64(&s, values[i].v);
      ASSERT_EQ_FMT(CBOR_ERROR_NONE, e, "%d");
      ASSERT_EQ_FMT(encoded_n, cbor_read_avail(&s), "%zu");
      ASSERT_MEM_EQ(encoded, b, encoded_n);
    }

    double v;
    cbor_init(&s, encoded, encoded_n);
    e = cbor_read_float64(&s, &v);
    ASSERT_EQ_FMT(values[i].error, e, "%d");
    if (values[i].error == CBOR_ERROR_NONE) {
      if (isnan(values[i].v)) {
        ASSERT(isnan(v));
      }
      else {
        ASSERT_EQ_FMT(values[i].v, v, "%f");
      }
    }
  }
  PASS();
}

#define B(v_) ((const uint8_t*)(v_))
#define C(v_) ((const char*)(v_))
TEST test_bytes(void) {
  struct {
    const uint8_t* v;
    const char* encoded;
    bool canonical;
    cbor_error_t error;
  } values[] = {
    { B(""), "40", true, CBOR_ERROR_NONE },
    { B("00000000000000000000000"),
      "573030303030303030303030303030303030303030303030",
      true, CBOR_ERROR_NONE },
    { B("000000000000000000000000"),
      "5818303030303030303030303030303030303030303030303030",
      true, CBOR_ERROR_NONE },
    { B("000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000"),
      "58ff30303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "3030303030303030303030303030303030303030303030303030",
      true, CBOR_ERROR_NONE },
    { B("000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000"
      "0000000000000000000000000000000000000000000000000000000000"),
      "5901003030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "3030303030303030303030303030303030303030303030303030",
      true, CBOR_ERROR_NONE },
    { B("abcdef"), "5f46616263646566ff", false, CBOR_ERROR_NONE },
    { B("A"), "5f40414140ff", false, CBOR_ERROR_NONE },
    { B(""), "5f40ff", false, CBOR_ERROR_NONE },
    { B(""), "5800", false, CBOR_ERROR_NONE },
    { B(""), "5fff", false, CBOR_ERROR_NONE },
    { B(""), "590000", false, CBOR_ERROR_NONE },
    { B(""), "5a00000000", false, CBOR_ERROR_NONE },
    { B(""), "5b0000000000000000", false, CBOR_ERROR_NONE },
    { B("A"), "580141", false, CBOR_ERROR_NONE },
    { B("B"), "59000142", false, CBOR_ERROR_NONE },
    { B("C"), "5a0000000143", false, CBOR_ERROR_NONE },
    { B("D"), "5b000000000000000144", false, CBOR_ERROR_NONE },
    { NULL, "5c", false, CBOR_ERROR_INVALID_AI },
    { NULL, "5d", false, CBOR_ERROR_INVALID_AI },
    { NULL, "5e", false, CBOR_ERROR_INVALID_AI },
    { NULL, "4241", false, CBOR_ERROR_END_OF_STREAM },
    { NULL, "5f4661626364656642787a", false, CBOR_ERROR_END_OF_STREAM },
    { NULL, "5f6661626364656642787aff", false, CBOR_ERROR_INDEF_MISMATCH },
    { NULL, "5f5f46616263646566ffff", false, CBOR_ERROR_INDEF_NESTING },
  };
  // TODO Need to add some test cases for generating indefinite byte strings
  // including ones with segments

  for (size_t i = 0; i < sizeof(values)/sizeof(*values); i++) {
    cbor_stream_t s;
    cbor_error_t e;
    uint8_t b[300];
    uint8_t encoded[300];
    size_t  encoded_n = dechex(sizeof(encoded), encoded, values[i].encoded);

    if (values[i].canonical) {
      cbor_init(&s, b, sizeof(b));
      e = cbor_write_bytes(&s, values[i].v, strlen(C(values[i].v)));
      ASSERT_EQ_FMT(CBOR_ERROR_NONE, e, "%d");
      ASSERT_EQ_FMT(encoded_n, cbor_read_avail(&s), "%zu");
      ASSERT_MEM_EQ(encoded, b, encoded_n);
    }

    size_t bn = sizeof(b);
    cbor_stream_t s2;
    cbor_init(&s, encoded, encoded_n);
    e = cbor_read_bytes(&s, &s2, &bn);
    ASSERT_EQ_FMT(values[i].error, e, "%d");
    if (values[i].error == CBOR_ERROR_NONE) {
      ASSERT_LT(bn, sizeof(b));
      e = cbor_memmove(b, &s2, bn);
      ASSERT_EQ_FMT(CBOR_ERROR_NONE, e, "%d");
      ASSERT_EQ_FMT(strlen(C(values[i].v)),  bn, "%zu");
      ASSERT_MEM_EQ(values[i].v, b, bn);
    }
  }
  PASS();
}

TEST test_text(void) {
  struct {
    const char* v;
    const char* encoded;
    bool canonical;
    cbor_error_t error;
  } values[] = {
    { "", "60", true, CBOR_ERROR_NONE },
    { "00000000000000000000000",
      "773030303030303030303030303030303030303030303030",
      true, CBOR_ERROR_NONE },
    { "000000000000000000000000",
      "7818303030303030303030303030303030303030303030303030",
      true, CBOR_ERROR_NONE },
    { "000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000",
      "78ff30303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "3030303030303030303030303030303030303030303030303030",
      true, CBOR_ERROR_NONE },
    { "000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000"
      "0000000000000000000000000000000000000000000000000000000000",
      "7901003030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "303030303030303030303030303030303030303030303030303030303030303030"
      "3030303030303030303030303030303030303030303030303030",
      true, CBOR_ERROR_NONE },
    { "abcdefxz©", "7f6661626364656662787a62c2a9ff", false, CBOR_ERROR_NONE },
    { "©", "7f62c2a9ff", false, CBOR_ERROR_NONE },
    { "©", "7f6062c2a960ff", false, CBOR_ERROR_NONE },
    { "", "7f60ff", false, CBOR_ERROR_NONE },
    { "", "7fff", false, CBOR_ERROR_NONE },
    { "", "7800", false, CBOR_ERROR_NONE },
    { "", "790000", false, CBOR_ERROR_NONE },
    { "", "7a00000000", false, CBOR_ERROR_NONE },
    { "", "7b0000000000000000", false, CBOR_ERROR_NONE },
    { "A", "780141", false, CBOR_ERROR_NONE },
    { "B", "79000142", false, CBOR_ERROR_NONE },
    { "C", "7a0000000143", false, CBOR_ERROR_NONE },
    { "D", "7b000000000000000144", false, CBOR_ERROR_NONE },
    { NULL, "7c", false, CBOR_ERROR_INVALID_AI },
    { NULL, "7d", false, CBOR_ERROR_INVALID_AI },
    { NULL, "7e", false, CBOR_ERROR_INVALID_AI },
    { NULL, "6241", false, CBOR_ERROR_END_OF_STREAM },
    { NULL, "61c2", false, CBOR_ERROR_INVALID_UTF8 },
    { NULL, "7f6661626364656662787a62c2a9", false, CBOR_ERROR_END_OF_STREAM },
    { NULL, "7f4661626364656662787a62c2a9ff", false,CBOR_ERROR_INDEF_MISMATCH },
    { NULL, "7f6661626364656662787a61c261a9ff", false,CBOR_ERROR_INVALID_UTF8 },
    { NULL, "7f7f66616263646566ffff", false, CBOR_ERROR_INDEF_NESTING },
  };
  // TODO Need to add some test cases for generating indefinite text strings
  // including ones with segments

  for (size_t i = 0; i < sizeof(values)/sizeof(*values); i++) {
    cbor_stream_t s;
    cbor_error_t e;
    uint8_t b[300];
    char    t[300];
    uint8_t encoded[300];
    size_t  encoded_n = dechex(sizeof(encoded), encoded, values[i].encoded);

    if (values[i].canonical) {
      cbor_init(&s, b, sizeof(b));
      e = cbor_write_text(&s, values[i].v);
      ASSERT_EQ_FMT(CBOR_ERROR_NONE, e, "%d");
      ASSERT_EQ_FMT(encoded_n, cbor_read_avail(&s), "%zu");
      ASSERT_MEM_EQ(encoded, b, encoded_n);
    }

    size_t tn = sizeof(t);
    cbor_stream_t s2;
    cbor_init(&s, encoded, encoded_n);
    e = cbor_read_text(&s, &s2, &tn);
    ASSERT_EQ_FMT(values[i].error, e, "%d");
    if (values[i].error == CBOR_ERROR_NONE) {
      ASSERT_LT(tn, sizeof(t));
      e = cbor_memmove(t, &s2, tn);
      ASSERT_EQ_FMT(CBOR_ERROR_NONE, e, "%d");
      ASSERT_EQ_FMT(strlen(values[i].v),  tn, "%zu");
      ASSERT_MEM_EQ(values[i].v, t, tn);
    }
  }
  PASS();
}

SUITE(the_suite) {
  RUN_TEST(test_int64);
  RUN_TEST(test_uint64);
  RUN_TEST(test_simple);
  RUN_TEST(test_float);
  RUN_TEST(test_bytes);
  RUN_TEST(test_text);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
  GREATEST_MAIN_BEGIN();
  RUN_SUITE(the_suite);
  GREATEST_MAIN_END();        /* display results */
}
