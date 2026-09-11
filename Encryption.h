/*
 *    FILE: Encryption.h
 * PURPOSE: ChaCha20-based obfuscation of the SNMP read community so it is not
 *          broadcasted in cleartext on the LAN.
 *
 *  A fixed nonce and counter are used so every PoESP32 device derives the
 *  same DHCP value. This design prevents simple community string discovery.
 */

#ifndef ENCRYPTION_H
#define ENCRYPTION_H

#include <stdint.h>
#include <stddef.h>

// Encrypt a community string to lowercase hex.
//   key              shared secret (pass COMMUNITY_KEY)
//   plain, len       cleartext community
//   hexOut, hexOutSize  output buffer (needs >= 2*len + 1 bytes)
void encryptCommunity(const char* key, const char* plain, size_t len,
                      char* hexOut, size_t hexOutSize);

// Decrypt hex back to the community.
//   key              shared secret
//   hexIn, hexLen    hex ciphertext
//   out, maxLen      cleartext output (NUL-terminated), capped at maxLen
// Returns the decrypted length.
uint8_t decryptCommunity(const char* key, const char* hexIn, size_t hexLen,
                         char* out, uint8_t maxLen);

#endif // ENCRYPTION_H
