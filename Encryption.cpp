#include "Encryption.h"
#include <string.h>

// ---- ChaCha20 (IETF variant: 256-bit key, 96-bit nonce) ----

static uint32_t rotl32(uint32_t x, int n) {
  return (x << n) | (x >> (32 - n));
}

#define QR(a, b, c, d)                             \
  do {                                             \
    (a) += (b); (d) ^= (a); (d) = rotl32((d), 16); \
    (c) += (d); (b) ^= (c); (b) = rotl32((b), 12); \
    (a) += (b); (d) ^= (a); (d) = rotl32((d), 8);  \
    (c) += (d); (b) ^= (c); (b) = rotl32((b), 7);  \
  } while (0)

static void chacha20Block(const uint8_t key[32], uint32_t counter,
                          const uint8_t nonce[12], uint8_t out[64]) {
  uint32_t x[16], orig[16];

  x[0] = 0x61707865;  // "expa"
  x[1] = 0x3320646e;  // "nd 3"
  x[2] = 0x79622d32;  // "2-by"
  x[3] = 0x6b206574;  // "te k"

  for (int i = 0; i < 8; i++) {
    x[4 + i] = (uint32_t)key[4 * i] |
               ((uint32_t)key[4 * i + 1] << 8) |
               ((uint32_t)key[4 * i + 2] << 16) |
               ((uint32_t)key[4 * i + 3] << 24);
  }
  x[12] = counter;
  x[13] = (uint32_t)nonce[0] | ((uint32_t)nonce[1] << 8) |
          ((uint32_t)nonce[2] << 16) | ((uint32_t)nonce[3] << 24);
  x[14] = (uint32_t)nonce[4] | ((uint32_t)nonce[5] << 8) |
          ((uint32_t)nonce[6] << 16) | ((uint32_t)nonce[7] << 24);
  x[15] = (uint32_t)nonce[8] | ((uint32_t)nonce[9] << 8) |
          ((uint32_t)nonce[10] << 16) | ((uint32_t)nonce[11] << 24);

  memcpy(orig, x, sizeof(x));

  for (int r = 0; r < 10; r++) {
    QR(x[0], x[4], x[8],  x[12]);
    QR(x[1], x[5], x[9],  x[13]);
    QR(x[2], x[6], x[10], x[14]);
    QR(x[3], x[7], x[11], x[15]);

    QR(x[0], x[5], x[10], x[15]);
    QR(x[1], x[6], x[11], x[12]);
    QR(x[2], x[7], x[8],  x[13]);
    QR(x[3], x[4], x[9],  x[14]);
  }

  for (int i = 0; i < 16; i++) {
    uint32_t v = x[i] + orig[i];
    out[4 * i]     = (uint8_t)(v & 0xFF);
    out[4 * i + 1] = (uint8_t)((v >> 8) & 0xFF);
    out[4 * i + 2] = (uint8_t)((v >> 16) & 0xFF);
    out[4 * i + 3] = (uint8_t)((v >> 24) & 0xFF);
  }
}

// Expand the shared key string to a 32-byte ChaCha20 key. Any bytes needed beyond
// the end of the string are filled by walking backwards through it (mirroring),
// rather than zero-padding, so a >=16-char key yields 32 non-trivial key bytes.
static void makeKey32(const char* key, uint8_t out[32]) {
  size_t n = strlen(key);
  size_t i = 0;
  for (; i < n && i < 32; i++) {
    out[i] = (uint8_t)key[i];                 // forward
  }
  for (; i < 32; i++) {
    out[i] = (uint8_t)key[n - 1 - (i - n)];   // walk backwards
  }
}

// Fixed nonce shared by all PoESP32 devices
static const uint8_t fixedNonce[12] = {0};

static uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
  if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
  return 0;
}

void encryptCommunity(const char* key, const char* plain, size_t len,
                      char* hexOut, size_t hexOutSize) {
  static const char hex[] = "0123456789abcdef";

  uint8_t key32[32];
  makeKey32(key, key32);
  uint8_t keystream[64];
  chacha20Block(key32, 0, fixedNonce, keystream);

  if (hexOutSize < 2 * len + 1) return;
  for (size_t i = 0; i < len; i++) {
    uint8_t c = (uint8_t)plain[i] ^ keystream[i];
    hexOut[2 * i]     = hex[c >> 4];
    hexOut[2 * i + 1] = hex[c & 0x0F];
  }
  hexOut[2 * len] = '\0';
}

uint8_t decryptCommunity(const char* key, const char* hexIn, size_t hexLen,
                         char* out, uint8_t maxLen) {
  uint8_t key32[32];
  makeKey32(key, key32);
  uint8_t keystream[64];
  chacha20Block(key32, 0, fixedNonce, keystream);

  size_t n = hexLen / 2;
  if (n > maxLen) n = maxLen;
  for (size_t i = 0; i < n; i++) {
    uint8_t c = (uint8_t)((hexNibble(hexIn[2 * i]) << 4) | hexNibble(hexIn[2 * i + 1]));
    out[i] = (char)(c ^ keystream[i]);
  }
  out[n] = '\0';
  return (uint8_t)n;
}
