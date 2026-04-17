#pragma once
#include "core/templates/hash_set.h"
#include "core/variant/variant.h"

#include "modules/gdsdecomp/utility/gdre_version.gen.h"

namespace gdre {
static const HashSet<String> main_commands = {
	// TODO: Auto-generate this list somehow
	"--recover",
	"--extract",
	"--compile",
	"--list-bytecode-versions",
	"--pck-create",
	"--pck-patch",
	"--txt-to-bin",
	"--bin-to-txt",
	"--patch-translations",
	"--gdre-help",
	"--gdre-version",
};

static void print_version() {
	print_line("Godot RE Tools " + String(GDRE_VERSION));
}

// returns true if we should quit the program immediately
static bool modify_cli_args(List<String> *args, List<String> *user_args) {
	Vector<String> engine_args;
	bool found_help = false;
	String path = "";
	for (List<String>::Element *E = args->front(); E; E = E->next()) {
		String arg = E->get();
		if (arg == "--path" && E->next()) {
			path = E->next()->get();
		} else if (arg == "--help" || arg == "--gdre-help") {
			found_help = true;
		} else if (arg == "--gdre-version" || arg == "--version") {
			print_version();
			return true;
		} else if (arg == "--godot-version") {
			args->clear();
			user_args->clear();
			args->push_back("--version");
			return false;
		} else if (arg == "--godot-help") {
			args->clear();
			user_args->clear();
			args->push_back("--help");
			return false;
		}
	}
	if (found_help) {
		args->clear();
		args->push_back("--headless");
		args->push_back("--no-header");
		if (!path.is_empty()) {
			args->push_back("--path");
			args->push_back(path);
		}
		user_args->clear();
		user_args->push_back("--gdre-help");
		return false;
	}
	for (List<String>::Element *E = args->front(); E; E = E->next()) {
		String arg = E->get();
		String main_command = arg.get_slice("=", 0);

		if (arg == "--" || arg == "++"){
			break;
		}
		if (main_commands.has(main_command)) {
			args->insert_before(E, "--");
			break;
		}
	}
	return false;
}
} //namespace gdre
