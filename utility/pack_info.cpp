#include "pack_info.h"

#include "core/object/class_db.h"

void PackInfo::init(
		String f, Ref<GodotVer> godot_ver, uint32_t fver, uint32_t flags, uint64_t base, uint32_t count, PackType tp, bool p_encrypted, bool p_suspect_version, String p_non_standard_header, String p_application_version) {
	pack_file = f;
	// copy the version, or set it to null if it's invalid
	if (godot_ver.is_valid() && godot_ver->is_valid_semver()) {
		version = GodotVer::create(godot_ver->get_major(), godot_ver->get_minor(), godot_ver->get_patch(), godot_ver->get_prerelease(), godot_ver->get_build_metadata());
	}
	fmt_version = fver;
	pack_flags = flags;
	file_base = base;
	file_count = count;
	type = tp;
	encrypted = p_encrypted;
	suspect_version = p_suspect_version;
	non_standard_header = p_non_standard_header;
	app_version = p_application_version;
}

bool PackInfo::has_unknown_version() {
	return !version.is_valid() || !version->is_valid_semver();
}

PackInfo::PackInfo() {
	version.instantiate();
}

String PackInfo::get_pack_file() const {
	return pack_file;
}

void PackInfo::set_pack_file(const String &p_pack_file) {
	pack_file = p_pack_file;
}

Ref<GodotVer> PackInfo::get_version() const {
	return GodotVer::create(version->get_major(), version->get_minor(), version->get_patch(), version->get_prerelease(), version->get_build_metadata());
}

void PackInfo::set_version(const Ref<GodotVer> &p_version) {
	version = p_version;
}

uint32_t PackInfo::get_fmt_version() const {
	return fmt_version;
}

void PackInfo::set_fmt_version(uint32_t p_fmt_version) {
	fmt_version = p_fmt_version;
}

uint32_t PackInfo::get_pack_flags() const {
	return pack_flags;
}

void PackInfo::set_pack_flags(uint32_t p_pack_flags) {
	pack_flags = p_pack_flags;
}

uint64_t PackInfo::get_file_base() const {
	return file_base;
}

void PackInfo::set_file_base(uint64_t p_file_base) {
	file_base = p_file_base;
}

uint32_t PackInfo::get_file_count() const {
	return file_count;
}

void PackInfo::set_file_count(uint32_t p_file_count) {
	file_count = p_file_count;
}

PackInfo::PackType PackInfo::get_type() const {
	return type;
}

void PackInfo::set_type(PackType p_type) {
	type = p_type;
}

bool PackInfo::is_encrypted() const {
	return encrypted;
}

void PackInfo::set_encrypted(bool p_encrypted) {
	encrypted = p_encrypted;
}

bool PackInfo::has_suspect_version() const {
	return suspect_version;
}

void PackInfo::set_suspect_version(bool p_suspect_version) {
	suspect_version = p_suspect_version;
}

String PackInfo::get_non_standard_header() const {
	return non_standard_header;
}

void PackInfo::set_non_standard_header(const String &p_non_standard_header) {
	non_standard_header = p_non_standard_header;
}

String PackInfo::get_app_version() const {
	return app_version;
}

void PackInfo::set_app_version(const String &p_app_version) {
	app_version = p_app_version;
}

void PackInfo::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_pack_file"), &PackInfo::get_pack_file);
	ClassDB::bind_method(D_METHOD("set_pack_file", "p_pack_file"), &PackInfo::set_pack_file);
	ClassDB::bind_method(D_METHOD("get_version"), &PackInfo::get_version);
	ClassDB::bind_method(D_METHOD("set_version", "p_version"), &PackInfo::set_version);
	ClassDB::bind_method(D_METHOD("get_fmt_version"), &PackInfo::get_fmt_version);
	ClassDB::bind_method(D_METHOD("set_fmt_version", "p_fmt_version"), &PackInfo::set_fmt_version);
	ClassDB::bind_method(D_METHOD("get_pack_flags"), &PackInfo::get_pack_flags);
	ClassDB::bind_method(D_METHOD("set_pack_flags", "p_pack_flags"), &PackInfo::set_pack_flags);
	ClassDB::bind_method(D_METHOD("get_file_base"), &PackInfo::get_file_base);
	ClassDB::bind_method(D_METHOD("set_file_base", "p_file_base"), &PackInfo::set_file_base);
	ClassDB::bind_method(D_METHOD("get_file_count"), &PackInfo::get_file_count);
	ClassDB::bind_method(D_METHOD("set_file_count", "p_file_count"), &PackInfo::set_file_count);
	ClassDB::bind_method(D_METHOD("get_type"), &PackInfo::get_type);
	ClassDB::bind_method(D_METHOD("set_type", "p_type"), &PackInfo::set_type);
	ClassDB::bind_method(D_METHOD("is_encrypted"), &PackInfo::is_encrypted);
	ClassDB::bind_method(D_METHOD("set_encrypted", "p_encrypted"), &PackInfo::set_encrypted);
	ClassDB::bind_method(D_METHOD("has_suspect_version"), &PackInfo::has_suspect_version);
	ClassDB::bind_method(D_METHOD("set_suspect_version", "p_suspect_version"), &PackInfo::set_suspect_version);
	ClassDB::bind_method(D_METHOD("get_non_standard_header"), &PackInfo::get_non_standard_header);
	ClassDB::bind_method(D_METHOD("set_non_standard_header", "p_non_standard_header"), &PackInfo::set_non_standard_header);
	ClassDB::bind_method(D_METHOD("get_app_version"), &PackInfo::get_app_version);
	ClassDB::bind_method(D_METHOD("set_app_version", "p_app_version"), &PackInfo::set_app_version);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "pack_file"), "set_pack_file", "get_pack_file");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "version", PROPERTY_HINT_RESOURCE_TYPE, GodotVer::get_class_static()), "set_version", "get_version");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "fmt_version"), "set_fmt_version", "get_fmt_version");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "pack_flags", PROPERTY_HINT_FLAGS, "ENCRYPTED:1,REL_FILEBASE:2,SPARSE_BUNDLE:4"), "set_pack_flags", "get_pack_flags");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "file_base"), "set_file_base", "get_file_base");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "file_count"), "set_file_count", "get_file_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "type", PROPERTY_HINT_ENUM, "PCK,APK,ZIP,DIR,EXE,UNKNOWN"), "set_type", "get_type");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "encrypted"), "set_encrypted", "is_encrypted");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "suspect_version"), "set_suspect_version", "has_suspect_version");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "non_standard_header"), "set_non_standard_header", "get_non_standard_header");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "app_version"), "set_app_version", "get_app_version");

	BIND_ENUM_CONSTANT(PCK);
	BIND_ENUM_CONSTANT(APK);
	BIND_ENUM_CONSTANT(ZIP);
	BIND_ENUM_CONSTANT(DIR);
	BIND_ENUM_CONSTANT(EXE);
	BIND_ENUM_CONSTANT(UNKNOWN);
}
