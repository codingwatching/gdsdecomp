#pragma once

#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/os/mutex.h"

#include "plugin_info.h"
#include "plugin_source.h"

#include "utility/task_manager.h"

class PluginManager : public Object {
	GDCLASS(PluginManager, Object)

private:
	static PluginManager *singleton;
	static constexpr const char *PLUGIN_CACHE_ENV_VAR = "GDRE_PLUGIN_CACHE_DIR";
	static constexpr const char *STATIC_PLUGIN_CACHE_PATH = "res://gdre_static_plugin_cache.json";
	static constexpr const int MAX_SOURCES = 64;
	static Ref<PluginSource> sources[MAX_SOURCES];
	static int source_count;
	static bool prepopping;
	static HashMap<String, PluginVersion> plugin_version_cache;
	static Mutex plugin_version_cache_mutex;
	static HashMap<String, Pair<String, String>> known_bad_plugin_versions;

	// Source management
	static Ref<PluginSource> get_source(const String &plugin_name);
	static void load_plugin_version_cache_file(const String &cache_file, bool prepop = false);
	static void load_plugin_version_cache();
	static void save_plugin_version_cache();
	static Error save_plugin_version_static_cache(const String &output_path);
	static Error populate_plugin_version_hashes(PluginVersion &plugin_version);
	static String get_download_path_for_release(const ReleaseInfo &p_release_info);
	static TaskManager::DownloadTaskID start_download_plugin_to_cache(const ReleaseInfo &p_release_info, const String &p_sha256_sum, String &r_save_path, Error &r_error);

	static PluginVersion _get_plugin_version_for_current_release_info(const ReleaseInfo &release_info, Error &r_error);

protected:
	static void _bind_methods();

	static Dictionary _get_plugin_info(const String &plugin_name, const Vector<String> &hashes);

public:
	static void _init();
	// Main interface methods
	static String get_plugin_download_cache_path();
	static String get_plugin_cache_path();
	static TaskManager::DownloadTaskID download_plugin(const PluginVersion &p_plugin_version, String &r_save_path, Error &r_error);
	static PluginVersion get_plugin_info(const String &plugin_name, const Vector<String> &hashes);
	static Vector<PluginVersion> get_possibles_from_deps(const String &plugin_name, const Ref<GodotVer> engine_version, const Vector<PluginBin> &deps);
	static void load_cache();
	static void save_cache();
	static Error prepop_cache(const Vector<String> &plugin_names, const String &output_path);
	static bool is_prepopping();
	// PluginVersion cache management
	static PluginVersion get_cached_plugin_version(const String &cache_key);
	static void erase_cached_plugin_version(const String &cache_key);
	static void cache_plugin_version(const String &cache_key, const PluginVersion &version);
	static PluginVersion populate_plugin_version_from_release(const ReleaseInfo &release_info, Error &r_error);
	static PluginVersion get_plugin_version_for_key(const String &plugin_name, int64_t primary_id, int64_t secondary_id, Error &r_error);
	// Source management
	static void register_source(Ref<PluginSource> source, bool at_front = false);
	static void unregister_source(Ref<PluginSource> source);
	static void print_plugin_cache();
	static void clear_plugin_cache(bool clear_static_cache = false);
	static void clear_download_cache();
};
