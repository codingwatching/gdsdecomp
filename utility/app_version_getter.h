#pragma once
#include "core/object/object.h"
#include "core/string/ustring.h"
class AppVersionGetter : public Object {
	GDCLASS(AppVersionGetter, Object);

protected:
	static void _bind_methods();

public:
	static String get_version_from_info_plist(const String &p_path);
	static String get_version_from_windows_exe_versioninfo(const String &p_path);
};
