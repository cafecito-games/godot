#pragma once

#include <cstddef>
#include <cstdint>

namespace CafecitoSecurity {

struct HashStreamOps {
	void *context;
	void (*update)(void *p_context, const uint8_t *p_data, size_t p_length);
};

struct RuntimeOps {
	void (*sha256)(const uint8_t *p_data, size_t p_length, uint8_t r_digest[32]);
};

uint32_t get_pack_header_magic();
uint32_t get_encrypted_header_magic();
void derive_aes_key(const uint8_t p_raw_key[32], uint8_t r_derived_key[32], const RuntimeOps *p_runtime);
bool force_encrypt_pck_directory();
void feed_key_fingerprint(const HashStreamOps *p_hash_stream);
const char *get_export_dialog_title_override();

} // namespace CafecitoSecurity
