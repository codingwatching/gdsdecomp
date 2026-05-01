// Bounds-checked little-endian / big-endian byte reading helpers.
//
// Used internally by the format parsers so that any out-of-bounds access turns
// into a Status::Truncated / Status::Malformed result rather than UB.

#ifndef UNSIGNED_HASH_BYTE_READER_HPP
#define UNSIGNED_HASH_BYTE_READER_HPP

#include <cstdint>
#include <cstring>

namespace unsigned_hash {
namespace detail {

inline bool in_bounds(size_t off, size_t len, size_t total) {
	if (off > total) {
		return false;
	}
	if (len > total - off) {
		return false;
	}
	return true;
}

inline bool read_u8(const uint8_t *base, size_t total, size_t off, uint8_t &out) {
	if (!in_bounds(off, 1, total)) {
		return false;
	}
	out = base[off];
	return true;
}

inline bool read_u16_le(const uint8_t *base, size_t total, size_t off, uint16_t &out) {
	if (!in_bounds(off, 2, total)) {
		return false;
	}
	out = static_cast<uint16_t>((uint32_t)base[off] | ((uint32_t)base[off + 1] << 8));
	return true;
}

inline bool read_u32_le(const uint8_t *base, size_t total, size_t off, uint32_t &out) {
	if (!in_bounds(off, 4, total)) {
		return false;
	}
	out = (uint32_t)base[off] | ((uint32_t)base[off + 1] << 8) |
			((uint32_t)base[off + 2] << 16) | ((uint32_t)base[off + 3] << 24);
	return true;
}

inline bool read_u64_le(const uint8_t *base, size_t total, size_t off, uint64_t &out) {
	if (!in_bounds(off, 8, total)) {
		return false;
	}
	uint32_t lo, hi;
	if (!read_u32_le(base, total, off, lo)) {
		return false;
	}
	if (!read_u32_le(base, total, off + 4, hi)) {
		return false;
	}
	out = (uint64_t)lo | ((uint64_t)hi << 32);
	return true;
}

inline bool read_u32_be(const uint8_t *base, size_t total, size_t off, uint32_t &out) {
	if (!in_bounds(off, 4, total)) {
		return false;
	}
	out = ((uint32_t)base[off] << 24) | ((uint32_t)base[off + 1] << 16) |
			((uint32_t)base[off + 2] << 8) | (uint32_t)base[off + 3];
	return true;
}

inline bool read_u64_be(const uint8_t *base, size_t total, size_t off, uint64_t &out) {
	if (!in_bounds(off, 8, total)) {
		return false;
	}
	uint32_t hi, lo;
	if (!read_u32_be(base, total, off, hi)) {
		return false;
	}
	if (!read_u32_be(base, total, off + 4, lo)) {
		return false;
	}
	out = ((uint64_t)hi << 32) | (uint64_t)lo;
	return true;
}

inline uint16_t bswap16(uint16_t v) {
	return static_cast<uint16_t>(((uint32_t)v >> 8) | (((uint32_t)v & 0xff) << 8));
}

inline uint32_t bswap32(uint32_t v) {
	return ((v & 0x000000ffu) << 24) | ((v & 0x0000ff00u) << 8) |
			((v & 0x00ff0000u) >> 8) | ((v & 0xff000000u) >> 24);
}

inline uint64_t bswap64(uint64_t v) {
	return ((uint64_t)bswap32((uint32_t)(v & 0xffffffffu)) << 32) |
			(uint64_t)bswap32((uint32_t)(v >> 32));
}

} // namespace detail
} // namespace unsigned_hash

#endif // UNSIGNED_HASH_BYTE_READER_HPP
