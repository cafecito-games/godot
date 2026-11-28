/**************************************************************************/
/*  ForegroundPresentationState.kt                                        */
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

package org.godotengine.godot

/**
 * Tracks an opt-in native foreground presentation (such as a translucent Play Billing activity)
 * while the Godot host activity stays visible underneath.
 *
 * Token lifetime and activity visibility are independent: ending the token while the activity is
 * still paused keeps input suppressed until resume and still emits the deferred application pause
 * if the activity stops before resuming.
 */
internal class ForegroundPresentationState {
	enum class PauseAction {
		NORMAL_PAUSE,
		VISIBLE_PRESENTATION_PAUSE,
		ALREADY_PAUSED_VISIBLE,
	}

	enum class ResumeAction {
		NORMAL_RESUME,
		VISIBLE_PRESENTATION_RESUME,
	}

	private var activeToken: Any? = null
	private var pausedVisible: Boolean = false

	val suppressesInput: Boolean
		get() = activeToken != null || pausedVisible

	val isPausedVisible: Boolean
		get() = pausedVisible

	fun begin(token: Any?, canBegin: Boolean): Boolean {
		if (token == null || !canBegin || activeToken != null || pausedVisible) {
			return false
		}
		activeToken = token
		return true
	}

	fun end(token: Any?): Boolean {
		if (token == null || activeToken !== token) {
			return false
		}
		activeToken = null
		return true
	}

	fun onPause(): PauseAction {
		if (pausedVisible) {
			return PauseAction.ALREADY_PAUSED_VISIBLE
		}
		if (activeToken != null) {
			pausedVisible = true
			return PauseAction.VISIBLE_PRESENTATION_PAUSE
		}
		return PauseAction.NORMAL_PAUSE
	}

	fun onResume(): ResumeAction {
		if (pausedVisible) {
			pausedVisible = false
			return ResumeAction.VISIBLE_PRESENTATION_RESUME
		}
		return ResumeAction.NORMAL_RESUME
	}

	/**
	 * Clears the presentation scope on activity stop.
	 *
	 * @return true if [onPause] deferred the application-paused notification and the caller must
	 *         emit it before pausing the render thread.
	 */
	fun onStop(): Boolean {
		val shouldEmitDeferredPause = pausedVisible
		activeToken = null
		pausedVisible = false
		return shouldEmitDeferredPause
	}

	fun onDestroy() {
		activeToken = null
		pausedVisible = false
	}
}
