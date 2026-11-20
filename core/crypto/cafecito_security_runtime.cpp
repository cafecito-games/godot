#include "core/crypto/cafecito_security_godot.h"

#include "core/crypto/crypto_core.h"

namespace CafecitoSecurity {

namespace {

void runtime_sha256(const uint8_t *p_data, size_t p_length, uint8_t r_digest[32]) {
	CryptoCore::sha256(p_data, static_cast<int>(p_length), r_digest);
}

void hash_stream_update(void *p_context, const uint8_t *p_data, size_t p_length) {
	CryptoCore::SHA256Context *context = static_cast<CryptoCore::SHA256Context *>(p_context);
	context->update(p_data, p_length);
}

} // namespace

const RuntimeOps *get_runtime_ops() {
	static const RuntimeOps ops = {
		&runtime_sha256,
	};
	return &ops;
}

String key_fingerprint() {
	CryptoCore::SHA256Context context;
	context.start();

	HashStreamOps stream_ops = {
		&context,
		&hash_stream_update,
	};
	feed_key_fingerprint(&stream_ops);

	uint8_t digest[32];
	context.finish(digest);
	return String::hex_encode_buffer(digest, 32);
}

} // namespace CafecitoSecurity
