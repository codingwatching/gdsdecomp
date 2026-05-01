// Mach-O signature-strip content hash.
//
// Strategy: hash the file from offset 0 to the "real" end of __LINKEDIT (the
// maximum file offset reached by any LinkEdit-resident sub-region, excluding
// LC_CODE_SIGNATURE). While streaming, normalize the load-command region and
// __LINKEDIT extents so the bytes are identical between signed, ad-hoc-signed,
// and unsigned forms:
//
//   1. mach header `ncmds` and `sizeofcmds` -> the values they would have
//      without LC_CODE_SIGNATURE.
//   2. The entire load-command region is materialized into a buffer and
//      normalized: __LINKEDIT segment vmsize/filesize get rewritten to the
//      unsigned values, and LC_CODE_SIGNATURE (wherever it sits in the LC
//      list) is compacted out by shifting the post-codesig LCs left and
//      zero-filling the now-vacated trailing `codesig_lc_size` bytes. The
//      hash is therefore independent of LC_CODE_SIGNATURE's position in the
//      load-command list.
//   3. Hashing stops at real_linkedit_end. The padding zeros and CS_SuperBlob
//      are not hashed.
//
// Fat Mach-O: parsed at the fat-header level, hashed per-slice. The combined
// hash is SHA-256(concat(slice_hashes)).

#include "macho.hpp"

