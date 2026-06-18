#pragma once
#include "core/object/ref_counted.h"
#include "utility/godotver.h"

class PackInfo : public RefCounted {
	GDCLASS(PackInfo, RefCounted);

	friend class GDRESettings;

public:
	enum PackType {
		PCK,
		APK,
		ZIP,
		DIR,
		EXE,
		UNKNOWN
	};

private:
	String pack_file = "";
	Ref<GodotVer> version;
	uint32_t fmt_version = 0;
	uint32_t pack_flags = 0;
	uint64_t file_base = 0;
	uint32_t file_count = 0;
	PackType type = PCK;
	bool encrypted = false;
	bool suspect_version = false;
	String non_standard_header;
	String app_version;

public:
	void init(
			String f, Ref<GodotVer> godot_ver, uint32_t fver, uint32_t flags, uint64_t base, uint32_t count, PackType tp, bool p_encrypted = false, bool p_suspect_version = false, String p_non_standard_header = {}, String p_application_version = {});
	bool has_unknown_version();
	PackInfo();

	String get_pack_file() const;
	void set_pack_file(const String &p_pack_file);
	Ref<GodotVer> get_version() const;
	void set_version(const Ref<GodotVer> &p_version);
	uint32_t get_fmt_version() const;
	void set_fmt_version(uint32_t p_fmt_version);
	uint32_t get_pack_flags() const;
	void set_pack_flags(uint32_t p_pack_flags);
	uint64_t get_file_base() const;
	void set_file_base(uint64_t p_file_base);
	uint32_t get_file_count() const;
	void set_file_count(uint32_t p_file_count);
	PackType get_type() const;
	void set_type(PackType p_type);
	bool is_encrypted() const;
	void set_encrypted(bool p_encrypted);
	bool has_suspect_version() const;
	void set_suspect_version(bool p_suspect_version);
	String get_non_standard_header() const;
	void set_non_standard_header(const String &p_non_standard_header);
	String get_app_version() const;
	void set_app_version(const String &p_app_version);

protected:
	static void _bind_methods();
};

VARIANT_ENUM_CAST(PackInfo::PackType);
