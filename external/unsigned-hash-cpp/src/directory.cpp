// Directory / framework Merkle hashing.
//
// Walk the directory tree, skip symlinks, prune _CodeSignature, hash each
// regular file with its format-appropriate unsigned-content hash (or raw
// SHA-256 for non-binaries), then mix per-file digests into a single Merkle
// root with optional path framing.

#include "directory.hpp"

#include "macho.hpp"
#include "pe.hpp"
#include "sha256.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>
#include <vector>

namespace unsigned_hash {
namespace directory {

namespace fs = std::filesystem;

namespace {

constexpr uint8_t kFmtTagRaw = 0;
constexpr uint8_t kFmtTagPE = 1;
constexpr uint8_t kFmtTagMachO = 2;
constexpr uint8_t kFmtTagFatMachO = 3;

uint8_t format_tag(Format f) {
	switch (f) {
		case Format::PE: return kFmtTagPE;
		case Format::MachO: return kFmtTagMachO;
		case Format::FatMachO: return kFmtTagFatMachO;
		default: return kFmtTagRaw;
	}
}

// Read entire file into a buffer. Returns false + sets `err_message` on error.
bool read_entire_file(const fs::path &p, std::vector<uint8_t> &out, std::string &err_message) {
	std::ifstream f(p, std::ios::binary | std::ios::ate);
	if (!f) {
		err_message = "cannot open " + p.string();
		return false;
	}
	std::streamsize sz = f.tellg();
	if (sz < 0) {
		err_message = "cannot determine size of " + p.string();
		return false;
	}
	f.seekg(0, std::ios::beg);
	out.assign((size_t)sz, 0);
	if (sz > 0 && !f.read(reinterpret_cast<char *>(out.data()), sz)) {
		err_message = "read error on " + p.string();
		return false;
	}
	return true;
}

// Detect format from magic bytes (no parsing beyond magic).
Format detect_format(const uint8_t *data, size_t size) {
	if (pe::is_pe(data, size)) {
		return Format::PE;
	}
	if (macho::is_fat_macho(data, size)) {
		return Format::FatMachO;
	}
	if (macho::is_thin_macho(data, size)) {
		return Format::MachO;
	}
	return Format::Unknown;
}

// Compute the per-file unsigned-content SHA-256. The result must equal what
// `hash()`/`hash_file()` would return for the same buffer, so callers can
// compose `entries[i].sha256` with `hash_file()` results.
//
// Note: this is why thin/PE go through the stream_* primitives (init->stream
// ->final == wrapper) but FatMachO uses the hash_fat wrapper - FatMachO's
// public hash is SHA-256(concat(per_slice_digests)), not a single SHA-256
// over the streamed normalized bytes, so we cannot use stream_fat here.
Status compute_file_hash(const uint8_t *buf, size_t buf_len, Format fmt,
		std::array<uint8_t, 32> &out, std::string &message) {
	switch (fmt) {
		case Format::PE: {
			SliceHash sh;
			Status st = pe::hash_authenticode(buf, buf_len, sh, message);
			if (st == Status::Ok) {
				out = sh.sha256;
			}
			return st;
		}
		case Format::MachO: {
			SliceHash sh;
			Status st = macho::hash_thin(buf, buf_len, 0, buf_len, sh, message);
			if (st == Status::Ok) {
				out = sh.sha256;
			}
			return st;
		}
		case Format::FatMachO: {
			std::vector<SliceHash> slices;
			std::array<uint8_t, 32> combined{};
			Status st = macho::hash_fat(buf, buf_len, slices, combined, message);
			if (st == Status::Ok) {
				out = combined;
			}
			return st;
		}
		default: {
			unsigned_hash_sha256_ctx ctx;
			unsigned_hash_sha256_init(&ctx);
			unsigned_hash_sha256_update(&ctx, buf, buf_len);
			uint8_t digest[32];
			unsigned_hash_sha256_final(&ctx, digest);
			std::memcpy(out.data(), digest, 32);
			return Status::Ok;
		}
	}
}

// Convert a filesystem relative path to a forward-slash string.
std::string to_forward_slash(const fs::path &rel) {
	std::string s = rel.generic_string();
	// generic_string() already uses forward slashes.
	return s;
}

void mix_entry_into_dir_ctx(unsigned_hash_sha256_ctx *ctx,
		const DirectoryEntryHash &e, bool include_paths) {
	if (include_paths) {
		uint32_t plen = (uint32_t)e.relative_path.size();
		uint8_t lenbuf[4];
		lenbuf[0] = (uint8_t)(plen & 0xff);
		lenbuf[1] = (uint8_t)((plen >> 8) & 0xff);
		lenbuf[2] = (uint8_t)((plen >> 16) & 0xff);
		lenbuf[3] = (uint8_t)((plen >> 24) & 0xff);
		unsigned_hash_sha256_update(ctx, lenbuf, 4);
		if (plen) {
			unsigned_hash_sha256_update(ctx,
					reinterpret_cast<const uint8_t *>(e.relative_path.data()), plen);
		}
	} else {
		unsigned_hash_sha256_update(ctx, UH_FILE_BARRIER, sizeof(UH_FILE_BARRIER));
	}
	uint8_t tag = format_tag(e.format);
	unsigned_hash_sha256_update(ctx, &tag, 1);
	unsigned_hash_sha256_update(ctx, e.sha256.data(), 32);
}

} // namespace

bool add_hash_to_result(HashResult &r, unsigned_hash_sha256_ctx *dir_ctx, const uint8_t *buf, size_t buf_len, const std::string &relative, bool include_paths) {
	DirectoryEntryHash e;
	e.relative_path = relative;
	e.size = (uint64_t)buf_len;
	e.format = detect_format(buf, buf_len);

	std::string msg;
	Status st = compute_file_hash(buf, buf_len, e.format, e.sha256, msg);
	if (st != Status::Ok) {
		// Recognized as a binary but failed to parse. Fall back to raw
		// SHA-256 so the directory hash stays computable; record the
		// fallback by setting format=Unknown.
		e.format = Format::Unknown;
		std::string msg2;
		Status st2 = compute_file_hash(buf, buf_len, Format::Unknown, e.sha256, msg2);
		if (st2 != Status::Ok) {
			r.status = Status::Malformed;
			r.message = "fallback raw hash failed for " + relative + ": " + msg2;
			return false;
		}
	}

	mix_entry_into_dir_ctx(dir_ctx, e, include_paths);
	r.entries.push_back(std::move(e));
	return true;
}

HashResult hash(const std::string &dir_path, DirectoryHashOptions opts) {
	HashResult r;
	r.format = Format::Directory;

	std::error_code ec;
	fs::path root = fs::path(dir_path);
	fs::file_status st = fs::status(root, ec);
	if (ec) {
		r.status = Status::IoError;
		r.message = "stat failed: " + ec.message();
		return r;
	}
	if (!fs::is_directory(st)) {
		r.status = Status::IoError;
		r.message = "not a directory: " + dir_path;
		return r;
	}

	// 1. Walk and collect file paths.
	struct Candidate {
		fs::path absolute;
		std::string relative; // forward-slash
	};
	std::vector<Candidate> files;

	fs::recursive_directory_iterator it(root,
			fs::directory_options::skip_permission_denied, ec);
	if (ec) {
		r.status = Status::IoError;
		r.message = "open dir failed: " + ec.message();
		return r;
	}

	const fs::recursive_directory_iterator end;
	for (; it != end; it.increment(ec)) {
		if (ec) {
			r.status = Status::IoError;
			r.message = "iterate failed: " + ec.message();
			return r;
		}
		const fs::directory_entry &de = *it;

		// Use symlink_status (not status) so symlinks are detected without
		// following them.
		std::error_code lec;
		fs::file_status lst = de.symlink_status(lec);
		if (lec) {
			r.status = Status::IoError;
			r.message = "lstat failed for " + de.path().string() + ": " + lec.message();
			return r;
		}

		if (fs::is_symlink(lst)) {
			// Skip symlinks entirely. If a symlink target is a directory we
			// don't want recursion to follow it either (recursive_directory_iterator
			// does not follow symlinks by default; this is just defensive).
			if (it.depth() >= 0 && fs::is_directory(de.symlink_status(lec))) {
				it.disable_recursion_pending();
			}
			continue;
		}

		auto filename = de.path().filename().string();

		if (fs::is_directory(lst)) {
			if (opts.skip_hidden_files && filename.size() > 0 && filename[0] == '.'){
				it.disable_recursion_pending();
				continue;
			}
			if (de.path().filename() == "_CodeSignature") {
				it.disable_recursion_pending();
				continue;
			}
			continue;
		}

		if (!fs::is_regular_file(lst)) {
			continue;
		}
		if (opts.skip_hidden_files && filename.size() > 0 && filename[0] == '.'){
			it.disable_recursion_pending();
			continue;
		}
		if (opts.skip_info_plist && filename == "Info.plist"){
			continue;
		}

		fs::path rel = fs::relative(de.path(), root, ec);
		if (ec) {
			r.status = Status::IoError;
			r.message = "relative() failed: " + ec.message();
			return r;
		}
		Candidate c;
		c.absolute = de.path();
		c.relative = to_forward_slash(rel);
		files.push_back(std::move(c));
	}

	// 2. Sort by relative path (lexicographic UTF-8 byte order).
	std::sort(files.begin(), files.end(),
			[](const Candidate &a, const Candidate &b) { return a.relative < b.relative; });

	// 3. Per-file hash + mix into directory context.
	unsigned_hash_sha256_ctx dir_ctx;
	unsigned_hash_sha256_init(&dir_ctx);

	r.entries.reserve(files.size());
	for (const auto &c : files) {
		std::vector<uint8_t> buf;
		std::string err;
		if (!read_entire_file(c.absolute, buf, err)) {
			r.status = Status::IoError;
			r.message = err;
			return r;
		}

		if (!add_hash_to_result(r, &dir_ctx, buf.data(), buf.size(), c.relative, opts.include_paths)) {
			return r;
		}
	}

	uint8_t digest[32];
	unsigned_hash_sha256_final(&dir_ctx, digest);
	std::memcpy(r.combined_sha256.data(), digest, 32);

	r.status = Status::Ok;
	return r;
}

HashResult hash_set(const std::vector<std::pair<std::string, std::pair<const uint8_t *, size_t>>> &files, DirectoryHashOptions opts) {
	HashResult r;
	r.format = Format::Directory;
	std::vector<std::pair<std::string, std::pair<const uint8_t *, size_t>>> sorted_files;
	for (const auto &pair : files) {
		auto &[path, file_data] = pair;
		// normalize the path
		std::string normalized_path = path;
		std::replace(normalized_path.begin(), normalized_path.end(), '\\', '/');
		sorted_files.push_back(std::make_pair(normalized_path, file_data));
	}
	std::sort(sorted_files.begin(), sorted_files.end(),
			[](const auto &a, const auto &b) { return a.first < b.first; });

	// 3. Per-file hash + mix into directory context.
	unsigned_hash_sha256_ctx dir_ctx;
	unsigned_hash_sha256_init(&dir_ctx);

	for (const auto &[path, file_data] : files) {
		std::filesystem::path p(path);
		// check if the path contains '_CodeSignature'
		auto filename = p.filename().string();
		size_t code_sig_idx = path.find("_CodeSignature/");
		if (code_sig_idx != std::string::npos && (code_sig_idx == 0 || path[code_sig_idx - 1] == '/')) {
			continue;
		}
		if (opts.skip_hidden_files && filename.size() > 0 && filename[0] == '.'){
			continue;
		}
		if (opts.skip_info_plist && filename == "Info.plist"){
			continue;
		}
		auto &[buf, buf_len] = file_data;
		if (!add_hash_to_result(r, &dir_ctx, buf, buf_len, path, opts.include_paths)) {
			return r;
		}
	}
	uint8_t digest[32];
	unsigned_hash_sha256_final(&dir_ctx, digest);
	std::memcpy(r.combined_sha256.data(), digest, 32);

	r.status = Status::Ok;
	return r;
}

} // namespace directory
} // namespace unsigned_hash
