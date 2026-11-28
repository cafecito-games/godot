/**************************************************************************/
/*  test_foreground_presentation.cpp                                      */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_foreground_presentation)

#include "drivers/apple_embedded/foreground_presentation.h"

namespace TestForegroundPresentation {

TEST_CASE("[AppleEmbedded] An inactive purchase belongs to one foreground scene and invocation") {
	AppleEmbeddedForegroundPresentation presentation;
	int purchase = 0;
	int replacement = 0;
	int scene = 0;
	int other_scene = 0;

	CHECK_FALSE(presentation.begin(&purchase, nullptr));
	REQUIRE(presentation.begin(&purchase, &scene));
	CHECK_FALSE(presentation.begin(&replacement, &scene));
	presentation.resign_active(&other_scene);
	CHECK_FALSE(presentation.should_iterate(&scene));
	presentation.resign_active(&scene);
	presentation.resign_active(&scene);
	CHECK(presentation.should_iterate(&scene));
	CHECK_FALSE(presentation.should_iterate(&other_scene));

	presentation.end(&replacement);
	CHECK(presentation.should_iterate(&scene));
	presentation.end(&purchase);
	CHECK_FALSE(presentation.should_iterate(&scene));
	CHECK(presentation.suppresses_input());
	CHECK_FALSE(presentation.begin(&replacement, &scene));
	presentation.activate(&other_scene);
	CHECK(presentation.suppresses_input());
	presentation.activate(&scene);
	CHECK_FALSE(presentation.suppresses_input());

	REQUIRE(presentation.begin(&replacement, &scene));
	presentation.resign_active(&scene);
	presentation.end(&purchase);
	CHECK(presentation.should_iterate(&scene));
	presentation.leave_foreground();
	CHECK_FALSE(presentation.should_iterate(&scene));
	CHECK_FALSE(presentation.suppresses_input());
	presentation.activate(&scene);
	presentation.resign_active(&scene);
	CHECK_FALSE(presentation.should_iterate(&scene));
}

} // namespace TestForegroundPresentation
