// © 2024 Unit Circle Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <stdarg.h>
#include <errno.h>
#include <math.h>
#include "cbor.h"


static void dump_mem(const char* prefix, uint8_t* b, size_t n) {
  printf("%s", prefix);
  while (n-- > 0) {
    printf("%02x", *b++);
  }
  printf("\n");
}

#define ASSERT(condition, message) \
      do { \
        if (!(condition)) { \
          fprintf(stderr, "[%s:%d] Assert failed in %s(): %s\n", \
              __FILE__, __LINE__, __func__, message); \
          abort(); \
        } \
      } \
      while(0)

#define MAX_VARIABLE_NAME  (32)
#define ERROR_MESSAGE_SIZE (80 + MAX_VARIABLE_NAME + 15)

typedef enum {
  // Structure related tokens
  TOKEN_ARRAY,
  TOKEN_INDEF_ARRAY,
  TOKEN_ARRAY_END,
  TOKEN_MAP,
  TOKEN_INDEF_MAP,
  TOKEN_MAP_END,
  TOKEN_LEFT_PAREN,
  TOKEN_RIGHT_PAREN,
  TOKEN_INDEF_LEFT_PAREN,
  TOKEN_COLON,
  TOKEN_COMMA,
  TOKEN_RAW,

  // Value related tokens
  TOKEN_DECODE_ONLY,
  TOKEN_ERRORV,
  TOKEN_SIMPLE,
  TOKEN_FALSE,
  TOKEN_NULL,
  TOKEN_TRUE,
  TOKEN_UNDEFINED,
  TOKEN_INT,
  TOKEN_UINT,
  TOKEN_DOUBLE,
  TOKEN_TEXT,
  TOKEN_BYTES,
  TOKEN_INDEF_TEXT0,
  TOKEN_INDEF_BYTES0,
  TOKEN_DATETIME,
  TOKEN_ENCODED,
  TOKEN_SELFDESC,
  TOKEN_DECIMAL,
  TOKEN_RATIONAL,

  // Source related tokens
  TOKEN_ERROR,
  TOKEN_EOF
} TokenType;

typedef enum {
  INT64,
  INT32,
  INT16,
  INT8,
  UINT64,
  UINT32,
  UINT16,
  UINT8,
  FLOAT64,
  FLOAT32,
  FLOAT16,
  DATETIME,
  SELFDESC,
  ENCODED,
  DECIMAL,
  RATIONAL,
} CastType;

typedef struct {
  TokenType type;
  const char* start; // pointer to source
  int length;        // length of token
  int line;          // 1-based line #
  union {
    uint64_t uintval;
    int64_t intval;
    float64_t num;
    char* text;
    uint8_t* bytes;
  } value;
  int value_n;
} Token;

typedef struct {
  const char* identifier;
  size_t      length;
  TokenType   tokenType;
} Keyword;

static Keyword keywords[] = {
  { "encoded", 7, TOKEN_ENCODED },
  { "selfdesc", 8, TOKEN_SELFDESC },
  { "decimal", 7, TOKEN_DECIMAL },
  { "rational", 8, TOKEN_RATIONAL },
  { "datetime", 8, TOKEN_DATETIME },
  { "decodeonly", 10, TOKEN_DECODE_ONLY },
  { "simple",    6, TOKEN_SIMPLE },
  { "error",     5, TOKEN_ERRORV },
  { "true",      4, TOKEN_TRUE },
  { "false",     5, TOKEN_FALSE },
  { "null",      4, TOKEN_NULL },
  { "undefined", 9, TOKEN_UNDEFINED },
  { "inf",       3, TOKEN_DOUBLE },
  { "nan",       3, TOKEN_DOUBLE },
  { NULL,        0, TOKEN_EOF }
};

typedef struct {
  const char* identifier;
  size_t      length;
  int         value;
} Enum;

#define ENUM(e_) { #e_, sizeof(#e_)-1, e_ }
static Enum enums[] = {
  // These should match from cbor.h
  ENUM(CBOR_ERROR_NONE),
  ENUM(CBOR_ERROR_END_OF_STREAM),
  ENUM(CBOR_ERROR_INVALID_AI),
  ENUM(CBOR_ERROR_INDEF_MISMATCH),
  ENUM(CBOR_ERROR_INDEF_NESTING),
  ENUM(CBOR_ERROR_INVALID_UTF8),
  ENUM(CBOR_ERROR_BUFFER_TOO_SMALL),
  ENUM(CBOR_ERROR_BAD_TYPE),
  ENUM(CBOR_ERROR_RECURSION),
  ENUM(CBOR_ERROR_MAP_LENGTH),
  ENUM(CBOR_ERROR_BAD_SIMPLE_VALUE),
  ENUM(CBOR_ERROR_UNEXPECTED_BREAK),
  ENUM(CBOR_ERROR_NULL),
  ENUM(CBOR_ERROR_ITEM_TOO_LONG),
  ENUM(CBOR_ERROR_RANGE),
  ENUM(CBOR_ERROR_KEY_NOT_FOUND),
  ENUM(CBOR_ERROR_BAD_DATETIME),
  ENUM(CBOR_ERROR_BAD_DOUBLE),
  ENUM(CBOR_ERROR_BAD_DECIMAL),
  ENUM(CBOR_ERROR_BAD_RATIONAL),
  ENUM(CBOR_ERROR_BAD_ENCODED),
  ENUM(CBOR_ERROR_CANT_CONVERT_TYPE),
  ENUM(CBOR_ERROR_IDX_TOO_BIG),
  ENUM(CBOR_ERROR_FMT),
  ENUM(CBOR_ERROR_ARRAY_TOO_LARGE),
  ENUM(INT64),
  ENUM(INT32),
  ENUM(INT16),
  ENUM(INT8),
  ENUM(UINT64),
  ENUM(UINT32),
  ENUM(UINT16),
  ENUM(UINT8),
  ENUM(FLOAT64),
  ENUM(FLOAT32),
  ENUM(FLOAT16),
  ENUM(DATETIME),
  ENUM(SELFDESC),
  ENUM(ENCODED),
  ENUM(DECIMAL),
  ENUM(RATIONAL),
  { NULL, 0, 0 }
};


typedef struct {
  uint8_t* b;
  size_t   size;
  size_t   used;
} ByteBuffer;