#include "byte_reader.hpp"
#include "sha256.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace unsigned_hash {
namespace macho {

using detail::bswap32;
using detail::bswap64;
using detail::read_u32_be;
using detail::read_u32_le;
using detail::read_u64_be;
using detail::read_u64_le;

// Magic numbers.
static constexpr uint32_t MH_MAGIC = 0xfeedfaceu;
static constexpr uint32_t MH_CIGAM = 0xcefaedfeu;
static constexpr uint32_t MH_MAGIC_64 = 0xfeedfacfu;
static constexpr uint32_t MH_CIGAM_64 = 0xcffaedfeu;
static constexpr uint32_t FAT_MAGIC = 0xcafebabeu;
static constexpr uint32_t FAT_CIGAM = 0xbebafecau;
static constexpr uint32_t FAT_MAGIC_64 = 0xcafebabfu;
static constexpr uint32_t FAT_CIGAM_64 = 0xbfbafecau;

// Load command identifiers we care about. Apple stamps everyone with these
// constants; LC_REQ_DYLD does not appear on any of the LCs we inspect except
// LC_DYLD_INFO_ONLY, which sets bit 0x80000000.
static constexpr uint32_t LC_REQ_DYLD = 0x80000000u;
static constexpr uint32_t LC_SEGMENT = 0x1u;
static constexpr uint32_t LC_SYMTAB = 0x2u;
static constexpr uint32_t LC_DYSYMTAB = 0xbu;
static constexpr uint32_t LC_SEGMENT_64 = 0x19u;
static constexpr uint32_t LC_CODE_SIGNATURE = 0x1du;
static constexpr uint32_t LC_SEGMENT_SPLIT_INFO = 0x1eu;
static constexpr uint32_t LC_FUNCTION_STARTS = 0x26u;
static constexpr uint32_t LC_DATA_IN_CODE = 0x29u;
static constexpr uint32_t LC_DYLIB_CODE_SIGN_DRS = 0x2bu;
static constexpr uint32_t LC_LINKER_OPTIMIZATION_HINT = 0x2eu;
static constexpr uint32_t LC_DYLD_EXPORTS_TRIE = 0x33u | LC_REQ_DYLD;
static constexpr uint32_t LC_DYLD_CHAINED_FIXUPS = 0x34u | LC_REQ_DYLD;
static constexpr uint32_t LC_ATOM_INFO = 0x36u;
static constexpr uint32_t LC_DYLD_INFO = 0x22u;
static constexpr uint32_t LC_DYLD_INFO_ONLY = 0x22u | LC_REQ_DYLD;

// CPU type bit indicating 64-bit. arm64 and x86_64 both set this.
static constexpr uint32_t CPU_ARCH_ABI64 = 0x01000000u;
static constexpr uint32_t CPU_TYPE_ARM = 12;

bool is_thin_macho(const uint8_t *data, size_t size) {
	if (size < 4) {
		return false;
	}
	uint32_t m = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
			((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
	return m == MH_MAGIC || m == MH_CIGAM || m == MH_MAGIC_64 || m == MH_CIGAM_64;
}

bool is_fat_macho(const uint8_t *data, size_t size) {
	if (size < 4) {
		return false;
	}
	uint32_t m = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
			((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
	return m == FAT_MAGIC || m == FAT_CIGAM || m == FAT_MAGIC_64 || m == FAT_CIGAM_64;
}

namespace {

struct ThinHeader {
	bool is_64 = false;
	bool swapped = false; // file is in non-host endianness; for our purposes
			// only matters that we read fields with the correct swap. We
			// interpret cmds in their on-disk endianness and write back
			// normalized bytes in that same endianness.
	uint32_t cputype = 0;
	uint32_t cpusubtype = 0;
	uint32_t ncmds = 0;
	uint32_t sizeofcmds = 0;
	size_t header_size = 0; // 28 for 32-bit, 32 for 64-bit
};

// Reader for an in-memory load command region. The region is in the file's
// native endianness; if `swap` is true we byte-swap on read (and we will
// likewise swap when writing back during the hash stream).
struct ReaderEndian {
	const uint8_t *base;
	size_t total;
	bool swap;

	bool u32(size_t off, uint32_t &out) const {
		if (!read_u32_le(base, total, off, out)) {
			return false;
		}
		if (swap) {
			out = bswap32(out);
		}
		return true;
	}
	bool u64(size_t off, uint64_t &out) const {
		if (!read_u64_le(base, total, off, out)) {
			return false;
		}
		if (swap) {
			out = bswap64(out);
		}
		return true;
	}
};

bool parse_thin_header(const uint8_t *data, size_t total, size_t base, ThinHeader &h, std::string &msg) {
	if (!detail::in_bounds(base, 28, total)) {
		msg = "Mach-O: header truncated";
		return false;
	}
	uint32_t magic = (uint32_t)data[base] | ((uint32_t)data[base + 1] << 8) |
			((uint32_t)data[base + 2] << 16) | ((uint32_t)data[base + 3] << 24);

	bool swap = false;
	if (magic == MH_MAGIC) {
		h.is_64 = false;
		swap = false;
	} else if (magic == MH_CIGAM) {
		h.is_64 = false;
		swap = true;
	} else if (magic == MH_MAGIC_64) {
		h.is_64 = true;
		swap = false;
	} else if (magic == MH_CIGAM_64) {
		h.is_64 = true;
		swap = true;
	} else {
		msg = "Mach-O: bad magic";
		return false;
	}
	h.swapped = swap;
	h.header_size = h.is_64 ? 32 : 28;

	if (!detail::in_bounds(base, h.header_size, total)) {
		msg = "Mach-O: header truncated";
		return false;
	}

	ReaderEndian r{ data, total, swap };
	if (!r.u32(base + 4, h.cputype)) {
		return false;
	}
	if (!r.u32(base + 8, h.cpusubtype)) {
		return false;
	}
	// filetype @ +12 (we don't need it)
	if (!r.u32(base + 16, h.ncmds)) {
		return false;
	}
	if (!r.u32(base + 20, h.sizeofcmds)) {
		return false;
	}
	// flags @ +24 (we don't need it)
	// for 64-bit, reserved @ +28
	return true;
}

// Tracks the state we need from one walk of the load command list.
struct LoadCommandScan {
	bool have_linkedit = false;
	size_t linkedit_lc_off = 0; // offset of __LINKEDIT segment_command
	size_t linkedit_lc_size = 0;
	uint64_t linkedit_fileoff = 0;
	uint64_t linkedit_filesize = 0;
	// Already-stored vm/file values we will overwrite during hashing.
	size_t linkedit_filesize_field_off = 0;
	size_t linkedit_vmsize_field_off = 0;
	bool linkedit_seg_is_64 = false;

	bool have_codesig = false;
	size_t codesig_lc_off = 0;
	uint32_t codesig_lc_size = 0;
	uint64_t codesig_dataoff = 0;
	uint64_t codesig_datasize = 0;
	bool codesig_is_last = false;

	uint64_t real_linkedit_end = 0; // running max of LinkEdit sub-region ends, EXCLUDING signature

	// Whether we encountered any unknown-but-valid linkedit-resident
	// command we couldn't account for. We don't currently flag this as an
	// error; we're permissive.
};

bool extend_linkedit_end(LoadCommandScan &s, uint64_t end) {
	if (end > s.real_linkedit_end) {
		s.real_linkedit_end = end;
	}
	return true;
}

bool scan_load_commands(const uint8_t *data, size_t total, size_t base,
		const ThinHeader &h, LoadCommandScan &s, std::string &msg) {
	ReaderEndian r{ data, total, h.swapped };

	size_t off = base + h.header_size;
	const size_t lc_region_end = off + h.sizeofcmds;

	if (!detail::in_bounds(base + h.header_size, h.sizeofcmds, total)) {
		msg = "Mach-O: load command region extends past EOF";
		return false;
	}

	for (uint32_t i = 0; i < h.ncmds; ++i) {
		if (off + 8 > lc_region_end) {
			msg = "Mach-O: load command count exceeds sizeofcmds";
			return false;
		}
		uint32_t cmd = 0, cmdsize = 0;
		if (!r.u32(off, cmd)) {
			return false;
		}
		if (!r.u32(off + 4, cmdsize)) {
			return false;
		}
		if (cmdsize < 8 || off + cmdsize > lc_region_end) {
			msg = "Mach-O: bogus load command size";
			return false;
		}

		const bool is_last_cmd = (i + 1 == h.ncmds);

		switch (cmd) {
			case LC_SEGMENT_64: {
				if (cmdsize < 72) {
					msg = "Mach-O: LC_SEGMENT_64 truncated";
					return false;
				}
				// segname @ +8 .. +24 (16 bytes), vmaddr @ +24, vmsize @ +32,
				// fileoff @ +40, filesize @ +48, ...
				char segname[17] = { 0 };
				std::memcpy(segname, data + off + 8, 16);
				uint64_t fileoff = 0, filesize = 0;
				if (!r.u64(off + 40, fileoff)) {
					return false;
				}
				if (!r.u64(off + 48, filesize)) {
					return false;
				}
				const bool is_linkedit = (std::strcmp(segname, "__LINKEDIT") == 0);
				if (is_linkedit) {
					s.have_linkedit = true;
					s.linkedit_lc_off = off;
					s.linkedit_lc_size = cmdsize;
					s.linkedit_fileoff = fileoff;
					s.linkedit_filesize = filesize;
					s.linkedit_filesize_field_off = off + 48;
					s.linkedit_vmsize_field_off = off + 32;
					s.linkedit_seg_is_64 = true;
				}
			} break;
			case LC_SEGMENT: {
				if (cmdsize < 56) {
					msg = "Mach-O: LC_SEGMENT truncated";
					return false;
				}
				// segname @ +8, vmaddr @ +24 (u32), vmsize @ +28 (u32),
				// fileoff @ +32 (u32), filesize @ +36 (u32), ...
				char segname[17] = { 0 };
				std::memcpy(segname, data + off + 8, 16);
				uint32_t fileoff = 0, filesize = 0;
				if (!r.u32(off + 32, fileoff)) {
					return false;
				}
				if (!r.u32(off + 36, filesize)) {
					return false;
				}
				const bool is_linkedit = (std::strcmp(segname, "__LINKEDIT") == 0);
				if (is_linkedit) {
					s.have_linkedit = true;
					s.linkedit_lc_off = off;
					s.linkedit_lc_size = cmdsize;
					s.linkedit_fileoff = fileoff;
					s.linkedit_filesize = filesize;
					s.linkedit_filesize_field_off = off + 36;
					s.linkedit_vmsize_field_off = off + 28;
					s.linkedit_seg_is_64 = false;
				}
			} break;
			case LC_SYMTAB: {
				if (cmdsize < 24) {
					msg = "Mach-O: LC_SYMTAB truncated";
					return false;
				}
				uint32_t symoff = 0, nsyms = 0, stroff = 0, strsize = 0;
				if (!r.u32(off + 8, symoff)) return false;
				if (!r.u32(off + 12, nsyms)) return false;
				if (!r.u32(off + 16, stroff)) return false;
				if (!r.u32(off + 20, strsize)) return false;
				const uint64_t nlist_size = h.is_64 ? 16ull : 12ull;
				const uint64_t sym_end = (uint64_t)symoff + (uint64_t)nsyms * nlist_size;
				const uint64_t str_end = (uint64_t)stroff + (uint64_t)strsize;
				extend_linkedit_end(s, sym_end);
				extend_linkedit_end(s, str_end);
			} break;
			case LC_DYSYMTAB: {
				if (cmdsize < 80) {
					msg = "Mach-O: LC_DYSYMTAB truncated";
					return false;
				}
				// Layout (offsets within LC):
				//  +8  ilocalsym, +12 nlocalsym
				//  +16 iextdefsym, +20 nextdefsym
				//  +24 iundefsym, +28 nundefsym
				//  +32 tocoff, +36 ntoc
				//  +40 modtaboff, +44 nmodtab
				//  +48 extrefsymoff, +52 nextrefsyms
				//  +56 indirectsymoff, +60 nindirectsyms
				//  +64 extreloff, +68 nextrel
				//  +72 locreloff, +76 nlocrel
				const uint64_t module_size = h.is_64 ? 56ull : 52ull;
				uint32_t tocoff = 0, ntoc = 0, modtaboff = 0, nmodtab = 0;
				uint32_t extrefsymoff = 0, nextrefsyms = 0;
				uint32_t indirectsymoff = 0, nindirectsyms = 0;
				uint32_t extreloff = 0, nextrel = 0;
				uint32_t locreloff = 0, nlocrel = 0;
				if (!r.u32(off + 32, tocoff)) return false;
				if (!r.u32(off + 36, ntoc)) return false;
				if (!r.u32(off + 40, modtaboff)) return false;
				if (!r.u32(off + 44, nmodtab)) return false;
				if (!r.u32(off + 48, extrefsymoff)) return false;
				if (!r.u32(off + 52, nextrefsyms)) return false;
				if (!r.u32(off + 56, indirectsymoff)) return false;
				if (!r.u32(off + 60, nindirectsyms)) return false;
				if (!r.u32(off + 64, extreloff)) return false;
				if (!r.u32(off + 68, nextrel)) return false;
				if (!r.u32(off + 72, locreloff)) return false;
				if (!r.u32(off + 76, nlocrel)) return false;
				if (ntoc) extend_linkedit_end(s, (uint64_t)tocoff + (uint64_t)ntoc * 8ull);
				if (nmodtab) extend_linkedit_end(s, (uint64_t)modtaboff + (uint64_t)nmodtab * module_size);
				if (nextrefsyms) extend_linkedit_end(s, (uint64_t)extrefsymoff + (uint64_t)nextrefsyms * 8ull);
				if (nindirectsyms) extend_linkedit_end(s, (uint64_t)indirectsymoff + (uint64_t)nindirectsyms * 4ull);
				if (nextrel) extend_linkedit_end(s, (uint64_t)extreloff + (uint64_t)nextrel * 8ull);
				if (nlocrel) extend_linkedit_end(s, (uint64_t)locreloff + (uint64_t)nlocrel * 8ull);
			} break;
			case LC_DYLD_INFO:
			case LC_DYLD_INFO_ONLY: {
				if (cmdsize < 48) {
					msg = "Mach-O: LC_DYLD_INFO truncated";
					return false;
				}
				// rebase_off/size, bind_off/size, weak_bind_off/size,
				// lazy_bind_off/size, export_off/size — each u32 pair starting at +8.
				for (size_t f = 0; f < 5; ++f) {
					uint32_t pos_off = 0, pos_size = 0;
					if (!r.u32(off + 8 + f * 8, pos_off)) return false;
					if (!r.u32(off + 12 + f * 8, pos_size)) return false;
					if (pos_size) extend_linkedit_end(s, (uint64_t)pos_off + (uint64_t)pos_size);
				}
			} break;
			case LC_FUNCTION_STARTS:
			case LC_DATA_IN_CODE:
			case LC_DYLD_EXPORTS_TRIE:
			case LC_DYLD_CHAINED_FIXUPS:
			case LC_LINKER_OPTIMIZATION_HINT:
			case LC_SEGMENT_SPLIT_INFO:
			case LC_DYLIB_CODE_SIGN_DRS:
			case LC_ATOM_INFO: {
				// linkedit_data_command: dataoff @ +8, datasize @ +12
				if (cmdsize < 16) {
					msg = "Mach-O: linkedit_data_command truncated";
					return false;
				}
				uint32_t dataoff = 0, datasize = 0;
				if (!r.u32(off + 8, dataoff)) return false;
				if (!r.u32(off + 12, datasize)) return false;
				if (datasize) extend_linkedit_end(s, (uint64_t)dataoff + (uint64_t)datasize);
			} break;
			case LC_CODE_SIGNATURE: {
				if (cmdsize < 16) {
					msg = "Mach-O: LC_CODE_SIGNATURE truncated";
					return false;
				}
				uint32_t dataoff = 0, datasize = 0;
				if (!r.u32(off + 8, dataoff)) return false;
				if (!r.u32(off + 12, datasize)) return false;
				s.have_codesig = true;
				s.codesig_lc_off = off;
				s.codesig_lc_size = cmdsize;
				s.codesig_dataoff = dataoff;
				s.codesig_datasize = datasize;
				s.codesig_is_last = is_last_cmd;
			} break;
			default:
				break;
		}

		off += cmdsize;
	}

	return true;
}

// Helper: write a u32 to a 4-byte buffer in the file's endianness.
inline void store_u32(uint8_t out[4], uint32_t v, bool swap) {
	uint32_t w = swap ? bswap32(v) : v;
	out[0] = (uint8_t)(w & 0xff);
	out[1] = (uint8_t)((w >> 8) & 0xff);
	out[2] = (uint8_t)((w >> 16) & 0xff);
	out[3] = (uint8_t)((w >> 24) & 0xff);
}
inline void store_u64(uint8_t out[8], uint64_t v, bool swap) {
	uint64_t w = swap ? bswap64(v) : v;
	for (int i = 0; i < 8; ++i) out[i] = (uint8_t)((w >> (i * 8)) & 0xff);
}

// Stream the slice into the SHA-256 context, applying overrides for the bytes
// that signing mutates so that signed and unsigned forms produce the same
// hash.
struct ByteOverride {
	size_t off;     // absolute file offset
	size_t length;  // length of override
	std::vector<uint8_t> bytes; // replacement bytes; size == length
};

void hash_with_overrides(unsigned_hash_sha256_ctx *ctx,
		const uint8_t *data, size_t start, size_t end,
		std::vector<ByteOverride> overrides) {
	// Sort overrides by offset; we assume non-overlapping (we only emit
	// non-overlapping ones below).
	std::sort(overrides.begin(), overrides.end(),
			[](const ByteOverride &a, const ByteOverride &b) { return a.off < b.off; });

	size_t cur = start;
	for (const auto &ov : overrides) {
		if (ov.off >= end) break;
		const size_t ov_end = ov.off + ov.length;
		if (ov_end <= cur) continue; // already past

		if (ov.off > cur) {
			unsigned_hash_sha256_update(ctx, data + cur, ov.off - cur);
			cur = ov.off;
		}
		const size_t copy_end = std::min(ov_end, end);
		const size_t in_off = cur - ov.off; // how far into ov we already are
		unsigned_hash_sha256_update(ctx, ov.bytes.data() + in_off, copy_end - cur);
		cur = copy_end;
	}
	if (cur < end) {
		unsigned_hash_sha256_update(ctx, data + cur, end - cur);
	}
}

uint64_t round_up(uint64_t v, uint64_t a) {
	if (a == 0) return v;
	return (v + (a - 1)) & ~(a - 1);
}

uint64_t page_size_for_cputype(uint32_t cputype) {
	const uint32_t base = cputype & ~CPU_ARCH_ABI64;
	if (base == CPU_TYPE_ARM) {
		return 0x4000ull;
	}
	return 0x1000ull;
}

} // namespace

Status stream_thin(const uint8_t *data, size_t total_size, size_t base_offset, size_t slice_size,
		unsigned_hash_sha256_ctx *ctx,
		uint32_t &cputype, uint32_t &cpusubtype,
		std::string &message) {
	if (!detail::in_bounds(base_offset, slice_size, total_size)) {
		message = "Mach-O: slice extents outside file";
		return Status::Truncated;
	}

	ThinHeader h;
	if (!parse_thin_header(data, base_offset + slice_size, base_offset, h, message)) {
		// parse_thin_header reports either bad magic or truncated header; map
		// bad magic specifically.
		if (message == "Mach-O: bad magic") {
			return Status::NotABinary;
		}
		return Status::Truncated;
	}

	cputype = h.cputype;
	cpusubtype = h.cpusubtype;

	LoadCommandScan scan;
	if (!scan_load_commands(data, base_offset + slice_size, base_offset, h, scan, message)) {
		return Status::Malformed;
	}

	// Compute end-of-real-linkedit. If __LINKEDIT exists but no sub-region
	// extents reached its bounds (unusual), fall back to fileoff + filesize
	// minus the signature region.
	uint64_t real_end = scan.real_linkedit_end;
	if (scan.have_linkedit) {
		const uint64_t linkedit_min_end = scan.linkedit_fileoff;
		if (real_end < linkedit_min_end) {
			real_end = linkedit_min_end;
		}
		// If we have a code signature, ensure real_end never exceeds the
		// signature start (the signature region is what we want to exclude).
		if (scan.have_codesig && real_end > scan.codesig_dataoff) {
			// This would mean a non-signature linkedit sub-region overlapped
			// with the signature, which would be malformed.
			message = "Mach-O: linkedit sub-region overlaps signature";
			return Status::Malformed;
		}
		// Fallback: if the file was unsigned and we never found any linkedit
		// sub-regions but __LINKEDIT exists, hash through end of __LINKEDIT.
		if (!scan.have_codesig && real_end < scan.linkedit_fileoff + scan.linkedit_filesize) {
			real_end = scan.linkedit_fileoff + scan.linkedit_filesize;
		}
	} else {
		// No __LINKEDIT — strange but possible (e.g., kext/object file). Fall
		// back to slice_size.
		real_end = slice_size;
	}

	if (real_end > slice_size) {
		message = "Mach-O: linkedit end past slice end";
		return Status::Malformed;
	}

	// Build overrides.
	std::vector<ByteOverride> overrides;

	// Mach header normalization (ncmds @ +16, sizeofcmds @ +20).
	{
		uint32_t new_ncmds = h.ncmds;
		uint32_t new_sizeofcmds = h.sizeofcmds;
		if (scan.have_codesig) {
			new_ncmds -= 1;
			new_sizeofcmds -= scan.codesig_lc_size;
		}
		uint8_t buf[4];
		store_u32(buf, new_ncmds, h.swapped);
		overrides.push_back({ base_offset + 16, 4, std::vector<uint8_t>(buf, buf + 4) });
		store_u32(buf, new_sizeofcmds, h.swapped);
		overrides.push_back({ base_offset + 20, 4, std::vector<uint8_t>(buf, buf + 4) });
	}

	// Materialize the entire load-command region into a buffer and normalize
	// it. This is the single place LC bytes are rewritten, regardless of
	// LC_CODE_SIGNATURE's position in the LC list. For codesig-last binaries
	// the result is byte-identical to the previous per-field override scheme;
	// for codesig-in-the-middle binaries, post-codesig LCs are shifted left
	// by codesig_lc_size and the trailing tail is zero-filled.
	const size_t lc_start = base_offset + h.header_size;
	const size_t lc_size = h.sizeofcmds;
	std::vector<uint8_t> lc_buf(data + lc_start, data + lc_start + lc_size);

	// __LINKEDIT segment command vmsize / filesize -> normalized values, in
	// the file's endianness, applied to the buffer at the LCs' original
	// positions (the subsequent compaction pass will move these bytes to
	// their final position if the LINKEDIT LC happens to sit after codesig).
	if (scan.have_linkedit) {
		const uint64_t new_filesize = real_end - scan.linkedit_fileoff;
		const uint64_t page = page_size_for_cputype(h.cputype);
		const uint64_t new_vmsize = round_up(new_filesize, page);
		const size_t vmsize_rel = scan.linkedit_vmsize_field_off - lc_start;
		const size_t filesize_rel = scan.linkedit_filesize_field_off - lc_start;
		if (scan.linkedit_seg_is_64) {
			store_u64(lc_buf.data() + vmsize_rel, new_vmsize, h.swapped);
			store_u64(lc_buf.data() + filesize_rel, new_filesize, h.swapped);
		} else {
			if (new_vmsize > 0xffffffffull || new_filesize > 0xffffffffull) {
				message = "Mach-O: 32-bit __LINKEDIT vmsize/filesize overflow";
				return Status::Malformed;
			}
			store_u32(lc_buf.data() + vmsize_rel, (uint32_t)new_vmsize, h.swapped);
			store_u32(lc_buf.data() + filesize_rel, (uint32_t)new_filesize, h.swapped);
		}
	}

	// Compact away LC_CODE_SIGNATURE: shift everything after it left, then
	// zero-fill the trailing codesig_lc_size bytes. When codesig is last,
	// the memmove copies zero bytes (the post-codesig region is empty) and
	// the zero-fill simply blanks codesig in place - byte-identical to the
	// pre-refactor behavior for the codesig-last case.
	if (scan.have_codesig) {
		const size_t cs_rel = scan.codesig_lc_off - lc_start;
		const size_t after_rel = cs_rel + scan.codesig_lc_size;
		if (after_rel < lc_size) {
			std::memmove(lc_buf.data() + cs_rel,
					lc_buf.data() + after_rel,
					lc_size - after_rel);
		}
		std::memset(lc_buf.data() + lc_size - scan.codesig_lc_size, 0,
				scan.codesig_lc_size);
	}

	overrides.push_back({ lc_start, lc_size, std::move(lc_buf) });

	// Stream [base_offset, base_offset + real_end) into ctx with overrides.
	hash_with_overrides(ctx, data, base_offset, base_offset + (size_t)real_end, std::move(overrides));
	return Status::Ok;
}

Status hash_thin(const uint8_t *data, size_t total_size, size_t base_offset, size_t slice_size,
		SliceHash &out, std::string &message) {
	unsigned_hash_sha256_ctx ctx;
	unsigned_hash_sha256_init(&ctx);
	uint32_t cputype = 0, cpusubtype = 0;
	Status st = stream_thin(data, total_size, base_offset, slice_size, &ctx, cputype, cpusubtype, message);
	if (st != Status::Ok) {
		return st;
	}
	uint8_t digest[32];
	unsigned_hash_sha256_final(&ctx, digest);
	std::memcpy(out.sha256.data(), digest, 32);
	out.cputype = cputype;
	out.cpusubtype = cpusubtype;
	return Status::Ok;
}

namespace {

// Walk the fat-arch table, invoking `visit(offset, slice_size, cputype, cpusubtype)`
// for each slice. Used by both stream_fat and hash_fat.
template <typename Visit>
Status visit_fat_slices(const uint8_t *data, size_t size, std::string &message, Visit visit) {
	if (size < 8) {
		message = "Mach-O fat: header truncated";
		return Status::Truncated;
	}
	uint32_t magic = 0;
	if (!read_u32_be(data, size, 0, magic)) {
		message = "Mach-O fat: cannot read magic";
		return Status::Truncated;
	}
	const bool is_fat_64 = (magic == FAT_MAGIC_64) || (magic == FAT_CIGAM_64);
	const bool is_fat = (magic == FAT_MAGIC) || (magic == FAT_CIGAM) || is_fat_64;
	if (!is_fat) {
		message = "Mach-O fat: bad magic";
		return Status::NotABinary;
	}
	uint32_t nfat_arch = 0;
	if (!read_u32_be(data, size, 4, nfat_arch)) {
		message = "Mach-O fat: cannot read nfat_arch";
		return Status::Truncated;
	}
	if (nfat_arch == 0 || nfat_arch > 64) {
		message = "Mach-O fat: implausible nfat_arch";
		return Status::Malformed;
	}
	const size_t entry_size = is_fat_64 ? 32 : 20;
	const size_t arch_off_base = 8;
	if (!detail::in_bounds(arch_off_base, (size_t)nfat_arch * entry_size, size)) {
		message = "Mach-O fat: arch table truncated";
		return Status::Truncated;
	}
	for (uint32_t i = 0; i < nfat_arch; ++i) {
		const size_t e = arch_off_base + (size_t)i * entry_size;
		uint32_t cputype = 0, cpusubtype = 0;
		uint64_t offset = 0, slice_size = 0;
		if (is_fat_64) {
			if (!read_u32_be(data, size, e + 0, cputype)) return Status::Truncated;
			if (!read_u32_be(data, size, e + 4, cpusubtype)) return Status::Truncated;
			if (!read_u64_be(data, size, e + 8, offset)) return Status::Truncated;
			if (!read_u64_be(data, size, e + 16, slice_size)) return Status::Truncated;
		} else {
			uint32_t off32 = 0, sz32 = 0;
			if (!read_u32_be(data, size, e + 0, cputype)) return Status::Truncated;
			if (!read_u32_be(data, size, e + 4, cpusubtype)) return Status::Truncated;
			if (!read_u32_be(data, size, e + 8, off32)) return Status::Truncated;
			if (!read_u32_be(data, size, e + 12, sz32)) return Status::Truncated;
			offset = off32;
			slice_size = sz32;
		}
		if (offset > size || slice_size > size || offset + slice_size > size) {
			message = "Mach-O fat: slice extents outside file";
			return Status::Malformed;
		}
		Status st = visit((size_t)offset, (size_t)slice_size, cputype, cpusubtype);
		if (st != Status::Ok) {
			return st;
		}
	}
	return Status::Ok;
}

} // namespace

Status stream_fat(const uint8_t *data, size_t size,
		unsigned_hash_sha256_ctx *ctx, std::string &message) {
	return visit_fat_slices(data, size, message,
			[&](size_t offset, size_t slice_size, uint32_t /*ct*/, uint32_t /*cst*/) -> Status {
				uint32_t ct = 0, cst = 0;
				return stream_thin(data, size, offset, slice_size, ctx, ct, cst, message);
			});
}

Status hash_fat(const uint8_t *data, size_t size,
		std::vector<SliceHash> &slices, std::array<uint8_t, 32> &combined,
		std::string &message) {
	slices.clear();
	Status st = visit_fat_slices(data, size, message,
			[&](size_t offset, size_t slice_size, uint32_t /*ct*/, uint32_t /*cst*/) -> Status {
				SliceHash sh;
				Status s = hash_thin(data, size, offset, slice_size, sh, message);
				if (s != Status::Ok) {
					return s;
				}
				slices.push_back(sh);
				return Status::Ok;
			});
	if (st != Status::Ok) {
		return st;
	}
	// Combined hash = SHA-256 of concatenated slice digests.
	unsigned_hash_sha256_ctx ctx;
	unsigned_hash_sha256_init(&ctx);
	for (const auto &sh : slices) {
		unsigned_hash_sha256_update(&ctx, sh.sha256.data(), 32);
	}
	uint8_t digest[32];
	unsigned_hash_sha256_final(&ctx, digest);
	std::memcpy(combined.data(), digest, 32);
	return Status::Ok;
}

} // namespace macho
} // namespace unsigned_hash
