#ifndef PCK_DUMPER_H
#define PCK_DUMPER_H

#include "core/object/object.h"
#include "core/object/ref_counted.h"

#include "packed_file_info.h"
class PckDumper : public RefCounted {
	GDCLASS(PckDumper, RefCounted)
	bool skip_malformed_paths = false;
	bool skip_failed_md5 = false;
	bool should_check_md5 = false;
	bool opt_multi_thread = true;
	std::atomic<bool> encryption_error = false;
	std::atomic<int> completed_cnt = 0;
	std::atomic<int> skipped_cnt = 0;
	std::atomic<int> broken_cnt = 0;

	bool _pck_file_check_md5(Ref<PackedFileInfo> &file);
	void _do_md5_check(uint32_t i, Ref<PackedFileInfo> *tokens);
	String get_file_description(int64_t i, Ref<PackedFileInfo> *userdata);
	void reset();
	struct ExtractToken {
		Ref<PackedFileInfo> file;
		String output_dir;
		Error err = OK;
	};
	void _do_extract(uint32_t i, ExtractToken *tokens);
	String get_extract_token_description(int64_t i, ExtractToken *userdata);

protected:
	static void _bind_methods();

public:
	bool had_encryption_error() const { return encryption_error; }

	Error check_md5_all_files();
	Error _check_md5_all_files(Vector<String> &broken_files, int &checked_files);

	Error _pck_dump_to_dir(const String &dir, const Vector<String> &files_to_extract, String &error_string);
	Error pck_dump_to_dir(const String &dir, const Vector<String> &files_to_extract);

	void set_multi_thread(bool multi_thread) { opt_multi_thread = multi_thread; }
	//Error pck_dump_to_dir(const String &dir, const Vector<String> &files_to_extract);
};

#endif // PCK_DUMPER_H
