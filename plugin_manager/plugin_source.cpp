#include "plugin_source.h"

#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "plugin_info.h"
#include "unsigned_hash/unsigned_hash.hpp"
#include "utility/common.h"
namespace {
unsigned_hash::HashResult hash_directory(const String &path) {
	auto paths = gdre::get_recursive_dir_list(path, {}, false, false);
	std::vector<std::pair<std::string, std::pair<const uint8_t *, size_t>>> files;
	Vector<Vector<uint8_t>> file_data;
	for (auto &p : paths) {
		if (p.find("_CodeSignature/") != String::npos) {
			continue;
		}
		if (p.get_file().begins_with(".")) {
			continue;
		}
		Error err = OK;
		file_data.push_back(FileAccess::get_file_as_bytes(path.path_join(p), &err));
		if (err != OK) {
			unsigned_hash::HashResult result;
			result.status = unsigned_hash::Status::IoError;
			result.message = "Failed to get file as bytes: ";
			result.message += p.utf8().get_data();
			return result;
		}
		auto &last = file_data[file_data.size() - 1];
		files.push_back(std::make_pair(p.utf8().get_data(), std::make_pair(last.ptr(), last.size())));
	}
	return unsigned_hash::hash_directory_set(files, { false, true, true });
}

String _get_unsigned_sha256(const String &path, bool *r_is_not_binary, bool *r_exists, bool *r_error) {
	auto da = DirAccess::create_for_path(path);
	if (da.is_null()) {
		*r_exists = false;
		return "";
	}
	bool is_dir = da->dir_exists(path);
	String sha256;
	if (is_dir || da->file_exists(path)) {
		*r_exists = true;
		unsigned_hash::HashResult result;
		if (is_dir) {
			result = hash_directory(path);
		} else {
			Error err = OK;
			auto data = FileAccess::get_file_as_bytes(path, &err);
			if (err != OK) {
				*r_error = true;
				ERR_FAIL_COND_V_MSG(err != OK, String(), "Failed to get file as bytes: " + path);
			}
			if (data.size() == 0) {
				*r_is_not_binary = true;
				return gdre::get_sha256(path);
			}
			result = unsigned_hash::hash(unsigned_hash::ByteView{ data.ptr(), (size_t)data.size() });
		}
		if (result.status == unsigned_hash::Status::NotABinary) {
			*r_is_not_binary = true;
			sha256 = gdre::get_sha256(path);
		} else if (result.status != unsigned_hash::Status::Ok) {
			*r_error = true;
			return "";
		} else {
			sha256 = String::hex_encode_buffer(result.combined_sha256.data(), result.combined_sha256.size());
		}
	} else {
		*r_exists = false;
	}
	return sha256;
}
} //namespace

PluginBin PluginSource::get_plugin_bin(const String &path, const SharedObject &obj) {
	PluginBin bin;
	bin.name = obj.path;
	bin.tags = obj.tags;
	if (path.is_empty()) {
		bin.exists = false;
		bin.sha256 = "";
		return bin;
	}
	bool is_not_binary = false;
	bool error = false;
	bin.sha256 = _get_unsigned_sha256(path, &is_not_binary, &bin.exists, &error);
	ERR_FAIL_COND_V_MSG(error, bin, "Failed to get unsigned SHA-256: " + path);
	if (bin.exists) {
		if (is_not_binary) {
			bin.verbatim_sha256 = bin.sha256;
		} else {
			bin.verbatim_sha256 = gdre::get_sha256(path);
		}
	}
	return bin;
}

String PluginSource::get_unsigned_sha256(const String &path) {
	bool is_not_binary = false;
	bool exists = false;
	bool error = false;
	String ret = _get_unsigned_sha256(path, &is_not_binary, &exists, &error);
	ERR_FAIL_COND_V_MSG(!exists, String(), "Plugin bin does not exist: " + path);
	ERR_FAIL_COND_V_MSG(error, String(), "Failed to get unsigned SHA-256: " + path);
	return ret;
}

bool PluginSource::handles_plugin(const String &plugin_name) {
	ERR_FAIL_V_MSG(false, "Not implemented");
}

bool PluginSource::is_default() {
	return false;
}

ReleaseInfo PluginSource::get_release_info(const String &plugin_name, int64_t primary_id, int64_t secondary_id, Error &r_connection_error) {
	r_connection_error = ERR_UNAVAILABLE;
	return ReleaseInfo();
}

void PluginSource::load_cache_internal() {
	ERR_FAIL_MSG("Not implemented");
}

void PluginSource::save_cache() {
	ERR_FAIL_MSG("Not implemented");
}

Vector<Pair<int64_t, int64_t>> PluginSource::get_plugin_version_numbers(const String &plugin_name, Error &r_connection_error) {
	r_connection_error = ERR_UNAVAILABLE;
	ERR_FAIL_V_MSG({}, "Not implemented");
}

void PluginSource::load_cache() {
	load_cache_internal();
}

String PluginSource::get_plugin_name() {
	ERR_FAIL_V_MSG(String(), "Not implemented");
}

Dictionary PluginSource::_get_plugin_version_numbers(const String &plugin_name) {
	Dictionary d;
	Error err = OK;
	auto pairs = get_plugin_version_numbers(plugin_name, err);
	ERR_FAIL_COND_V_MSG(err != OK, Dictionary(), "Failed to get plugin version numbers for plugin " + plugin_name);
	for (auto &E : pairs) {
		d[E.first] = E.second;
	}
	return d;
}

Dictionary PluginSource::_get_release_info(const String &plugin_name, int64_t primary_id, int64_t secondary_id) {
	Error err = OK;
	auto rel = get_release_info(plugin_name, primary_id, secondary_id, err);
	if (!rel.is_valid() || err) {
		return Dictionary();
	}
	return rel.to_json();
}

Vector<ReleaseInfo> PluginSource::find_release_infos_by_tag(const String &plugin_name, const String &tag, Error &r_error) {
	r_error = ERR_UNAVAILABLE;
	ERR_FAIL_V_MSG({}, "Not implemented");
}

void PluginSource::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_release_info", "plugin_name", "primary_id", "secondary_id"), &PluginSource::_get_release_info);
	ClassDB::bind_method(D_METHOD("get_plugin_version_numbers", "plugin_name"), &PluginSource::_get_plugin_version_numbers);
	ClassDB::bind_method(D_METHOD("load_cache"), &PluginSource::load_cache);
	ClassDB::bind_method(D_METHOD("save_cache"), &PluginSource::save_cache);
	ClassDB::bind_method(D_METHOD("handles_plugin", "plugin_name"), &PluginSource::handles_plugin);
	ClassDB::bind_method(D_METHOD("is_default"), &PluginSource::is_default);
}
