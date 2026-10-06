// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <stddef.h>
bool mux_android_initialize();
bool mux_android_nameserver(char* output, size_t capacity);
// Whether Android trusts a server's certificate chain for `host`, as its own
// TLS would: the chain (leaf first, DER) given to the platform's default
// X509TrustManager, through X509TrustManagerExtensions.checkServerTrusted --
// system and user CAs, its chain building, distrust and network security
// config. False where it refuses, and where it cannot be asked; `why` gets
// its message then (up to `capacity` bytes).
bool mux_android_trusts(const unsigned char* const* certificates, const size_t* sizes, size_t count, const char* host,
                        char* why, size_t capacity);
