/**************************************************************************/
/*  GodotRenderView.java                                                  */
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

package org.godotengine.godot;

import org.godotengine.godot.input.GodotInputHandler;

import android.os.Build;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceView;

public interface GodotRenderView {
	SurfaceView getView();

	/**
	 * Hint the underlying Android Surface with the target frame rate cap (0 clears the hint).
	 */
	default void setFrameRateHint(int fps) {}

	/**
	 * Applies the requested frame rate hint to the given SurfaceView on Android 11+ (API 30+),
	 * letting SurfaceFlinger seamlessly switch multi-refresh-rate displays (e.g. 90/120 Hz -> 60 Hz)
	 * instead of falling to an uneven vsync divisor.
	 */
	static void applySurfaceFrameRate(SurfaceView surfaceView, int fps, boolean isXrRuntime) {
		if (isXrRuntime || Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
			return;
		}
		Surface surface = surfaceView.getHolder().getSurface();
		if (surface == null || !surface.isValid()) {
			return;
		}
		float frameRate = fps > 0 ? (float)fps : 0.0f;
		int compatibility = fps > 0 ? Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE : Surface.FRAME_RATE_COMPATIBILITY_DEFAULT;
		try {
			if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
				surface.setFrameRate(frameRate, compatibility, Surface.CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS);
			} else {
				surface.setFrameRate(frameRate, compatibility);
			}
		} catch (IllegalArgumentException | IllegalStateException e) {
			Log.w("GodotRenderView", "Unable to apply surface frame rate hint (" + frameRate + " Hz)", e);
		}
	}

	/**
	 * Starts the thread that will drive Godot's rendering.
	 */
	void startRenderer();

	/**
	 * Queues a runnable to be run on the rendering thread.
	 */
	void queueOnRenderThread(Runnable event);

	void onActivityPaused();

	default void onForegroundPresentationPaused() {
		onActivityPaused();
	}

	default void onDeferredApplicationPaused() {}

	void onActivityStopped();

	void onActivityResumed();

	default void onForegroundPresentationResumed() {
		onActivityResumed();
	}

	void onActivityStarted();

	boolean blockingExitRenderer(long blockingTimeInMs);

	GodotInputHandler getInputHandler();

	void configurePointerIcon(int pointerType, String imagePath, float hotSpotX, float hotSpotY);

	void setPointerIcon(int pointerType);

	/**
	 * @return true if pointer capture is supported.
	 */
	boolean canCapturePointer();
}
