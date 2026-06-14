// unsigned-hash-cpp: stable SHA-256 of PE / Mach-O binaries with all
// signature-affected bytes excluded or normalized.
//
// The same hash is produced for:
//   - the original unsigned linker output,
//   - an ad-hoc-signed copy (codesign -s -),
//   - a copy signed by any real Authenticode / Apple developer identity.
//
// Different builds of the same source still produce different hashes; the
// algorithm only normalizes the bytes that the signing process itself touches.

#ifndef UNSIGNED_HASH_UNSIGNED_HASH_HPP
#define UNSIGNED_HASH_UNSIGNED_HASH_HPP

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#if defined(__cpp_lib_span) && __cpp_lib_span >= 202002L
#include <span>
#define UNSIGNED_HASH_HAS_STD_SPAN 1
#else
#define UNSIGNED_HASH_HAS_STD_SPAN 0
#endif

namespace unsigned_hash {

enum class Format {
	Unknown,
	PE,
	MachO,
	FatMachO,
	Directory,
};

enum class Status {
	Ok,
	NotABinary,
	Truncated,
	Malformed,
	Unsupported,
	IoError,
};

struct SliceHash {
	uint32_t cputype = 0;
	uint32_t cpusubtype = 0;
	std::array<uint8_t, 32> sha256{};
};

// Per-file entry recorded during directory hashing. The Merkle scheme makes
// the per-file digest a first-class artifact: callers can compare entries
// across builds, dedup by content, or feed them into manifests.
struct DirectoryEntryHash {
	std::string relative_path; // forward-slash separated, never starts with '/'
	Format format = Format::Unknown; // PE / MachO / FatMachO if recognized; Unknown for raw bytes
	uint64_t size = 0; // raw file size in bytes
	std::array<uint8_t, 32> sha256{}; // unsigned-content hash for binaries; raw SHA-256 otherwise
};

struct DirectoryHashOptions {
	// When true (default), each per-file digest is mixed into the directory
	// hash together with its relative path (length-prefixed). Reordering or
	// renaming files therefore changes the directory hash.
	// When false, the directory hash mixes only the per-file digests
	// separated by a fixed 16-byte file-barrier magic (UH_FILE_BARRIER).
	// The hash then depends on per-file content order but not on file names.
	bool include_paths = true;

	// When true (default), hidden files and directories are skipped.
	bool skip_hidden_files = true;

	// when true, Info.plist is skipped
	bool skip_info_plist = true;
};

// Magic constant emitted between files when DirectoryHashOptions.include_paths
// is false. 16 bytes of ASCII chosen to be unlikely to appear naturally.
inline constexpr uint8_t UH_FILE_BARRIER[16] = {
	'U', 'H', '_', 'F', 'I', 'L', 'E', '_',
	'B', 'A', 'R', 'R', 'I', 'E', 'R', '1',
};

struct HashResult {
	Status status = Status::NotABinary;
	Format format = Format::Unknown;
	// For PE and thin Mach-O: one entry. For FatMachO: one per slice
	// (in fat-header order). Empty for Directory.
	std::vector<SliceHash> slices;
	// For Directory format: per-file entries in lexicographic path order.
	// Empty for binary formats.
	std::vector<DirectoryEntryHash> entries;
	// For binaries: SHA-256 of the concatenation of slices[*].sha256.
	// For Directory: the Merkle root over `entries`.
	// For single-slice binaries this equals slices[0].sha256.
	std::array<uint8_t, 32> combined_sha256{};
	std::string message;
};

// Lightweight non-owning byte view used by the public API. We avoid <span> in
// the public header so the library compiles cleanly on C++17 toolchains.
struct ByteView {
	const uint8_t *data = nullptr;
	size_t size = 0;

	constexpr ByteView() noexcept = default;
	constexpr ByteView(const uint8_t *p, size_t n) noexcept : data(p), size(n) {}

	template <typename T>
	ByteView(const T *p, size_t n) noexcept : data(reinterpret_cast<const uint8_t *>(p)), size(n) {}

#if UNSIGNED_HASH_HAS_STD_SPAN
	ByteView(std::span<const uint8_t> s) noexcept : data(s.data()), size(s.size()) {}
#endif
};

HashResult hash(ByteView data);
HashResult hash_file(const std::string &path);

// Hash a directory tree. Walks recursively, skips symlinks, prunes any
// directory named `_CodeSignature`. Each remaining file is hashed with the
// format-appropriate unsigned-content hash (PE Authenticode, Mach-O
// signature-stripped) or as raw bytes if it is not a recognized binary.
// `entries` is sorted lexicographically by relative_path. `combined_sha256`
// is a Merkle root over the entries (see DirectoryHashOptions).
HashResult hash_directory(const std::string &dir_path,
		DirectoryHashOptions opts = {});

// Hash a set of files.
// `files` is a map of path to file data.
// `opts` is the directory hash options.
HashResult hash_directory_set(const std::vector<std::pair<std::string, std::pair<const uint8_t *, size_t>>> &files, DirectoryHashOptions opts);

// Hex-encode a 32-byte SHA-256 digest. Lower-case, no separators.
std::string to_hex(const std::array<uint8_t, 32> &digest);

const char *status_to_string(Status s);
const char *format_to_string(Format f);

} // namespace unsigned_hash

#endif // UNSIGNED_HASH_UNSIGNED_HASH_HPP
