#include "plugin_manager/codeberg_source.h"

#include "core/error/error_list.h"
#include "utility/http_requester.h"

const String CodebergSource::codeberg_release_api_url = _codeberg_release_api_url;
namespace {
static const HashMap<String, Vector<String>> tag_masks = {
	{ "godotsteam", { "*gdn*", "*gde*" } },
	{ "godotsteam_server", { "*gdn*", "*gde*" } },
};

static const HashMap<String, Vector<String>> release_file_masks = {};

static const HashMap<String, Vector<String>> release_file_exclude_masks = {};

static const HashMap<String, String> plugin_map = {
	{ "godotsteam", "https://codeberg.org/godotsteam/godotsteam" },
	{ "godotsteam_server", "https://codeberg.org/godotsteam/godotsteam-server" },
};

} // namespace

CodebergSource::CodebergSource() {
	// Initialize any necessary resources
}

CodebergSource::~CodebergSource() {
	// Clean up any resources
}

const HashMap<String, String> &CodebergSource::get_plugin_repo_map() {
	return plugin_map;
}

const HashMap<String, Vector<String>> &CodebergSource::get_plugin_tag_masks() {
	return tag_masks;
}

const HashMap<String, Vector<String>> &CodebergSource::get_plugin_release_file_masks() {
	return release_file_masks;
}

const HashMap<String, Vector<String>> &CodebergSource::get_plugin_release_file_exclude_masks() {
	return release_file_exclude_masks;
}

String CodebergSource::get_plugin_name() {
	return "codeberg";
}

// Codeberg API behaves like the GitHub API, so all we have to do is return the correct URL and the page limit
const String &CodebergSource::get_release_api_url() {
	return codeberg_release_api_url;
}

int CodebergSource::get_release_page_limit() {
	return 30;
}

Error CodebergSource::make_request(const String &url, const Vector<String> &extra_headers, Vector<uint8_t> &response) {
	// more retries for codeberg since it's unreliable at times
	Error err = HTTPRequester::wget_sync(url, response, 10, 8, extra_headers);
	if (err == ERR_TIMEOUT) { // codeberg often passes back a 503 Service Unavailable error if the rate limit is exceeded; we'll treat this as a rate limit error
		err = ERR_BUSY;
	}
	return err;
}
