#pragma once

#include "test_common.h"
#include "tests/test_macros.h"

namespace TestResourceExport {

void test_import_options();
Error test_export_sample(const String &version);
Error test_export_oggvorbisstr(const String &version);
Error test_export_texture(const String &version);
Error test_export_bmfont(const String &version);
String get_resource_type_dir(const String &version, const String &resource_type);
bool check_if_resource_type_dir_exists(const String &version, const String &resource_type);

TEST_CASE("[GDSDecomp][ResourceExport] Test import options") {
	test_import_options();
}

TEST_CASE("[GDSDecomp][ResourceExport] Export sample") {
	Vector<String> versions = get_test_versions();
	CHECK(versions.size() > 0);

	for (const String &version : versions) {
		if (!check_if_resource_type_dir_exists(version, "sample")) {
			continue;
		}
		SUBCASE(vformat("%s: Test export sample", version).utf8().get_data()) {
			test_export_sample(version);
		}
	}
}

TEST_CASE("[GDSDecomp][ResourceExport] Export oggvorbisstr") {
	Vector<String> versions = get_test_versions();
	CHECK(versions.size() > 0);

	for (const String &version : versions) {
		if (!check_if_resource_type_dir_exists(version, "ogg")) {
			continue;
		}
		SUBCASE(vformat("%s: Test export oggvorbisstr", version).utf8().get_data()) {
			test_export_oggvorbisstr(version);
		}
	}
}

TEST_CASE("[GDSDecomp][ResourceExport] Export texture") {
	Vector<String> versions = get_test_versions();
	CHECK(versions.size() > 0);

	for (const String &version : versions) {
		if (!check_if_resource_type_dir_exists(version, "texture")) {
			continue;
		}
		SUBCASE(vformat("%s: Test export texture", version).utf8().get_data()) {
			test_export_texture(version);
		}
	}
}

TEST_CASE("[GDSDecomp][ResourceExport] Export bmfont") {
	Vector<String> versions = get_test_versions();
	CHECK(versions.size() > 0);

	for (const String &version : versions) {
		if (!check_if_resource_type_dir_exists(version, "bmfont")) {
			continue;
		}
		SUBCASE(vformat("%s: Test export bmfont", version).utf8().get_data()) {
			test_export_bmfont(version);
		}
	}
}

} // namespace TestResourceExport
