/**************************************************************************/
/*  foreground_presentation.h                                             */
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

#pragma once

// Main-thread state for a native presentation in the current Godot scene.
// Scene state and token lifetime are independent: ending a purchase while its
// scene is still inactive must not allow input or resume the rendering loop.
class AppleEmbeddedForegroundPresentation {
	const void *token = nullptr;
	const void *scene = nullptr;
	bool inactive = false;

public:
	bool begin(const void *p_token, const void *p_active_scene) {
		if (!p_token || !p_active_scene || token || inactive) {
			return false;
		}
		token = p_token;
		scene = p_active_scene;
		return true;
	}

	void end(const void *p_token) {
		if (token != p_token) {
			return;
		}
		token = nullptr;
		if (!inactive) {
			scene = nullptr;
		}
	}

	void resign_active(const void *p_scene) {
		if (scene && scene == p_scene) {
			inactive = true;
		}
	}

	void activate(const void *p_scene) {
		if (scene == p_scene) {
			inactive = false;
			if (!token) {
				scene = nullptr;
			}
		}
	}

	void leave_foreground() {
		token = nullptr;
		scene = nullptr;
		inactive = false;
	}

	bool should_iterate(const void *p_scene) const {
		return inactive && token && scene == p_scene;
	}

	bool suppresses_input() const {
		return token || inactive;
	}

	bool is_inactive() const {
		return inactive;
	}
};
