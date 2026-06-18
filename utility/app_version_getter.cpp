#include "app_version_getter.h"
#include "core/io/file_access.h"
#include "core/io/xml_parser.h"
#include "core/object/class_db.h"

#include "utility/glob.h"

#define SKIP_UNTIL_NODE_TYPE(type)                                                  \
	{                                                                               \
		while (parser->get_node_type() != type && parser->read() != ERR_FILE_EOF) { \
		};                                                                          \
		if (parser->get_node_type() != type) {                                      \
			break;                                                                  \
		}                                                                           \
	}
#define SKIP_UNTIL_NODE_NAME(name)                                                                                                           \
	{                                                                                                                                        \
		while (!(parser->get_node_type() == XMLParser::NODE_ELEMENT && parser->get_node_name() == name) && parser->read() != ERR_FILE_EOF) { \
		};                                                                                                                                   \
		if (!(parser->get_node_type() == XMLParser::NODE_ELEMENT && parser->get_node_name() == name)) {                                      \
			break;                                                                                                                           \
		}                                                                                                                                    \
	}
String AppVersionGetter::get_version_from_info_plist(const String &p_path) {
	Ref<XMLParser> parser = memnew(XMLParser);
	Error err = parser->open(p_path);
	ERR_FAIL_COND_V_MSG(err != OK, "", "Failed to open info.plist file");

	bool not_in_dict = true;
	String short_version_string = "";
	String version = "";
	while (parser->read() != ERR_FILE_EOF) {
		if (not_in_dict && (parser->get_node_type() != XMLParser::NODE_ELEMENT || parser->get_node_name() != "dict")) {
			// parser->skip_section();
			continue;
		}
		not_in_dict = false;

		if (parser->get_node_type() == XMLParser::NODE_ELEMENT && parser->get_node_name() == "key") {
			SKIP_UNTIL_NODE_TYPE(XMLParser::NODE_TEXT);
			String key = parser->get_node_data();
			bool is_short_version = key == "CFBundleShortVersionString";
			if (is_short_version || key == "CFBundleVersion") {
				SKIP_UNTIL_NODE_NAME("string");
				SKIP_UNTIL_NODE_TYPE(XMLParser::NODE_TEXT);
				if (is_short_version) {
					short_version_string = parser->get_node_data();
				} else {
					version = parser->get_node_data();
				}
			}

			continue;
		}
	}
	if (version.is_empty()) {
		version = short_version_string;
	}
	return version;
}

String AppVersionGetter::get_version_from_app_or_framework(const String &p_path) {
	// just search for a plist
	Vector<String> paths = Glob::rglob(p_path.path_join("**/Info.plist"));
	if (paths.is_empty()) {
		return "";
	}
	for (const String &path : paths) {
		String version = get_version_from_info_plist(path);
		if (!version.is_empty()) {
			return version;
		}
	}
	return "";
}

#ifdef TOOLS_ENABLED
#define AVG_ERR_FAIL_COND_V_MSG(cond, ret, msg) ERR_FAIL_COND_V_MSG(cond, ret, msg)
#define AVG_ERR_FAIL_V_MSG(ret, msg) ERR_FAIL_V_MSG(ret, msg)
#else
#define AVG_ERR_FAIL_COND_V_MSG(cond, ret, msg) \
	if (unlikely(cond)) {                       \
		return ret;                             \
	}
#define AVG_ERR_FAIL_V_MSG(ret, msg) return ret;
#endif

