#ifndef UNSIGNED_HASH_PE_HPP
#define UNSIGNED_HASH_PE_HPP

#include "sha256.h"
#include "unsigned_hash/unsigned_hash.hpp"

namespace unsigned_hash {
namespace pe {

// Returns true if the buffer looks like a PE file (MZ + valid PE signature).
bool is_pe(const uint8_t *data, size_t size);

// Stream the Authenticode "image" into `ctx` without finalizing. Skips the
// CheckSum field, the security data directory entry, and stops at the start
// of the certificate table (or EOF if absent).
Status stream_authenticode(const uint8_t *data, size_t size,
		unsigned_hash_sha256_ctx *ctx, std::string &message);

// Compute the Authenticode-style content hash of a PE image. Implemented as
// init -> stream_authenticode -> final.
Status hash_authenticode(const uint8_t *data, size_t size, SliceHash &out, std::string &message);

} // namespace pe
} // namespace unsigned_hash

#endif // UNSIGNED_HASH_PE_HPP
