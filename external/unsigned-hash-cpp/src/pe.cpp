// PE / Authenticode content hash.
//
// Implements the Microsoft "Windows Authenticode Portable Executable Signature
// Format" hash recipe: hash the entire file but exclude
//   1) the OptionalHeader.CheckSum field (4 bytes)
//   2) the IMAGE_DIRECTORY_ENTRY_SECURITY data directory entry (8 bytes)
//   3) the certificate table (the Attribute Certificate Table itself)
// and stop hashing at the start of the certificate table (or EOF if absent).
//
// The output is byte-equal across all signers, ad-hoc / unsigned variants, and
// the original linker output of the same build.

#include "pe.hpp"

#include "byte_reader.hpp"
#include "sha256.h"

#include <algorithm>
#include <cstring>

namespace unsigned_hash {
namespace pe {

using detail::read_u16_le;
using detail::read_u32_le;

// File offset of e_lfanew in the DOS header.
static constexpr size_t kDosLfanewOffset = 0x3c;

// Offsets within IMAGE_NT_HEADERS, relative to the start of IMAGE_NT_HEADERS.
static constexpr size_t kNtSignatureSize = 4; // "PE\0\0"
static constexpr size_t kFileHeaderSize = 20; // sizeof(IMAGE_FILE_HEADER)
// OptionalHeader starts at e_lfanew + 4 + 20.
// Within IMAGE_OPTIONAL_HEADER:
//   Magic                u16   off 0
//   MajorLinkerVersion   u8    off 2
//   MinorLinkerVersion   u8    off 3
//   SizeOfCode           u32   off 4
//   SizeOfInitializedData u32  off 8
//   SizeOfUninitializedData u32 off 12
//   AddressOfEntryPoint  u32   off 16
//   BaseOfCode           u32   off 20
//   [PE32 only] BaseOfData u32 off 24  -> ImageBase u32 off 28
//   [PE32+] ImageBase u64 off 24
//   ... SectionAlignment, FileAlignment, MajorOSVer, MinorOSVer ...
//   ... CheckSum is at offset 64 in both PE32 and PE32+.
// The data directory section starts at:
//   PE32:  offset 96
//   PE32+: offset 112
// Each data directory entry is 8 bytes (VirtualAddress u32, Size u32).
// IMAGE_DIRECTORY_ENTRY_SECURITY is index 4.

static constexpr size_t kCheckSumOffsetInOptHdr = 64;
static constexpr size_t kDataDirOffsetPE32 = 96;
static constexpr size_t kDataDirOffsetPE32Plus = 112;
static constexpr size_t kSecurityDirIndex = 4;
static constexpr size_t kDataDirEntrySize = 8;

static constexpr uint16_t kOptHdrMagicPE32 = 0x010b;
static constexpr uint16_t kOptHdrMagicPE32Plus = 0x020b;

bool is_pe(const uint8_t *data, size_t size) {
	// Conservative detection: MZ magic. The full PE signature check happens
	// inside hash_authenticode(), which reports Truncated/Malformed for
	// PE-looking-but-broken inputs rather than leaving the dispatcher to
	// downgrade them to NotABinary.
	if (size < 2) {
		return false;
	}
	return data[0] == 'M' && data[1] == 'Z';
}

Status stream_authenticode(const uint8_t *data, size_t size,
		unsigned_hash_sha256_ctx *ctx, std::string &message) {
	if (size < 0x40) {
		message = "PE: file too small for DOS header";
		return Status::Truncated;
	}
	if (data[0] != 'M' || data[1] != 'Z') {
		message = "PE: missing MZ signature";
		return Status::NotABinary;
	}

	uint32_t e_lfanew = 0;
	if (!read_u32_le(data, size, kDosLfanewOffset, e_lfanew)) {
		message = "PE: truncated DOS header";
		return Status::Truncated;
	}

	// Need at least PE\0\0 + IMAGE_FILE_HEADER + 2 bytes of optional header magic.
	if (!detail::in_bounds(e_lfanew, kNtSignatureSize + kFileHeaderSize + 2, size)) {
		message = "PE: e_lfanew points outside file";
		return Status::Truncated;
	}

	if (data[e_lfanew] != 'P' || data[e_lfanew + 1] != 'E' ||
			data[e_lfanew + 2] != 0 || data[e_lfanew + 3] != 0) {
		message = "PE: missing PE signature";
		return Status::NotABinary;
	}

	const size_t opt_hdr_off = e_lfanew + kNtSignatureSize + kFileHeaderSize;
	uint16_t opt_magic = 0;
	if (!read_u16_le(data, size, opt_hdr_off, opt_magic)) {
		message = "PE: truncated optional header";
		return Status::Truncated;
	}

	bool is_pe32_plus = false;
	if (opt_magic == kOptHdrMagicPE32) {
		is_pe32_plus = false;
	} else if (opt_magic == kOptHdrMagicPE32Plus) {
		is_pe32_plus = true;
	} else {
		message = "PE: unknown optional header magic";
		return Status::Unsupported;
	}

	uint16_t size_of_optional_header = 0;
	// IMAGE_FILE_HEADER.SizeOfOptionalHeader is at e_lfanew + 4 + 16.
	if (!read_u16_le(data, size, e_lfanew + kNtSignatureSize + 16, size_of_optional_header)) {
		message = "PE: truncated file header";
		return Status::Truncated;
	}

	const size_t data_dir_off = opt_hdr_off + (is_pe32_plus ? kDataDirOffsetPE32Plus : kDataDirOffsetPE32);
	const size_t security_dir_off = data_dir_off + kSecurityDirIndex * kDataDirEntrySize;

	// The optional header must be large enough to hold up through the security
	// data directory entry; otherwise there is no certificate table to skip.
	bool has_security_dir = false;
	uint32_t cert_table_off = 0;
	uint32_t cert_table_size = 0;
	if (size_of_optional_header >= (security_dir_off - opt_hdr_off) + kDataDirEntrySize) {
		has_security_dir = true;
		if (!read_u32_le(data, size, security_dir_off, cert_table_off)) {
			message = "PE: truncated security data directory";
			return Status::Truncated;
		}
		if (!read_u32_le(data, size, security_dir_off + 4, cert_table_size)) {
			message = "PE: truncated security data directory";
			return Status::Truncated;
		}
	}

	const size_t checksum_off = opt_hdr_off + kCheckSumOffsetInOptHdr;
	if (!detail::in_bounds(checksum_off, 4, size)) {
		message = "PE: optional header truncated before CheckSum";
		return Status::Truncated;
	}

	// Determine the end-of-hashing offset:
	// If there is a non-empty cert table, stop at its start. The cert table
	// itself is excluded.
	size_t hash_end = size;
	if (has_security_dir && cert_table_off != 0 && cert_table_size != 0) {
		// The cert table offset is a raw file offset (not RVA).
		if (cert_table_off > size) {
			message = "PE: certificate table offset outside file";
			return Status::Malformed;
		}
		// Defensive: the cert table must lie at or after the headers.
		if (cert_table_off < security_dir_off + kDataDirEntrySize) {
			message = "PE: certificate table overlaps headers";
			return Status::Malformed;
		}
		// Also enforce that cert_table_off + cert_table_size doesn't overflow
		// nor exceed file size; treat that as malformed but continue using
		// cert_table_off as the cutoff.
		uint64_t cert_end = (uint64_t)cert_table_off + (uint64_t)cert_table_size;
		if (cert_end > size) {
			message = "PE: certificate table extends past EOF";
			return Status::Malformed;
		}
		hash_end = cert_table_off;
	}

	if (security_dir_off + kDataDirEntrySize > hash_end) {
		// Should be impossible if the file was parseable up to here, but bail
		// rather than emit a garbage hash.
		message = "PE: security data directory beyond hash region";
		return Status::Malformed;
	}

	// 1. [0, checksum_off)
	unsigned_hash_sha256_update(ctx, data, checksum_off);
	// 2. Skip 4 bytes of CheckSum.
	const size_t after_checksum = checksum_off + 4;
	// 3. [after_checksum, security_dir_off)
	if (after_checksum < security_dir_off) {
		unsigned_hash_sha256_update(ctx, data + after_checksum, security_dir_off - after_checksum);
	}
	// 4. Skip 8 bytes of security data directory entry (only if present).
	const size_t after_security = has_security_dir ? security_dir_off + kDataDirEntrySize : security_dir_off;
	// 5. [after_security, hash_end)
	if (after_security < hash_end) {
		unsigned_hash_sha256_update(ctx, data + after_security, hash_end - after_security);
	}

	return Status::Ok;
}

Status hash_authenticode(const uint8_t *data, size_t size, SliceHash &out, std::string &message) {
	unsigned_hash_sha256_ctx ctx;
	unsigned_hash_sha256_init(&ctx);
	Status st = stream_authenticode(data, size, &ctx, message);
	if (st != Status::Ok) {
		return st;
	}
	uint8_t digest[32];
	unsigned_hash_sha256_final(&ctx, digest);
	std::memcpy(out.sha256.data(), digest, 32);
	out.cputype = 0;
	out.cpusubtype = 0;
	return Status::Ok;
}

} // namespace pe
} // namespace unsigned_hash
