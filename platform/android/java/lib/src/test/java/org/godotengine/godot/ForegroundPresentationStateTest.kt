/**************************************************************************/
/*  ForegroundPresentationStateTest.kt                                    */
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

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ForegroundPresentationStateTest {

	@Test
	fun normalLifecycleWithoutPresentationUsesStandardPauseAndResume() {
		val state = ForegroundPresentationState()

		assertFalse(state.suppressesInput)
		assertFalse(state.isPausedVisible)
		assertEquals(ForegroundPresentationState.PauseAction.NORMAL_PAUSE, state.onPause())
		assertFalse(state.onStop())
		assertEquals(ForegroundPresentationState.ResumeAction.NORMAL_RESUME, state.onResume())
		assertFalse(state.suppressesInput)
	}

	@Test
	fun visiblePurchaseEndingBeforeResumeKeepsInputBlockedUntilResume() {
		val state = ForegroundPresentationState()
		val purchaseToken = Any()
		val replacementToken = Any()

		assertFalse(state.begin(null, canBegin = true))
		assertFalse(state.begin(purchaseToken, canBegin = false))
		assertTrue(state.begin(purchaseToken, canBegin = true))
		assertTrue(state.suppressesInput)
		assertFalse(state.begin(replacementToken, canBegin = true))

		assertEquals(ForegroundPresentationState.PauseAction.VISIBLE_PRESENTATION_PAUSE, state.onPause())
		assertTrue(state.isPausedVisible)
		assertTrue(state.suppressesInput)
		assertEquals(ForegroundPresentationState.PauseAction.ALREADY_PAUSED_VISIBLE, state.onPause())

		assertFalse(state.end(replacementToken))
		assertTrue(state.end(purchaseToken))
		assertTrue(state.suppressesInput)
		assertTrue(state.isPausedVisible)
		assertFalse(state.begin(replacementToken, canBegin = true))

		assertEquals(ForegroundPresentationState.ResumeAction.VISIBLE_PRESENTATION_RESUME, state.onResume())
		assertFalse(state.isPausedVisible)
		assertFalse(state.suppressesInput)
		assertFalse(state.onStop())
	}

	@Test
	fun visiblePurchaseResumingBeforeTokenEndsKeepsInputBlockedUntilEnd() {
		val state = ForegroundPresentationState()
		val purchaseToken = Any()

		assertTrue(state.begin(purchaseToken, canBegin = true))
		assertEquals(ForegroundPresentationState.PauseAction.VISIBLE_PRESENTATION_PAUSE, state.onPause())
		assertEquals(ForegroundPresentationState.ResumeAction.VISIBLE_PRESENTATION_RESUME, state.onResume())
		assertFalse(state.isPausedVisible)
		assertTrue(state.suppressesInput)

		assertTrue(state.end(purchaseToken))
		assertFalse(state.suppressesInput)
	}

	@Test
	fun stoppingDuringVisiblePresentationEmitsDeferredPauseAndFailsClosed() {
		val state = ForegroundPresentationState()
		val purchaseToken = Any()

		assertTrue(state.begin(purchaseToken, canBegin = true))
		assertEquals(ForegroundPresentationState.PauseAction.VISIBLE_PRESENTATION_PAUSE, state.onPause())

		assertTrue(state.onStop())
		assertFalse(state.isPausedVisible)
		assertFalse(state.suppressesInput)
		assertFalse(state.onStop())
		assertFalse(state.end(purchaseToken))

		assertEquals(ForegroundPresentationState.ResumeAction.NORMAL_RESUME, state.onResume())
		assertEquals(ForegroundPresentationState.PauseAction.NORMAL_PAUSE, state.onPause())
	}

	@Test
	fun stoppingAfterTokenEndedWhileStillPausedStillEmitsDeferredPause() {
		val state = ForegroundPresentationState()
		val purchaseToken = Any()

		assertTrue(state.begin(purchaseToken, canBegin = true))
		assertEquals(ForegroundPresentationState.PauseAction.VISIBLE_PRESENTATION_PAUSE, state.onPause())
		assertTrue(state.end(purchaseToken))

		assertTrue(state.onStop())
		assertFalse(state.isPausedVisible)
		assertFalse(state.suppressesInput)
		assertEquals(ForegroundPresentationState.ResumeAction.NORMAL_RESUME, state.onResume())
	}

	@Test
	fun synchronousLaunchFailureWithoutPauseRestoresNormalLifecycle() {
		val state = ForegroundPresentationState()
		val purchaseToken = Any()

		assertTrue(state.begin(purchaseToken, canBegin = true))
		assertTrue(state.suppressesInput)
		assertTrue(state.end(purchaseToken))
		assertFalse(state.suppressesInput)
		assertEquals(ForegroundPresentationState.PauseAction.NORMAL_PAUSE, state.onPause())
	}

	@Test
	fun destroyClearsPresentationStateFailClosed() {
		val state = ForegroundPresentationState()
		val purchaseToken = Any()

		assertTrue(state.begin(purchaseToken, canBegin = true))
		assertEquals(ForegroundPresentationState.PauseAction.VISIBLE_PRESENTATION_PAUSE, state.onPause())

		state.onDestroy()
		assertFalse(state.isPausedVisible)
		assertFalse(state.suppressesInput)
		assertFalse(state.onStop())
		assertFalse(state.end(purchaseToken))
	}
}
