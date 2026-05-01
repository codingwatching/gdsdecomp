#ifndef UNSIGNED_HASH_MACHO_HPP
#define UNSIGNED_HASH_MACHO_HPP

#include "sha256.h"
#include "unsigned_hash/unsigned_hash.hpp"

namespace unsigned_hash {
namespace macho {

bool is_thin_macho(const uint8_t *data, size_t size);
bool is_fat_macho(const uint8_t *data, size_t size);

// Stream the normalized content of a thin Mach-O slice into `ctx` without
// finalizing. `data` must be the full file buffer; `base_offset`/`slice_size`
// describe the slice extent (use base_offset=0, slice_size=size for a thin
// file). On success, `cputype` and `cpusubtype` are populated from the
// slice's mach_header.
Status stream_thin(const uint8_t *data, size_t total_size, size_t base_offset, size_t slice_size,
		unsigned_hash_sha256_ctx *ctx,
		uint32_t &cputype, uint32_t &cpusubtype,
		std::string &message);

// Stream every slice of a fat Mach-O into `ctx` in fat-header order. No
// per-slice digest is captured; this is for single-context scenarios where
// the caller wants one hash over the whole fat binary's normalized content.
Status stream_fat(const uint8_t *data, size_t size,
		unsigned_hash_sha256_ctx *ctx, std::string &message);

// Hash a thin Mach-O slice. Wrapper around stream_thin: init -> stream -> final.
Status hash_thin(const uint8_t *data, size_t total_size, size_t base_offset, size_t slice_size,
		SliceHash &out, std::string &message);

// Hash a fat Mach-O. Populates `slices` with one SliceHash per arch in
// fat-header order. `combined` is set to SHA-256(concat(slices[*].sha256)).
Status hash_fat(const uint8_t *data, size_t size,
		std::vector<SliceHash> &slices, std::array<uint8_t, 32> &combined,
		std::string &message);

} // namespace macho
} // namespace unsigned_hash

#endif // UNSIGNED_HASH_MACHO_HPP