uint64_t next_pow2m1(uint64_t x) {
  x |= x>>1;
  x |= x>>2;
  x |= x>>4;
  x |= x>>8;
  x |= x>>16;
  x |= x>>32;
  return x;
}
static size_t next_pow2(size_t n) {
  uint64_t nm1 = next_pow2m1(n);
  if (nm1 >= 16383) {
    fprintf(stderr, "byteBuffer too large: %zu\n", n);
    exit(2);
  }
  return (size_t) (nm1 + 1);
}

static void byteBufferInit(ByteBuffer* b) {
 b->b = NULL;
 b->size = 0;
 b->used = 0;
}

static void byteBufferFill(ByteBuffer* b, uint8_t v, size_t n) {
  if (b->used + n > b->size) {
    size_t new_size = next_pow2(b->used + n);
    b->b = realloc(b->b, new_size);
    b->size = new_size;
  }
  for (size_t i = 0; i < n; i++) {
    b->b[b->used++] = v;
  }
}

static void byteBufferWrite(ByteBuffer* b, uint8_t v) {
  byteBufferFill(b, v, 1);
}

static void byteBufferWriteN(ByteBuffer* b, const void* p_, size_t n) {
  const uint8_t* p = (const uint8_t*) p_;
  while (n-- > 0) {
    byteBufferFill(b, *p++, 1);
  }
}

static void byteBufferClear(ByteBuffer* b) {
  if (b->b) free(b->b);
  byteBufferInit(b);
}

typedef struct {
  const char* source;
  const char* tokenStart;  // Start of currently-being-lexed token in [source]
  const char* currentChar; // The current character being lexed in [source].
  int currentLine; // The 1-based line number of [currentChar].
  Token current;   // The most recently lexed token.
  Token previous;  // The most recently consumed/advanced token.
  bool hasError;   // If a syntax or compile error has occurred.
  uint8_t* raw;
  size_t   raw_n;
  bool decodeOnly;
  cbor_stream_t encode;
  cbor_stream_t decode;
} Parser;

static void printError(Parser* parser, int line, const char* label,
                       const char* format, va_list args) {
  parser->hasError = true;

  char message[ERROR_MESSAGE_SIZE];
  int length = snprintf(message, sizeof(message), "%s: ", label);
  length += vsnprintf(message + length, sizeof(message)-length,  format, args);
  ASSERT(length < ERROR_MESSAGE_SIZE, "Error should not exceed buffer.");
  fprintf(stderr, "%d: %s\n", line, message);
}

static void lexError(Parser* parser, const char* format, ...) {
  va_list args;
  va_start(args, format);
  printError(parser, parser->currentLine, "Error", format, args);
  va_end(args);
}

static void error(Parser* parser, const char* format, ...) {
  Token* token = &parser->previous;

  // The lexer has already reported it.
  if (token->type == TOKEN_ERROR) return;

  va_list args;
  va_start(args, format);
  if (token->type == TOKEN_EOF) {
    printError(parser, token->line, "Error at end of file", format, args);
  }
  else {
    // Make sure we don't exceed the buffer with a very long token.
    char label[10 + MAX_VARIABLE_NAME + 4 + 1];
    int n = token->length;
    if (n > MAX_VARIABLE_NAME) n = MAX_VARIABLE_NAME;
    snprintf(label, sizeof(label), "Error at '%.*s'", n, token->start);
    printError(parser, token->line, label, format, args);
  }
  va_end(args);
  exit(1);
}