// Parses the VS_VERSIONINFO resource (RT_VERSION, id 16) out of a PE file and returns the
// FileVersion string. Falls back to ProductVersion, then to the binary VS_FIXEDFILEINFO
// fields formatted as "Major.Minor.Build.Revision". Returns "" on any failure.
//
// Byte layout matches the writer in platform/windows/export/template_modifier.cpp.
String AppVersionGetter::get_version_from_windows_exe_versioninfo(const String &p_path) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	if (f.is_null()) {
		return "";
	}

	// Locate the PE header (mirrors GDREPackedSource::seek_after_magic_windows).
	f->seek(0x3c);
	uint32_t pe_pos = f->get_32();
	if (pe_pos == 0 || pe_pos > f->get_length()) {
		return "";
	}
	f->seek(pe_pos);
	if (f->get_32() != 0x00004550) { // "PE\0\0"
		return "";
	}

	const int64_t coff_start = f->get_position();
	f->seek(coff_start + 2);
	int num_sections = f->get_16();
	f->seek(coff_start + 16);
	uint16_t opt_header_size = f->get_16();
	if (num_sections <= 0 || opt_header_size == 0) {
		return "";
	}
	const int64_t optional_header_start = coff_start + 20;

	f->seek(optional_header_start);
	uint16_t pe_magic = f->get_16();
	bool pe32plus;
	if (pe_magic == 0x10b) {
		pe32plus = false;
	} else if (pe_magic == 0x20b) {
		pe32plus = true;
	} else {
		return "";
	}

	// DataDirectory[2] (Resource Table): RVA u32 + Size u32. The DataDirectory array
	// itself starts at +96 (PE32) / +112 (PE32+) inside the optional header, so entry [2]
	// is at base + 2*8. Cross-check: TemplateModifier writes the resource size at
	// +116 (PE32) / +132 (PE32+), which is DataDirectory[2].Size = base + 4.
	const uint32_t resource_dd_offset = pe32plus ? 128 : 112;
	if (resource_dd_offset + 8 > opt_header_size) {
		return "";
	}
	f->seek(optional_header_start + resource_dd_offset);
	uint32_t resource_rva = f->get_32();
	f->get_32(); // resource_size
	if (resource_rva == 0) {
		return "";
	}

	// Section table follows the optional header.
	const int64_t section_table_pos = optional_header_start + opt_header_size;
	uint32_t resource_section_va = 0;
	uint32_t resource_section_raw = 0;
	bool found_section = false;
	for (int i = 0; i < num_sections; ++i) {
		f->seek(section_table_pos + i * 40 + 8);
		uint32_t virtual_size = f->get_32();
		uint32_t virtual_address = f->get_32();
		uint32_t size_of_raw_data = f->get_32();
		uint32_t pointer_to_raw_data = f->get_32();
		uint32_t span = MAX(virtual_size, size_of_raw_data);
		if (resource_rva >= virtual_address && resource_rva < virtual_address + span) {
			resource_section_va = virtual_address;
			resource_section_raw = pointer_to_raw_data;
			found_section = true;
			break;
		}
	}
	if (!found_section) {
		AVG_ERR_FAIL_V_MSG("", "Could not find resource section in Windows executable");
	}

	auto rva_to_offset = [&](uint32_t rva) -> int64_t {
		return (int64_t)resource_section_raw + ((int64_t)rva - (int64_t)resource_section_va);
	};

	const uint64_t file_length = f->get_length();
	const int64_t resource_root_offset = (int64_t)resource_section_raw + ((int64_t)resource_rva - (int64_t)resource_section_va);
	if (resource_root_offset < 0 || (uint64_t)resource_root_offset >= file_length) {
		return "";
	}

	// Walk the three-level resource directory tree. Sub-offsets in directory entries are
	// relative to the root of the resource section (i.e. resource_root_offset).
	auto seek_directory_entry = [&](int64_t directory_offset, int level, uint32_t &r_next_offset, bool &r_is_subdir) -> bool {
		// Reads one entry from a directory at directory_offset matching the per-level
		// criterion (level 1: id == 16; level 2/3: first entry).
		f->seek(directory_offset + 12);
		uint16_t named_count = f->get_16();
		uint16_t id_count = f->get_16();
		const int64_t entries_pos = directory_offset + 16;

		if (level == 1) {
			// Skip named entries; only ID entries matter for RT_VERSION.
			for (int i = 0; i < id_count; ++i) {
				f->seek(entries_pos + (int64_t)(named_count + i) * 8);
				uint32_t name_or_id = f->get_32();
				uint32_t off = f->get_32();
				if ((name_or_id & 0x80000000u) == 0 && name_or_id == 16) {
					r_is_subdir = (off & 0x80000000u) != 0;
					r_next_offset = off & 0x7fffffffu;
					return true;
				}
			}
			AVG_ERR_FAIL_V_MSG(false, "Could not find ID entry for RT_VERSION in Windows executable at offset " + itos(directory_offset));
		}

		if (named_count + id_count == 0) {
			AVG_ERR_FAIL_V_MSG(false, "No entries found in directory at offset " + itos(directory_offset));
		}
		f->seek(entries_pos);
		f->get_32(); // name_or_id
		uint32_t off = f->get_32();
		r_is_subdir = (off & 0x80000000u) != 0;
		r_next_offset = off & 0x7fffffffu;
		return true;
	};

	uint32_t next_rel = 0;
	bool is_subdir = false;
	if (!seek_directory_entry(resource_root_offset, 1, next_rel, is_subdir) || !is_subdir) {
		// specific error messages for all the below cases
		AVG_ERR_FAIL_V_MSG("", vformat("Failed to find directory entry level 1 in Windows executable at offset %d", resource_root_offset));
	}
	int64_t level2_offset = resource_root_offset + next_rel;
	if (level2_offset < 0 || (uint64_t)level2_offset >= file_length) {
		AVG_ERR_FAIL_V_MSG("", vformat("Directory entry level 2 offset %d exceeds file length %d", level2_offset, file_length));
	}
	if (!seek_directory_entry(level2_offset, 2, next_rel, is_subdir) || !is_subdir) {
		AVG_ERR_FAIL_V_MSG("", vformat("Failed to find directory entry level 2 in Windows executable at offset %d", level2_offset));
	}
	int64_t level3_offset = resource_root_offset + next_rel;
	if (level3_offset < 0 || (uint64_t)level3_offset >= file_length) {
		AVG_ERR_FAIL_V_MSG("", vformat("Directory entry level 3 offset %d exceeds file length %d", level3_offset, file_length));
	}
	if (!seek_directory_entry(level3_offset, 3, next_rel, is_subdir) || is_subdir) {
		AVG_ERR_FAIL_V_MSG("", vformat("Failed to find directory entry level 3 in Windows executable at offset %d", level3_offset));
	}
	int64_t data_entry_offset = resource_root_offset + next_rel;
	if (data_entry_offset < 0 || (uint64_t)(data_entry_offset + 16) > file_length) {
		AVG_ERR_FAIL_V_MSG("", vformat("IMAGE_RESOURCE_DATA_ENTRY offset %d exceeds file length %d", data_entry_offset, file_length));
	}

	// IMAGE_RESOURCE_DATA_ENTRY: rva u32, size u32, codepage u32, reserved u32.
	f->seek(data_entry_offset);
	uint32_t blob_rva = f->get_32();
	uint32_t blob_size = f->get_32();
	if (blob_size < 8 || blob_size > 64 * 1024) { // VS_VERSIONINFO is small; cap to be safe.
		AVG_ERR_FAIL_V_MSG("", vformat("IMAGE_RESOURCE_DATA_ENTRY size %d is out of bounds", blob_size));
	}
	int64_t blob_file_offset = rva_to_offset(blob_rva);
	if (blob_file_offset < 0 || (uint64_t)(blob_file_offset + blob_size) > file_length) {
		AVG_ERR_FAIL_V_MSG("", vformat("IMAGE_RESOURCE_DATA_ENTRY file offset %d exceeds file length %d", blob_file_offset, file_length));
	}

	f->seek(blob_file_offset);
	Vector<uint8_t> blob = f->get_buffer(blob_size);
	f.unref();
	if ((uint32_t)blob.size() != blob_size) {
		AVG_ERR_FAIL_V_MSG("", vformat("IMAGE_RESOURCE_DATA_ENTRY buffer size %d does not match expected size %d", blob.size(), blob_size));
	}

	const uint8_t *b = blob.ptr();
	const uint32_t blob_len = blob.size();

	auto read_u16 = [&](uint32_t off) -> uint16_t {
		return (uint16_t)b[off] | ((uint16_t)b[off + 1] << 8);
	};
	auto read_u32 = [&](uint32_t off) -> uint32_t {
		return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) | ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
	};
	auto align4 = [](uint32_t off) -> uint32_t {
		return (off + 3) & ~3u;
	};
	// Reads a null-terminated UTF-16LE string starting at `off`, advancing `off` past the
	// terminator. Returns "" and clamps `off` to `limit` if the string runs off the end.
	auto read_utf16_string = [&](uint32_t &off, uint32_t limit) -> String {
		uint32_t start = off;
		uint32_t cur = off;
		while (cur + 2 <= limit) {
			uint16_t w = read_u16(cur);
			cur += 2;
			if (w == 0) {
				break;
			}
		}
		if (cur > limit) {
			off = limit;
			return String();
		}
		uint32_t bytes = cur - start;
		uint32_t chars = bytes >= 2 ? (bytes - 2) / 2 : 0;
		String s;
		if (chars > 0) {
			s.append_utf16((const char16_t *)(b + start), chars);
		}
		off = cur;
		return s;
	};

	// VS_VERSIONINFO root.
	if (blob_len < 6) {
		AVG_ERR_FAIL_V_MSG("", vformat("VS_VERSIONINFO root is too small: %d bytes", blob_len));
	}
	uint32_t node_len = read_u16(0);
	uint32_t value_len = read_u16(2);
	// uint16_t node_type = read_u16(4); // 0 = binary, 1 = text; not enforced here.
	if (node_len == 0 || node_len > blob_len) {
		AVG_ERR_FAIL_V_MSG("", vformat("VS_VERSIONINFO root length %d is out of bounds: %d bytes", node_len, blob_len));
	}

	uint32_t cursor = 6;
	String root_key = read_utf16_string(cursor, node_len);
	if (root_key != "VS_VERSION_INFO") {
		AVG_ERR_FAIL_V_MSG("", vformat("VS_VERSIONINFO root key is not VS_VERSION_INFO: %s", root_key));
	}
	cursor = align4(cursor);

	uint32_t file_version_ms = 0;
	uint32_t file_version_ls = 0;
	uint32_t product_version_ms = 0;
	uint32_t product_version_ls = 0;
	if (value_len >= 52 && cursor + 52 <= node_len) {
		uint32_t signature = read_u32(cursor);
		if (signature == 0xfeef04bd) {
			// Skip signature + struct_version.
			file_version_ms = read_u32(cursor + 8);
			file_version_ls = read_u32(cursor + 12);
			product_version_ms = read_u32(cursor + 16);
			product_version_ls = read_u32(cursor + 20);
		}
	}
	cursor += value_len;
	cursor = align4(cursor);

	HashMap<String, String> strings;

	// Iterate children of the root (StringFileInfo / VarFileInfo blocks).
	while (cursor + 6 <= node_len) {
		uint32_t child_start = cursor;
		uint32_t child_len = read_u16(cursor);
		uint32_t child_value_len = read_u16(cursor + 2);
		// uint16_t child_type = read_u16(cursor + 4);
		if (child_len < 6 || child_start + child_len > node_len) {
			break;
		}
		uint32_t child_end = child_start + child_len;
		uint32_t inner = cursor + 6;
		String child_key = read_utf16_string(inner, child_end);
		inner = align4(inner);
		(void)child_value_len; // StringFileInfo/VarFileInfo headers carry value_length 0.

		if (child_key == "StringFileInfo") {
			// Children are StringTable blocks.
			while (inner + 6 <= child_end) {
				uint32_t table_start = inner;
				uint32_t table_len = read_u16(inner);
				if (table_len < 6 || table_start + table_len > child_end) {
					break;
				}
				uint32_t table_end = table_start + table_len;
				uint32_t s_off = inner + 6;
				String table_key = read_utf16_string(s_off, table_end);
				(void)table_key; // Lang/codepage like "040904b0"; ignored.
				s_off = align4(s_off);

				// Children of the StringTable are String entries.
				while (s_off + 6 <= table_end) {
					uint32_t str_start = s_off;
					uint32_t str_len = read_u16(s_off);
					uint32_t str_value_len = read_u16(s_off + 2); // In characters per MSDN.
					if (str_len < 6 || str_start + str_len > table_end) {
						break;
					}
					uint32_t str_end = str_start + str_len;
					uint32_t k_off = s_off + 6;
					String key = read_utf16_string(k_off, str_end);
					k_off = align4(k_off);
					String value;
					if (str_value_len > 0 && k_off < str_end) {
						uint32_t v_off = k_off;
						value = read_utf16_string(v_off, str_end);
					}
					if (!key.is_empty()) {
						strings[key] = value;
					}
					s_off = align4(str_end);
				}

				inner = align4(table_end);
			}
		}
		// VarFileInfo and any unknown blocks: skipped by advancing to child_end below.

		cursor = align4(child_end);
	}

	if (strings.has("FileVersion") && !strings["FileVersion"].is_empty()) {
		return strings["FileVersion"];
	}
	if (strings.has("ProductVersion") && !strings["ProductVersion"].is_empty()) {
		return strings["ProductVersion"];
	}
	if (file_version_ms != 0 || file_version_ls != 0) {
		return vformat("%d.%d.%d.%d",
				(file_version_ms >> 16) & 0xffff,
				file_version_ms & 0xffff,
				(file_version_ls >> 16) & 0xffff,
				file_version_ls & 0xffff);
	}
	if (product_version_ms != 0 || product_version_ls != 0) {
		return vformat("%d.%d.%d.%d",
				(product_version_ms >> 16) & 0xffff,
				product_version_ms & 0xffff,
				(product_version_ls >> 16) & 0xffff,
				product_version_ls & 0xffff);
	}
	AVG_ERR_FAIL_V_MSG("", "Could not find any version string in Windows executable");
}

void AppVersionGetter::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_version_from_info_plist", "path"), &AppVersionGetter::get_version_from_info_plist);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_version_from_windows_exe_versioninfo", "path"), &AppVersionGetter::get_version_from_windows_exe_versioninfo);
}
