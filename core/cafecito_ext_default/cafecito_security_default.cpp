#include "core/crypto/cafecito_security.h"

#include <cstring>

extern uint8_t script_encryption_key[32];

namespace CafecitoSecurity {

uint32_t get_pack_header_magic() {
	return 0x43504447; // "GDPC"
}

uint32_t get_encrypted_header_magic() {
	return 0x43454447; // "GDEC"
}

void derive_aes_key(const uint8_t p_raw_key[32], uint8_t r_derived_key[32], const RuntimeOps *p_runtime) {
	(void)p_runtime;
	memcpy(r_derived_key, p_raw_key, 32);
}

bool force_encrypt_pck_directory() {
	return false;
}

void feed_key_fingerprint(const HashStreamOps *p_hash_stream) {
	p_hash_stream->update(p_hash_stream->context, script_encryption_key, 32);

	uint8_t magic_bytes[8];
	const uint32_t pack_magic = get_pack_header_magic();
	const uint32_t encrypted_magic = get_encrypted_header_magic();
	for (int index = 0; index < 4; index++) {
		magic_bytes[index] = static_cast<uint8_t>((pack_magic >> (index * 8)) & 0xFF);
		magic_bytes[index + 4] = static_cast<uint8_t>((encrypted_magic >> (index * 8)) & 0xFF);
	}
	p_hash_stream->update(p_hash_stream->context, magic_bytes, sizeof(magic_bytes));
}

const char *get_export_dialog_title_override() {
	return nullptr;
}

} // namespace CafecitoSecurity
