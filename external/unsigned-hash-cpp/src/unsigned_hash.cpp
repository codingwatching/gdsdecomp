// Public API surface for unsigned-hash-cpp.

#include "unsigned_hash/unsigned_hash.hpp"

#include "directory.hpp"
#include "macho.hpp"
#include "pe.hpp"
#include "sha256.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace unsigned_hash {

const char *status_to_string(Status s) {
	switch (s) {
		case Status::Ok: return "Ok";
		case Status::NotABinary: return "NotABinary";
		case Status::Truncated: return "Truncated";
		case Status::Malformed: return "Malformed";
		case Status::Unsupported: return "Unsupported";
		case Status::IoError: return "IoError";
	}
	return "Unknown";
}

const char *format_to_string(Format f) {
	switch (f) {
		case Format::Unknown: return "Unknown";
		case Format::PE: return "PE";
		case Format::MachO: return "MachO";
		case Format::FatMachO: return "FatMachO";
		case Format::Directory: return "Directory";
	}
	return "Unknown";
}

std::string to_hex(const std::array<uint8_t, 32> &digest) {
	static const char *hex = "0123456789abcdef";
	std::string out;
	out.resize(64);
	for (size_t i = 0; i < 32; ++i) {
		out[i * 2 + 0] = hex[(digest[i] >> 4) & 0xf];
		out[i * 2 + 1] = hex[digest[i] & 0xf];
	}
	return out;
}

HashResult hash(ByteView data) {
	HashResult r;
	if (data.size == 0 || data.data == nullptr) {
		r.status = Status::NotABinary;
		r.message = "empty input";
		return r;
	}

	if (pe::is_pe(data.data, data.size)) {
		SliceHash sh;
		std::string msg;
		Status st = pe::hash_authenticode(data.data, data.size, sh, msg);
		r.status = st;
		r.format = Format::PE;
		r.message = std::move(msg);
		if (st == Status::Ok) {
			r.slices.push_back(sh);
			r.combined_sha256 = sh.sha256;
		}
		return r;
	}

	if (macho::is_fat_macho(data.data, data.size)) {
		std::vector<SliceHash> slices;
		std::array<uint8_t, 32> combined{};
		std::string msg;
		Status st = macho::hash_fat(data.data, data.size, slices, combined, msg);
		r.status = st;
		r.format = Format::FatMachO;
		r.message = std::move(msg);
		if (st == Status::Ok) {
			r.slices = std::move(slices);
			r.combined_sha256 = combined;
		}
		return r;
	}

	if (macho::is_thin_macho(data.data, data.size)) {
		SliceHash sh;
		std::string msg;
		Status st = macho::hash_thin(data.data, data.size, 0, data.size, sh, msg);
		r.status = st;
		r.format = Format::MachO;
		r.message = std::move(msg);
		if (st == Status::Ok) {
			r.slices.push_back(sh);
			r.combined_sha256 = sh.sha256;
		}
		return r;
	}

	r.status = Status::NotABinary;
	r.format = Format::Unknown;
	r.message = "unrecognized magic";
	return r;
}

HashResult hash_directory(const std::string &dir_path, DirectoryHashOptions opts) {
	return directory::hash(dir_path, opts);
}

HashResult hash_file(const std::string &path) {
	HashResult r;
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f) {
		r.status = Status::IoError;
		r.message = "cannot open file";
		return r;
	}
	std::streamsize sz = f.tellg();
	if (sz < 0) {
		r.status = Status::IoError;
		r.message = "cannot determine file size";
		return r;
	}
	f.seekg(0, std::ios::beg);
	std::vector<uint8_t> buf((size_t)sz);
	if (sz > 0 && !f.read(reinterpret_cast<char *>(buf.data()), sz)) {
		r.status = Status::IoError;
		r.message = "read error";
		return r;
	}
	return hash(ByteView{ buf.data(), buf.size() });
}

HashResult hash_directory_set(const std::vector<std::pair<std::string, std::pair<const uint8_t *, size_t>>> &files, DirectoryHashOptions opts) {
	return directory::hash_set(files, opts);
}

} // namespace unsigned_hash