// Returns true if [c] is a valid (non-initial) identifier character.
static bool isName(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// Returns true if [c] is a digit.
static bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

// Returns the current character the parser is sitting on.
static char peekChar(Parser* parser) {
  return *parser->currentChar;
}

// Returns the character after the current character.
static char peekNextChar(Parser* parser) {
  // If we're at the end of the source, don't read past it.
  if (peekChar(parser) == '\0') return '\0';
  return *(parser->currentChar + 1);
}

// Advances the parser forward one character.
static char nextChar(Parser* parser) {
  char c = peekChar(parser);
  parser->currentChar++;
  if (c == '\n') parser->currentLine++;
  return c;
}

// If the current character is [c], consumes it and returns `true`.
static bool matchChar(Parser* parser, char c) {
  if (peekChar(parser) != c) return false;
  nextChar(parser);
  return true;
}

// Sets the parser's current token to the given [type] and current character
// range.
static void makeToken(Parser* parser, TokenType type) {
  parser->current.type = type;
  parser->current.start = parser->tokenStart;
  parser->current.length = (int)(parser->currentChar - parser->tokenStart);
  parser->current.line = parser->currentLine;
}

// If the current character is [c], then consumes it and makes a token of type
// [two]. Otherwise makes a token of type [one].
static void twoCharToken(Parser* parser, char c, TokenType two, TokenType one) {
  makeToken(parser, matchChar(parser, c) ? two : one);
}

// Skips the rest of the current line.
static void skipLineComment(Parser* parser) {
  while (peekChar(parser) != '\n' && peekChar(parser) != '\0') {
    nextChar(parser);
  }
}

// Skips the rest of a block comment.
static void skipBlockComment(Parser* parser) {
  int nesting = 1;
  while (nesting > 0) {
    if (peekChar(parser) == '\0') {
      lexError(parser, "Unterminated block comment.");
      return;
    }

    if (peekChar(parser) == '/' && peekNextChar(parser) == '*') {
      nextChar(parser);
      nextChar(parser);
      nesting++;
      continue;
    }

    if (peekChar(parser) == '*' && peekNextChar(parser) == '/') {
      nextChar(parser);
      nextChar(parser);
      nesting--;
      continue;
    }

    // Regular comment character.
    nextChar(parser);
  }
}

// Reads the next character, which should be a hex digit (0-9, a-f, or A-F) and
// returns its numeric value. If the character isn't a hex digit, returns -1.
static int readHexDigit(Parser* parser) {
  char c = nextChar(parser);
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;

  // Don't consume it if it isn't expected. Keeps us from reading past the end
  // of an unterminated string.
  parser->currentChar--;
  return -1;
}

// Parses the numeric value of the current token.
static void makeNumber(Parser* parser, bool isHex) {
  char* end;
  bool neg = *parser->tokenStart == '-';
  size_t n = (int)(parser->currentChar - parser->tokenStart);
  bool uint = parser->tokenStart[n-1] == 'u';
  if (neg) {
    errno = 0;
    parser->current.value.intval = strtoll(parser->tokenStart, &end, 10);
    if ((errno == 0) && ((end - parser->tokenStart) == n)) {
      makeToken(parser, TOKEN_INT);
      return;
    }
  }
  else {
    errno = 0;
    if (uint) {
      parser->current.value.uintval = strtoull(parser->tokenStart, &end,
        isHex ? 16:10);
      n -= 1;
    }
    else {
      parser->current.value.intval = strtoull(parser->tokenStart, &end,
        isHex ? 16:10);
    }

    if ((errno == 0) && ((end - parser->tokenStart) == n)) {
      makeToken(parser, uint ? TOKEN_UINT : TOKEN_INT);
      return;
    }
  }
  errno = 0;
  parser->current.value.num = strtod(parser->tokenStart, &end);
  if ((errno == 0) && ((end - parser->tokenStart) == n)) {
    makeToken(parser, TOKEN_DOUBLE);
    return;
  }
  lexError(parser, "Unable to parse number literal.");
}

// Finishes lexing a number literal.
static void readNumber(Parser* parser) {
  matchChar(parser, '-');  // Match optional - sign
  if (matchChar(parser, 'i') &&
      matchChar(parser, 'n') &&
      matchChar(parser, 'f')) {
    makeNumber(parser, false);
    return;
  }

  while (isDigit(peekChar(parser))) nextChar(parser);

  // See if trailing u to indicate unsigned value
  if (matchChar(parser, 'u')) {
    // check that there is no leading -
    if (*parser->tokenStart == '-') {
      lexError(parser, "unsigned integers can't start with -");
    }
    else {
      makeNumber(parser, false);
    }
    return;
  }

  // See if it has a floating point. Make sure there is a digit after the "."
  // so we don't get confused by method calls on number literals.
  if (peekChar(parser) == '.' && isDigit(peekNextChar(parser))) {
    nextChar(parser);
    while (isDigit(peekChar(parser))) nextChar(parser);
  }

  // See if the number is in scientific notation.
  if (matchChar(parser, 'e') || matchChar(parser, 'E')) {
    // Allow a negative exponent.
    matchChar(parser, '-');

    if (!isDigit(peekChar(parser))) {
      lexError(parser, "Unterminated scientific notation.");
    }

    while (isDigit(peekChar(parser))) nextChar(parser);
  }

  makeNumber(parser, false);
}

// Finishes lexing an identifier. Handles reserved words.
static void readName(Parser* parser) {
  while (isName(peekChar(parser)) || isDigit(peekChar(parser))) {
    nextChar(parser);
  }

  // Update the type if it's a keyword.
  size_t length = parser->currentChar - parser->tokenStart;
  for (int i = 0; keywords[i].identifier != NULL; i++) {
    if (length == keywords[i].length &&
        memcmp(parser->tokenStart, keywords[i].identifier, length) == 0) {

      if (keywords[i].tokenType == TOKEN_DOUBLE) {
        makeNumber(parser, false);
      }
      else {
        makeToken(parser, keywords[i].tokenType);
      }
      return;
    }
  }
  for (int i = 0; enums[i].identifier != NULL; i++) {
    if (length == enums[i].length &&
      memcmp(parser->tokenStart, enums[i].identifier, length) == 0) {
      parser->current.value.intval = enums[i].value;
      makeToken(parser, TOKEN_INT);
      return;
    }
  }
  fprintf(stderr, "Unknown keyword %*s\n", (int) length, parser->tokenStart);
  exit(1);
}

// Reads [digits] hex digits in a string literal and returns their number value.
static int readHex(Parser* parser, int digits, const char* description) {
  int value = 0;
  for (int i = 0; i < digits; i++) {
    int digit = readHexDigit(parser);
    if (digit == -1) {
      lexError(parser, "Invalid %s escape sequence.", description);
      break;
    }

    value = (value * 16) | digit;
  }

  return value;
}

static int utf8EncodeNumBytes(int value) {
  ASSERT(value >= 0, "Cannot encode a negative value.");

  if (value <= 0x7f) return 1;
  if (value <= 0x7ff) return 2;
  if (value <= 0xffff) return 3;
  if (value <= 0x10ffff) return 4;
  return 0;
}

int utf8Encode(int value, uint8_t* bytes) {
  if (value <= 0x7f) {
    // Single byte (i.e. fits in ASCII).
    *bytes = value & 0x7f;
    return 1;
  }
  else if (value <= 0x7ff) {
    // Two byte sequence: 110xxxxx 10xxxxxx.
    *bytes = 0xc0 | ((value & 0x7c0) >> 6);
    bytes++;
    *bytes = 0x80 | (value & 0x3f);
    return 2;
  }
  else if (value <= 0xffff) {
    // Three byte sequence: 1110xxxx 10xxxxxx 10xxxxxx.
    *bytes = 0xe0 | ((value & 0xf000) >> 12);
    bytes++;
    *bytes = 0x80 | ((value & 0xfc0) >> 6);
    bytes++;
    *bytes = 0x80 | (value & 0x3f);
    return 3;
  }
  else if (value <= 0x10ffff) {
    // Four byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx.
    *bytes = 0xf0 | ((value & 0x1c0000) >> 18);
    bytes++;
    *bytes = 0x80 | ((value & 0x3f000) >> 12);
    bytes++;
    *bytes = 0x80 | ((value & 0xfc0) >> 6);
    bytes++;
    *bytes = 0x80 | (value & 0x3f);
    return 4;
  }
  // Invalid Unicode value. See: http://tools.ietf.org/html/rfc3629
  fprintf(stderr, "Invalid utf8 value %d\n", value);
  exit(1);
}


// Reads a hex digit Unicode escape sequence in a string literal.
static void readUnicodeEscape(Parser* parser, ByteBuffer* string, int length) {
  int value = readHex(parser, length, "Unicode");

  // Grow the buffer enough for the encoded result.
  int numBytes = utf8EncodeNumBytes(value);
  if (numBytes != 0) {
    byteBufferFill(string, 0, numBytes);
    utf8Encode(value, string->b + string->used - numBytes);
  }
}

// Finishes lexing a string literal.
static void readText(Parser* parser) {
  ByteBuffer string;
  byteBufferInit(&string);

  for (;;) {
    char c = nextChar(parser);
    if (c == '"') break;

    if (c == '\0') {
      lexError(parser, "Unterminated string.");

      // Don't consume it if it isn't expected. Keeps us from reading past the
      // end of an unterminated string.
      parser->currentChar--;
      break;
    }

    if (c == '\\') {
      switch (nextChar(parser)) {
        case '"':  byteBufferWrite(&string, '"'); break;
        case '\\': byteBufferWrite(&string, '\\'); break;
        case '%':  byteBufferWrite(&string, '%'); break;
        case '0':  byteBufferWrite(&string, '\0'); break;
        case 'a':  byteBufferWrite(&string, '\a'); break;
        case 'b':  byteBufferWrite(&string, '\b'); break;
        case 'f':  byteBufferWrite(&string, '\f'); break;
        case 'n':  byteBufferWrite(&string, '\n'); break;
        case 'r':  byteBufferWrite(&string, '\r'); break;
        case 't':  byteBufferWrite(&string, '\t'); break;
        case 'u':  readUnicodeEscape(parser, &string, 4); break;
        case 'U':  readUnicodeEscape(parser, &string, 8); break;
        case 'v':  byteBufferWrite(&string, '\v'); break;
        case 'x':
          byteBufferWrite(&string, (uint8_t)readHex(parser, 2, "byte"));
          break;

        default:
          lexError(parser, "Invalid escape character '%c'.",
                   *(parser->currentChar - 1));
          break;
      }
    }
    else {
      byteBufferWrite(&string, c);
    }
  }
  if (matchChar(parser, '_')) {
    makeToken(parser, TOKEN_INDEF_TEXT0);
  }
  else {
    parser->current.value.text = malloc(string.used);
    parser->current.value_n = string.used;
    if (parser->current.value.text == NULL) {
      error(parser, "readText out of memory");
      return;
    }
    memmove(parser->current.value.text, string.b, string.used);
    byteBufferClear(&string);
    makeToken(parser, TOKEN_TEXT);
  }
}

static void readBytes(Parser* parser) {
  ByteBuffer string;
  byteBufferInit(&string);

  for (;;) {
    if (matchChar(parser, '\'')) break;
    if (matchChar(parser, ' ')) continue;
    if (matchChar(parser, '\t')) continue;
    int c = readHex(parser, 2, "Bytes");
    byteBufferWrite(&string, (uint8_t) c);
  }
  if (matchChar(parser, '_')) {
    makeToken(parser, TOKEN_INDEF_BYTES0);
  }
  else {
    parser->current.value.bytes = malloc(string.used);
    parser->current.value_n = string.used;
    if (parser->current.value.bytes == NULL) {
      error(parser, "readBytes out of memory");
      return;
    }
    memmove(parser->current.value.bytes, string.b, string.used);
    byteBufferClear(&string);
    makeToken(parser, TOKEN_BYTES);
  }
}

static void readRaw(Parser* parser) {
  ByteBuffer string;
  byteBufferInit(&string);

  for (;;) {
    if (matchChar(parser, '|')) break;
    if (matchChar(parser, ' ')) continue;
    if (matchChar(parser, '\t')) continue;
    if (matchChar(parser, '\r')) continue;
    if (matchChar(parser, '\n')) continue;
    int c = readHex(parser, 2, "Bytes");
    byteBufferWrite(&string, (uint8_t) c);
  }

  parser->current.value.bytes = malloc(string.used);
  parser->current.value_n = string.used;
  if (parser->current.value.bytes == NULL) {
    error(parser, "readRaw out of memory");
    return;
  }
  memmove(parser->current.value.bytes, string.b, string.used);
  byteBufferClear(&string);
  makeToken(parser, TOKEN_RAW);
}

// Lex the next token and store it in [parser.current].
static void nextToken(Parser* parser) {
  parser->previous = parser->current;

  // If we are out of tokens, don't try to tokenize any more. We *do* still
  // copy the TOKEN_EOF to previous so that code that expects it to be consumed
  // will still work.
  if (parser->current.type == TOKEN_EOF) return;

  while (peekChar(parser) != '\0') {
    parser->tokenStart = parser->currentChar;

    char c = nextChar(parser);
    switch (c) {
      case '(':
        twoCharToken(parser, '_', TOKEN_INDEF_LEFT_PAREN, TOKEN_LEFT_PAREN);
        return;
      case ')': makeToken(parser, TOKEN_RIGHT_PAREN); return;
      case '[':
        twoCharToken(parser, '_', TOKEN_INDEF_ARRAY, TOKEN_ARRAY);
        return;
      case ']': makeToken(parser, TOKEN_ARRAY_END); return;
      case '{':
        twoCharToken(parser, '_', TOKEN_INDEF_MAP, TOKEN_MAP);
        return;
      case '}': makeToken(parser, TOKEN_MAP_END); return;
      case ':': makeToken(parser, TOKEN_COLON); return;
      case ',': makeToken(parser, TOKEN_COMMA); return;

      case '|': readRaw(parser); return;

      case '/':
        if (matchChar(parser, '/')) {
          skipLineComment(parser);
          break;
        }

        if (matchChar(parser, '*')) {
          skipBlockComment(parser);
          break;
        }
        lexError(parser, "Unexpected character '%c'.", peekChar(parser));
        parser->current.type = TOKEN_ERROR;
        parser->current.length = 0;
        return;

      case ' ':
      case '\r':
      case '\t':
      case '\n':
        // Skip forward until we run out of whitespace.
        while (peekChar(parser) == ' ' ||
               peekChar(parser) == '\r' ||
               peekChar(parser) == '\n' ||
               peekChar(parser) == '\t') {
          nextChar(parser);
        }
        break;

      case '"': readText(parser); return;
      case '\'': readBytes(parser); return;
      default:
        if (isDigit(c) || (c == '-')) {
          readNumber(parser);
        }
        else if (isName(c)) {
          readName(parser);
        }
        else {
          if (c >= 32 && c <= 126) {
            lexError(parser, "Invalid character '%c'.", c);
          }
          else {
            // Don't show non-ASCII values since we didn't UTF-8 decode the
            // bytes. Since there are no non-ASCII byte values that are
            // meaningful code units in Wren, the lexer works on raw bytes,
            // even though the source code and console output are UTF-8.
            lexError(parser, "Invalid byte 0x%x.", (uint8_t)c);
          }
          parser->current.type = TOKEN_ERROR;
          parser->current.length = 0;
        }
        return;
    }
  }

  // If we get here, we're out of source, so just make EOF tokens.
  parser->tokenStart = parser->currentChar;
  makeToken(parser, TOKEN_EOF);
}

static const char* tokenTypeToString(TokenType t) {
  switch (t) {
    case TOKEN_ARRAY:            return "[";
    case TOKEN_INDEF_ARRAY:      return "[_";
    case TOKEN_ARRAY_END:        return "]";
    case TOKEN_MAP:              return "{";
    case TOKEN_INDEF_MAP:        return "{_";
    case TOKEN_MAP_END:          return "}";
    case TOKEN_COLON:            return ":";
    case TOKEN_COMMA:            return ",";
    case TOKEN_LEFT_PAREN:       return "(";
    case TOKEN_INDEF_LEFT_PAREN: return "(_";
    case TOKEN_RIGHT_PAREN:      return ")";
    case TOKEN_RAW:              return "|";

    case TOKEN_FALSE:            return "false";
    case TOKEN_NULL:             return "null";
    case TOKEN_TRUE:             return "true";
    case TOKEN_UNDEFINED:        return "undefined";
    case TOKEN_SIMPLE:           return "simple";
    case TOKEN_INT:              return "^int^";
    case TOKEN_UINT:             return "^uint^";
    case TOKEN_DOUBLE:           return "^float64^";
    case TOKEN_TEXT:             return "^text^";
    case TOKEN_BYTES:            return "^bytes^";

    case TOKEN_ERROR:            return "^error^";
    case TOKEN_EOF:              return "^eof^";
    default:
      fprintf(stderr, "Unknown TokenType %d.", t);
      exit(1);
  }
}

static void consume(Parser* p, TokenType expected, const char* errorMessage) {
  nextToken(p);
  if (p->previous.type != expected) {
    error(p, errorMessage);
  }
}

static void parse_raw(Parser* p) {
  consume(p, TOKEN_RAW, "missing raw value");
  p->raw = p->previous.value.bytes;
  p->raw_n = p->previous.value_n;
}

static void check(Parser* p, cbor_error_t e, const char* msg) {
  if (e != CBOR_ERROR_NONE) {
    printf("Error %d\n", e);
    error(p, msg);
  }
}

static void parse_value(Parser* p) {
  switch (p->current.type) {
    case TOKEN_FALSE: {
      bool v;
      check(p, cbor_write_bool(&p->encode, false), "cbor_write_bool");
      check(p, cbor_read_bool(&p->decode, &v), "cbor_read_bool");
      if (v != false) error(p, "reading false != true");
      nextToken(p);
      break;
    }
    case TOKEN_TRUE: {
      bool v;
      check(p, cbor_write_bool(&p->encode, true), "cbor_write_bool");
      check(p, cbor_read_bool(&p->decode, &v), "cbor_read_bool");
      if (v != true) error(p, "reading true != false");
      nextToken(p);
      break;
    }
    case TOKEN_NULL:
      check(p, cbor_write_null(&p->encode), "cbor_write_null");
      check(p, cbor_read_null(&p->decode), "cbor_read_null");
      nextToken(p);
      break;
    case TOKEN_UNDEFINED:
      check(p, cbor_write_undefined(&p->encode), "cbor_write_undefined");
      check(p, cbor_read_undefined(&p->decode), "cbor_read_undefined");
      nextToken(p);
      break;
    case TOKEN_UINT: {
      uint64_t v;
      cbor_stream_t s;
      nextToken(p);
      if (p->current.type == TOKEN_LEFT_PAREN) {
        check(p, cbor_write_tag(&p->encode, p->previous.value.uintval),
                "cbor_write_tag");
        check(p, cbor_read_tag(&p->decode, &s, &v), "cbor_read_tag");
        if (v != p->previous.value.uintval) {
          error(p, "reading %lld != %lld", p->previous.value.uintval, v);
        }

        // push
        cbor_stream_t save_s = p->decode;
        p->decode = s;

        consume(p, TOKEN_LEFT_PAREN, "missing (");
        parse_value(p);
        consume(p, TOKEN_RIGHT_PAREN, "missing )");

        // pop
        p->decode = save_s;
      }
      else {
        check(p, cbor_write_uint64(&p->encode, p->previous.value.uintval),
                "cbor_write_uint64");
        check(p, cbor_read_uint64(&p->decode, &v), "cbor_read_uint64");
        if (v != p->previous.value.uintval) {
          error(p, "reading %lld != %lld", p->previous.value.uintval, v);
        }
      }
      break;
    }
    case TOKEN_INT: {
      // Currently only range [-2^63, 2^63)
      int64_t v;
      nextToken(p);
      check(p, cbor_write_int64(&p->encode, p->previous.value.intval),
               "cbor_write_int64");
      check(p, cbor_read_int64(&p->decode, &v), "cbor_read_int64");
      if (v != p->previous.value.intval) {
        error(p, "reading %lld != %lld", p->previous.value.intval, v);
      }
      break;
    }
    case TOKEN_DOUBLE: {
      float64_t v;
      nextToken(p);
      check(p, cbor_write_float64(&p->encode, p->previous.value.num),
              "cbor_write_float64");
      check(p, cbor_read_float64(&p->decode, &v), "cbor_read_float64");
      if (isnan(p->previous.value.num)) {
        if (!isnan(v)) {
          error(p, "reading %f != %f", p->previous.value.num, v);
        }
      }
      else if (v != p->previous.value.num) {
        error(p, "reading %f != %f", p->previous.value.num, v);
      }
      break;
    }
    case TOKEN_TEXT: {
      size_t n;
      cbor_stream_t s;
      nextToken(p);
      check(p, cbor_write_textn(&p->encode, p->previous.value.text,
              p->previous.value_n), "cbor_write_textn");
      check(p, cbor_read_text(&p->decode, &s, &n), "cbor_read_text");
      if (n != p->previous.value_n) {
        error(p, "reading text lengths %zu != %zu", p->previous.value_n, n);
      }
      check(p, cbor_memcmp(p->previous.value.text, &s, n), "cbor_memcmp");
      break;
    }
    case TOKEN_BYTES: {
      size_t n;
      cbor_stream_t s;
      nextToken(p);
      check(p, cbor_write_bytes(&p->encode, p->previous.value.bytes,
              p->previous.value_n), "cbor_write_bytes");
      check(p, cbor_read_bytes(&p->decode, &s, &n), "cbor_read_bytes");
      if (n != p->previous.value_n) {
        error(p, "reading text lengths %zu != %zu", p->previous.value_n, n);
      }
      check(p, cbor_memcmp(p->previous.value.bytes, &s, n), "cbor_memcmp");
      break;
    }
    case TOKEN_SIMPLE: {
      uint8_t v;
      nextToken(p);
      consume(p, TOKEN_LEFT_PAREN, "missing (");
      if (p->current.type == TOKEN_INT) {
        if (p->current.value.intval >= 256) {
          error(p, "invalid simple value %llu", p->current.value.intval);
        }
        check(p, cbor_write_simple(&p->encode, (uint8_t) p->current.value.intval),
                "cbor_write_simple");
        check(p, cbor_read_simple(&p->decode, &v), "cbor_read_simple");
        if (v != p->current.value.intval) {
          error(p, "reading simple %u != %llu", v, p->current.value.intval);
        }
        nextToken(p);
        consume(p, TOKEN_RIGHT_PAREN, "missing )");
      }
      else {
        error(p, "simple value type not int");
      }
      break;
    }

    case TOKEN_DATETIME: {
      float64_t v;
      float64_t exp_v;
      nextToken(p);
      consume(p, TOKEN_LEFT_PAREN, "missing (");
      if (p->current.type == TOKEN_INT) {
        exp_v = (float64_t) p->current.value.intval;
        check(p, cbor_write_datetime(&p->encode, exp_v), "cbor_write_datetime");
        //check(p, cbor_write_tag(&p->encode, 1), "cbor_write_tag");
        //check(p, cbor_write_int64(&p->encode, exp_v),
        //          "cbor_write_datetime");
        check(p, cbor_read_datetime(&p->decode, &v), "cbor_read_datetime");
        if (v != exp_v) {
          error(p, "reading datetime %f != %f", v, exp_v);
        }
      }
      else if (p->current.type == TOKEN_DOUBLE) {
        exp_v = p->current.value.num;
        check(p, cbor_write_datetime(&p->encode, exp_v), "cbor_write_datetime");
        check(p, cbor_read_datetime(&p->decode, &v), "cbor_read_datetime");
        if (v != exp_v) {
          error(p, "reading datetime %f != %f", v, exp_v);
        }
      }
      else if (p->current.type == TOKEN_TEXT) {
        cbor_stream_t s;
        cbor_stream_t s2;
        uint64_t v;
        size_t n;
        check(p, cbor_write_tag(&p->encode, 0), "cbor_write_tag");
        check(p, cbor_write_textn(&p->encode, p->current.value.text,
                p->current.value_n), "cbor_write_text");
        cbor_stream_t s3 = p->decode;

        check(p, cbor_read_tag(&p->decode, &s, &v), "cbor_read_tag");
        if (v != 0) {
          error(p, "reading datetime-string tag %llu != 0", v);
        }
        check(p, cbor_read_text(&s, &s2, &n), "cbor_read_text");
        if (n !=  p->current.value_n) {
          error(p, "reading datetime-string len %zu != %zu", n, p->current.value_n);
        }
        if (cbor_memcmp(p->current.value.text, &s2, n) != 0) {
          error(p, "reading datetime-string values don't match");
        }

        nextToken(p);
        consume(p, TOKEN_COMMA, "missing ,");
        if (p->current.type != TOKEN_DOUBLE) {
          error(p, "missing dobule value");
        }
        float64_t v2;
        p->decode = s3;
        exp_v = p->current.value.num;
        check(p, cbor_read_datetime(&p->decode, &v2), "cbor_read_datetime");
        if (v2 != exp_v) {
          error(p, "reading datetime %f != %f", v2, exp_v);
        }
      }
      else {
        error(p, "datetime value not int, float64_t or string");
      }
      nextToken(p);
      consume(p, TOKEN_RIGHT_PAREN, "missing )");
      break;
    }

    case TOKEN_ENCODED: {
      size_t n;
      cbor_stream_t s;
      nextToken(p);
      consume(p, TOKEN_LEFT_PAREN, "mssing (");
      if (p->current.type != TOKEN_BYTES) {
        error(p, "expected bytes");
      }
      check(p, cbor_write_encoded(&p->encode, p->current.value.bytes,
              p->current.value_n), "cbor_write_encoded");
      check(p, cbor_read_encoded(&p->decode, &s, &n), "cbor_read_encoded");
      if (n != p->current.value_n) {
        error(p, "reading bytes lengths %zu != %zu", p->current.value_n, n);
      }
      dump_mem("exp:", p->current.value.bytes, n);
      dump_mem("dec:", s.b, n);
      check(p, cbor_memcmp(p->current.value.bytes, &s, n), "cbor_memcmp");
      nextToken(p);
      consume(p, TOKEN_RIGHT_PAREN, "missing )");
      break;
    }

    case TOKEN_SELFDESC: {
      cbor_stream_t s;
      nextToken(p);
      check(p, cbor_write_selfdesc(&p->encode), "cbor_write_selfdesc");
      check(p, cbor_read_selfdesc(&p->decode, &s), "cbor_read_selfdesc");

      // push
      cbor_stream_t save_s = p->decode;
      p->decode = s;

      consume(p, TOKEN_LEFT_PAREN, "missing (");
      parse_value(p);
      consume(p, TOKEN_RIGHT_PAREN, "missing )");

      // pop
      p->decode = save_s;
      break;
    }

    case TOKEN_DECIMAL: {
      int64_t exp;
      int64_t mant;
      int64_t r_exp;
      int64_t r_mant;

      nextToken(p);
      consume(p, TOKEN_LEFT_PAREN, "mssing (");
      if (p->current.type != TOKEN_INT) {
        error(p, "expected int");
      }
      exp = p->current.value.intval;
      nextToken(p);
      consume(p, TOKEN_COMMA, "missing ,");
      if (p->current.type != TOKEN_INT) {
        error(p, "expected int");
      }
      mant = p->current.value.intval;

      check(p, cbor_write_decimal(&p->encode, mant, exp), "cbor_write_decimal");
      check(p, cbor_read_decimal(&p->decode, &r_mant, &r_exp), "cbor_read_decimal");
      if ((exp != r_exp) || (mant != r_mant)) {
        error(p, "exps/mants don't match");
      }
      nextToken(p);
      consume(p, TOKEN_RIGHT_PAREN, "missing )");
      break;
    }

    case TOKEN_RATIONAL: {
      int64_t num;
      uint64_t denom;
      int64_t r_num;
      uint64_t r_denom;

      nextToken(p);
      consume(p, TOKEN_LEFT_PAREN, "mssing (");
      if (p->current.type != TOKEN_INT) {
        error(p, "expected int");
      }
      num = p->current.value.intval;
      nextToken(p);
      consume(p, TOKEN_COMMA, "missing ,");
      if (p->current.type != TOKEN_UINT) {
        error(p, "expected int");
      }
      denom = p->current.value.intval;

      check(p, cbor_write_rational(&p->encode, num, denom), "cbor_write_rational");
      check(p, cbor_read_rational(&p->decode, &r_num, &r_denom), "cbor_read_rational");
      if ((num != r_num) || (denom != r_denom)) {
        error(p, "num/denoms don't match");
      }
      nextToken(p);
      consume(p, TOKEN_RIGHT_PAREN, "missing )");
      break;
    }

    case TOKEN_INDEF_TEXT0: {
      size_t n;
      cbor_stream_t s;

      nextToken(p);
      check(p, cbor_write_text_start(&p->encode), "cbor_write_text_start");
      check(p, cbor_write_end(&p->encode), "cbor_write_end");
      check(p, cbor_read_text(&p->decode, &s, &n), "cbor_read_text");
      if (0 != n) {
        error(p, "length mismatch %zu != %zu", 0, n);
      }
      break;
    }

    case TOKEN_INDEF_BYTES0: {
      size_t n;
      cbor_stream_t s;

      nextToken(p);
      check(p, cbor_write_bytes_start(&p->encode), "cbor_write_bytes_start");
      check(p, cbor_write_end(&p->encode), "cbor_write_end");
      check(p, cbor_read_bytes(&p->decode, &s, &n), "cbor_read_bytes");
      if (0 != n) {
        error(p, "length mismatch %zu != %zu", 0, n);
      }
      break;
    }

    case TOKEN_DECODE_ONLY: {
      nextToken(p);
      consume(p, TOKEN_LEFT_PAREN, "missing (");
      parse_value(p);
      consume(p, TOKEN_RIGHT_PAREN, "missing )");

      p->decodeOnly = true;
      break;
    }

    case TOKEN_ERRORV: {
      uint8_t v;
      nextToken(p);
      consume(p, TOKEN_LEFT_PAREN, "missing (");
      if (p->current.type == TOKEN_INT) {
        cbor_value_t v;
        cbor_error_t exp_e = p->current.value.intval;
        cbor_error_t e;
        nextToken(p);


        if (p->current.type == TOKEN_COMMA) {
          nextToken(p);
          if (p->current.type == TOKEN_INT) {
            int64_t  v_int64;
            int32_t  v_int32;
            int16_t  v_int16;
            int8_t   v_int8;
            uint64_t v_uint64;
            uint32_t v_uint32;
            uint16_t v_uint16;
            uint8_t  v_uint8;
            float64_t   v_float64;
            float32_t    v_float32;
            float16_t   v_float16;
            int64_t    v_num;
            uint64_t    v_denom;
            int64_t    v_exp;
            int64_t    v_mant;
            cbor_stream_t v_s;
            size_t        v_n;
            switch (p->current.value.intval) {
              case INT64:
                e = cbor_read_int64(&p->decode, &v_int64);
                break;
              case INT32:
                e = cbor_read_int32(&p->decode, &v_int32);
                break;
              case INT16:
                e = cbor_read_int16(&p->decode, &v_int16);
                break;
              case INT8:
                e = cbor_read_int8(&p->decode, &v_int8);
                break;
              case UINT64:
                e = cbor_read_uint64(&p->decode, &v_uint64);
                break;
              case UINT32:
                e = cbor_read_uint32(&p->decode, &v_uint32);
                break;
              case UINT16:
                e = cbor_read_uint16(&p->decode, &v_uint16);
                break;
              case UINT8:
                e = cbor_read_uint8(&p->decode, &v_uint8);
                break;
              case FLOAT64:
                e = cbor_read_float64(&p->decode, &v_float64);
                break;
              case FLOAT32:
                e = cbor_read_float32(&p->decode, &v_float32);
                break;
              case FLOAT16:
                e = cbor_read_float16(&p->decode, &v_float16);
                break;
              case DATETIME:
                e = cbor_read_datetime(&p->decode, &v_float64);
                break;
              case ENCODED:
                e = cbor_read_encoded(&p->decode, &v_s, &v_n);
                break;
              case SELFDESC:
                e = cbor_read_selfdesc(&p->decode, &v_s);
                break;
              case DECIMAL:
                e = cbor_read_decimal(&p->decode, &v_exp, &v_mant);
                break;
              case RATIONAL:
                e = cbor_read_rational(&p->decode, &v_num, &v_denom);
                break;
              default:
                error(p, "unknown function type");
                break;
            }
          }
          else {
            error(p, "error value func type not int");
          }
          nextToken(p);
        }
        else {
           e = cbor_read_any(&p->decode, &v);
           printf("read_any e %d exp_e %d\n", e, exp_e);
        }
        if (e != exp_e) {
          error(p, "errors dont' match %d != %d\n", e, exp_e);
        }

        consume(p, TOKEN_RIGHT_PAREN, "missing )");
      }
      else {
        error(p, "error value type not int");
      }
      p->decodeOnly = true;
      break;
    }

    case TOKEN_ARRAY:
    case TOKEN_INDEF_ARRAY: {
      size_t n;
      cbor_stream_t s;
      nextToken(p);
      bool indef = p->previous.type != TOKEN_ARRAY;

      check(p, cbor_read_array(&p->decode, &s, &n), "cbor_read_array");
      if (indef) {
        check(p, cbor_write_array_start(&p->encode), "cbor_write_array_start");
      }
      else {
        check(p, cbor_write_array(&p->encode, n), "cbor_write_array");
      }

      // push
      cbor_stream_t save_s = p->decode;
      p->decode = s;

      if (n > 0) {
        parse_value(p);
        for (size_t i = 1; i < n; i++) {
          consume(p, TOKEN_COMMA, "missing ,");
          parse_value(p);
        }
      }
      consume(p, TOKEN_ARRAY_END, "missing ]");

      // pop
      p->decode = save_s;

      if (indef) {
        check(p, cbor_write_end(&p->encode), "cbor_write_end");
      }
      break;
    }

    case TOKEN_MAP:
    case TOKEN_INDEF_MAP: {
      size_t n;
      cbor_stream_t s;
      nextToken(p);
      bool indef = p->previous.type != TOKEN_MAP;

      check(p, cbor_read_map(&p->decode, &s, &n), "cbor_read_map");
      if (indef) {
        check(p, cbor_write_map_start(&p->encode), "cbor_write_map_start");
      }
      else {
        check(p, cbor_write_map(&p->encode, n), "cbor_write_map");
      }

      // push
      cbor_stream_t save_s = p->decode;
      p->decode = s;

      if (n > 0) {
        parse_value(p);
        consume(p, TOKEN_COLON, "missing :");
        parse_value(p);
        for (size_t i = 1; i < n; i++) {
          consume(p, TOKEN_COMMA, "missing ,");
          parse_value(p);
          consume(p, TOKEN_COLON, "missing :");
          parse_value(p);
        }
      }
      consume(p, TOKEN_MAP_END, "missing }");

      // pop
      p->decode = save_s;

      if (indef) {
        check(p, cbor_write_end(&p->encode), "cbor_write_end");
      }
      break;
    }

    case TOKEN_INDEF_LEFT_PAREN: {
      size_t n;
      cbor_stream_t s;
      ByteBuffer string;

      byteBufferInit(&string);
      nextToken(p);
      TokenType t = p->current.type;
      switch (t) {
        case TOKEN_TEXT:
          check(p, cbor_write_text_start(&p->encode), "cbor_write_text_start");
          check(p, cbor_write_textn(&p->encode, p->current.value.text,
                p->current.value_n), "cbor_write_text");
          check(p, cbor_read_text(&p->decode, &s, &n), "cbor_read_text");
          byteBufferWriteN(&string, p->current.value.text, p->current.value_n);
          break;
        case TOKEN_BYTES:
          check(p, cbor_write_bytes_start(&p->encode), "cbor_write_bytes_start");
          check(p, cbor_write_bytes(&p->encode, p->current.value.bytes,
                p->current.value_n), "cbor_write_bytes");
          check(p, cbor_read_bytes(&p->decode, &s, &n), "cbor_read_bytes");
          byteBufferWriteN(&string, p->current.value.bytes, p->current.value_n);
          break;
        case TOKEN_RIGHT_PAREN:
          error(p, "empty indef text/bytes not handled");
          break;
        default:
          error(p, "invalid indef text/bytes");
          break;
      }
      nextToken(p);

      while (p->current.type != TOKEN_RIGHT_PAREN) {
        consume(p, TOKEN_COMMA, "missing ,");
        if (p->current.type != t) {
          error(p, "indef text/bytes content mismatch");
          return;
        }
        if (t == TOKEN_TEXT) {
          check(p, cbor_write_textn(&p->encode, p->current.value.text,
                p->current.value_n), "cbor_write_text");
          byteBufferWriteN(&string, p->current.value.text, p->current.value_n);
        }
        else {
          check(p, cbor_write_bytes(&p->encode, p->current.value.bytes,
                p->current.value_n), "cbor_write_bytes");
          byteBufferWriteN(&string, p->current.value.bytes, p->current.value_n);
        }
        nextToken(p);
      }
      consume(p, TOKEN_RIGHT_PAREN, "missing )");

      check(p, cbor_write_end(&p->encode), "cbor_write_end");

      // TODO need to accumulate the bytes in a ByteBuffer
      // then compare the read value
      if (string.used != n) {
        error(p, "length mismatch %zu != %zu", string.used, n);
      }
      if (cbor_memcmp(string.b, &s, string.used) != 0) {
        error(p, "indef text/balues values notequal");
      }
      byteBufferClear(&string);
      break;
    }
    default:
      error(p, "unexpected token");
      break;
  }
}

static void parse(const char* source) {
  Parser p = {
    .source = source,
    .tokenStart = source,
    .currentChar = source,
    .currentLine = 1,
    .current.type = TOKEN_ERROR, .current.length = 0,
    .previous.type = TOKEN_ERROR, .previous.length = 0,
    .hasError = false
  };

  nextToken(&p); // start things off
  for (size_t tc = 0; ; tc++) {
    if (p.current.type == TOKEN_ERROR || p.current.type == TOKEN_EOF) break;
    parse_raw(&p);
    p.decodeOnly = false;
    cbor_init(&p.decode, p.raw, p.raw_n);
    printf("%zu: ", tc);
    dump_mem("", p.raw, p.raw_n);
    uint8_t* enc_raw = malloc(p.raw_n);
    if (enc_raw == NULL) {
      error(&p, "unable to allocate buffer for encode %zu", p.raw_n);
    }
    cbor_init(&p.encode, enc_raw, p.raw_n);
    parse_value(&p);

    if (!p.decodeOnly) {
      if (cbor_read_avail(&p.encode) != p.raw_n) {
        error(&p, "encoded length mismatch %zu != %zu",
            cbor_read_avail(&p.encode), p.raw_n);
      }
      if (memcmp(enc_raw, p.raw, p.raw_n) != 0) {
        dump_mem("exp:", p.raw, p.raw_n);
        dump_mem("act:", enc_raw, p.raw_n);
        error(&p, "encoded bytes mismatch");
      }
    }
    free(enc_raw);
  }
}

static void test_pack_unpack(void) {
  // cbor_pack cbor_vpack cbor_unpack (maybe add cbor_vunpack)
}

static void test_misc(void) {
  // cbor_write_float32
  // cbor_get_xxx
  // cbor_idx_xxx
}

int main(int argc, char* argv[]) {
  char* source;
  if (argc != 2) {
    fprintf(stderr, "usage: dninterp <file>\n");
    exit(1);
  }
  FILE* f = fopen(argv[1], "rt");
  if (f == NULL) {
    fprintf(stderr, "unable to open: %s\n", argv[1]);
    exit(1);
  }
  fseek(f, 0, SEEK_END);
  size_t n = ftell(f);
  fseek(f, 0, SEEK_SET);
  source = malloc(n+1);
  if (source == NULL) {
    fprintf(stderr, "file too large: %zu\n", n);
    exit(1);
  }
  memset(source, 0, n+1);
  fread(source, 1, n, f);
  parse(source);
  test_pack_unpack();
  test_misc();
  return 0;
}
