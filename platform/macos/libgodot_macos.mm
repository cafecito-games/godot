/**************************************************************************/
/*  libgodot_macos.mm                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "display_server_macos_embedded.h"
#include "os_macos.h"

#include "core/extension/godot_instance.h"
#include "core/extension/libgodot.h"
#include "core/object/class_db.h"
#include "main/main.h"

static OS_MacOS *os = nullptr;

static GodotInstance *instance = nullptr;
static bool embedded_driver_registered = false;
static bool embedded_class_registered = false;

static bool _is_user_args_delimiter(const char *p_arg) {
	return strcmp("--", p_arg) == 0 || strcmp("++", p_arg) == 0;
}

// Options that consume a following value token. The early selector must skip
// those values so a value that happens to look like a selector (for example a
// project directory literally named `--headless` after `--path`) is not treated
// as a host-mode request. This mirrors Main's string-valued options and is only
// a first pass; Main::setup() remains authoritative for full validation.
static bool _option_takes_value(const char *p_arg) {
	return strcmp("--path", p_arg) == 0 ||
			strcmp("-l", p_arg) == 0 ||
			strcmp("--language", p_arg) == 0 ||
			strcmp("--audio-driver", p_arg) == 0 ||
			strcmp("--rendering-driver", p_arg) == 0 ||
			strcmp("--rendering-method", p_arg) == 0 ||
			strcmp("--text-driver", p_arg) == 0 ||
			strcmp("--tablet-driver", p_arg) == 0 ||
			strcmp("--accessibility-driver", p_arg) == 0 ||
			strcmp("--log-file", p_arg) == 0 ||
			strcmp("-b", p_arg) == 0 ||
			strcmp("--breakpoints", p_arg) == 0 ||
			strcmp("--remote-fs", p_arg) == 0 ||
			strcmp("--remote-fs-password", p_arg) == 0 ||
			strcmp("--debug-server", p_arg) == 0 ||
			strcmp("--remote-debug", p_arg) == 0 ||
			strcmp("--validate-extension-api", p_arg) == 0 ||
			strcmp("--main-pack", p_arg) == 0 ||
			strcmp("--write-movie", p_arg) == 0 ||
			strcmp("--xr-mode", p_arg) == 0 ||
			strcmp("--benchmark-file", p_arg) == 0;
}

static bool _wants_embedded_driver(int p_argc, char *p_argv[]) {
	for (int i = 1; i < p_argc; i++) {
		if (_is_user_args_delimiter(p_argv[i])) {
			break;
		}
		if (strcmp("--embedded", p_argv[i]) == 0) {
			return true;
		}
		if (strcmp("--display-driver", p_argv[i]) == 0) {
			if (i < p_argc - 1 && strcmp("embedded", p_argv[i + 1]) == 0) {
				return true;
			}
			i++;
			continue;
		}
		if (_option_takes_value(p_argv[i]) && i < p_argc - 1) {
			i++;
		}
	}
	return false;
}

static bool _wants_headless(int p_argc, char *p_argv[]) {
	for (int i = 1; i < p_argc; i++) {
		if (_is_user_args_delimiter(p_argv[i])) {
			break;
		}
		if (strcmp("--headless", p_argv[i]) == 0) {
			return true;
		}
		if (strcmp("--display-driver", p_argv[i]) == 0) {
			if (i < p_argc - 1 && strcmp("headless", p_argv[i + 1]) == 0) {
				return true;
			}
			i++;
			continue;
		}
		if (_option_takes_value(p_argv[i]) && i < p_argc - 1) {
			i++;
		}
	}
	return false;
}

static bool _wants_ui_mode(int p_argc, char *p_argv[]) {
	for (int i = 1; i < p_argc; i++) {
		if (_is_user_args_delimiter(p_argv[i])) {
			break;
		}
		if (strcmp("--embedded", p_argv[i]) == 0) {
			return true;
		}
		if (strcmp("--display-driver", p_argv[i]) == 0) {
			if (i < p_argc - 1 && strcmp("headless", p_argv[i + 1]) != 0) {
				return true;
			}
			i++;
			continue;
		}
		if (_option_takes_value(p_argv[i]) && i < p_argc - 1) {
			i++;
		}
	}
	return false;
}

static bool _has_missing_display_driver_value(int p_argc, char *p_argv[]) {
	for (int i = 1; i < p_argc; i++) {
		if (_is_user_args_delimiter(p_argv[i])) {
			break;
		}
		if (strcmp("--display-driver", p_argv[i]) == 0) {
			if (i == p_argc - 1) {
				return true;
			}
			i++;
			continue;
		}
		if (_option_takes_value(p_argv[i]) && i < p_argc - 1) {
			i++;
		}
	}
	return false;
}

LIBGODOT_API GDExtensionObjectPtr libgodot_create_godot_instance(int p_argc, char *p_argv[], GDExtensionInitializationFunction p_init_func) {
	ERR_FAIL_COND_V_MSG(instance != nullptr, nullptr, "Only one Godot Instance may be created.");

	const bool wants_headless = _wants_headless(p_argc, p_argv);
	const bool wants_ui = _wants_ui_mode(p_argc, p_argv);

	if (_has_missing_display_driver_value(p_argc, p_argv)) {
		ERR_PRINT("LibGodot: --display-driver requires a value; refusing to create a Godot instance.");
		return nullptr;
	}

	if (wants_headless && wants_ui) {
		ERR_PRINT("LibGodot: conflicting host-mode selectors; --headless cannot be combined with --embedded or a non-headless --display-driver.");
		return nullptr;
	}

	if (!embedded_driver_registered && _wants_embedded_driver(p_argc, p_argv)) {
		DisplayServerMacOSEmbedded::register_embedded_driver();
		embedded_driver_registered = true;
	}

	uint32_t remaining_args = p_argc - 1;
	if (wants_headless) {
		os = memnew(OS_MacOS_Headless(p_argv[0], remaining_args, remaining_args > 0 ? &p_argv[1] : nullptr));
	} else {
		os = memnew(OS_MacOS_NSApp(p_argv[0], remaining_args, remaining_args > 0 ? &p_argv[1] : nullptr));
	}

	@autoreleasepool {
		Error err = Main::setup(p_argv[0], remaining_args, remaining_args > 0 ? &p_argv[1] : nullptr, false);
		if (err != OK) {
			memdelete(os);
			os = nullptr;
			return nullptr;
		}

		if (!embedded_class_registered) {
			ClassDB::register_abstract_class<DisplayServerMacOSEmbedded>();
			embedded_class_registered = true;
		}

		instance = memnew(GodotInstance);
		if (!instance->initialize(p_init_func)) {
			memdelete(instance);
			instance = nullptr;
			Main::cleanup();
			memdelete(os);
			os = nullptr;
			return nullptr;
		}

		return (GDExtensionObjectPtr)instance;
	}
}

LIBGODOT_API void libgodot_destroy_godot_instance(GDExtensionObjectPtr p_godot_instance) {
	GodotInstance *godot_instance = (GodotInstance *)p_godot_instance;
	if (instance == godot_instance) {
		godot_instance->stop();
		memdelete(godot_instance);
		instance = nullptr;
		Main::cleanup();
		if (os != nullptr) {
			memdelete(os);
			os = nullptr;
		}
	}
}
