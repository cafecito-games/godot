/**************************************************************************/
/*  test_gles3_canvas_shadow_cull.cpp                                     */
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

TEST_FORCE_LINK(test_gles3_canvas_shadow_cull)

#ifdef GLES3_ENABLED

#include "core/math/projection.h"
#include "core/math/random_pcg.h"
#include "core/math/rect2.h"
#include "core/math/transform_2d.h"
#include "drivers/gles3/rasterizer_canvas_gles3.h"

namespace TestGLES3CanvasShadowCull {

static bool point_visible_in_quadrant(const Projection &p_projection, const Vector2 &p_point) {
	const Vector4 clip = p_projection.xform(Vector4(p_point.x, p_point.y, 0.0f, 1.0f));
	if (clip.w <= 0.0f) {
		return false;
	}
	return clip.x >= -clip.w && clip.x <= clip.w && clip.z >= -clip.w && clip.z <= clip.w;
}

TEST_CASE("[Rendering][GLES3] 2D shadow occluder quadrant mask basic cases") {
	constexpr float far_distance = 200.0f;

	// Strictly inside each individual quadrant.
	CHECK(RasterizerCanvasGLES3::compute_shadow_occluder_quadrant_mask(Rect2(50.0f, -10.0f, 20.0f, 20.0f), far_distance) == (1u << 0));
	CHECK(RasterizerCanvasGLES3::compute_shadow_occluder_quadrant_mask(Rect2(-10.0f, 50.0f, 20.0f, 20.0f), far_distance) == (1u << 1));
	CHECK(RasterizerCanvasGLES3::compute_shadow_occluder_quadrant_mask(Rect2(-70.0f, -10.0f, 20.0f, 20.0f), far_distance) == (1u << 2));
	CHECK(RasterizerCanvasGLES3::compute_shadow_occluder_quadrant_mask(Rect2(-10.0f, -70.0f, 20.0f, 20.0f), far_distance) == (1u << 3));

	// Straddling diagonal boundary x == y in (+X, +Y) hits quadrants 0 and 1.
	CHECK(RasterizerCanvasGLES3::compute_shadow_occluder_quadrant_mask(Rect2(40.0f, 40.0f, 20.0f, 20.0f), far_distance) == ((1u << 0) | (1u << 1)));

	// Enclosing the origin hits all four quadrants.
	CHECK(RasterizerCanvasGLES3::compute_shadow_occluder_quadrant_mask(Rect2(-5.0f, -5.0f, 10.0f, 10.0f), far_distance) == 0x0Fu);

	// Strictly beyond far_distance is culled from all quadrants.
	CHECK(RasterizerCanvasGLES3::compute_shadow_occluder_quadrant_mask(Rect2(250.0f, -10.0f, 20.0f, 20.0f), far_distance) == 0u);
	CHECK(RasterizerCanvasGLES3::compute_shadow_occluder_quadrant_mask(Rect2(-270.0f, -10.0f, 20.0f, 20.0f), far_distance) == 0u);
	CHECK(RasterizerCanvasGLES3::compute_shadow_occluder_quadrant_mask(Rect2(-10.0f, 250.0f, 20.0f, 20.0f), far_distance) == 0u);
	CHECK(RasterizerCanvasGLES3::compute_shadow_occluder_quadrant_mask(Rect2(-10.0f, -270.0f, 20.0f, 20.0f), far_distance) == 0u);

	// Degenerate zero-height horizontal wall segment straddling +Y far plane.
	CHECK(RasterizerCanvasGLES3::compute_shadow_occluder_quadrant_mask(Rect2(-20.0f, 200.0f, 40.0f, 0.0f), far_distance) == (1u << 1));

	// Non-canonical negative-size Rect2 is handled identically to abs().
	CHECK(RasterizerCanvasGLES3::compute_shadow_occluder_quadrant_mask(Rect2(70.0f, 10.0f, -20.0f, -20.0f), far_distance) == (1u << 0));
}

TEST_CASE("[Rendering][GLES3] 2D shadow occluder quadrant mask is conservative against frustum projections") {
	constexpr float far_distance = 160.0f;
	constexpr float near_distance = far_distance / 1000.0f;

	Projection base_projection;
	const real_t ymax = near_distance * Math::tan(Math::deg_to_rad(45.0));
	base_projection.set_frustum(-ymax, ymax, -ymax, ymax, near_distance, far_distance);

	const Projection quadrant_projections[4] = {
		base_projection * Projection(Vector4(0, 0, -1, 0), Vector4(1, 0, 0, 0), Vector4(0, -1, 0, 0), Vector4(0, 0, 0, 1)),
		base_projection * Projection(Vector4(-1, 0, 0, 0), Vector4(0, 0, -1, 0), Vector4(0, -1, 0, 0), Vector4(0, 0, 0, 1)),
		base_projection * Projection(Vector4(0, 0, 1, 0), Vector4(-1, 0, 0, 0), Vector4(0, -1, 0, 0), Vector4(0, 0, 0, 1)),
		base_projection * Projection(Vector4(1, 0, 0, 0), Vector4(0, 0, 1, 0), Vector4(0, -1, 0, 0), Vector4(0, 0, 0, 1))
	};

	RandomPCG random(0x5A17C0DEu);
	int culled_quadrants = 0;

	for (int sample = 0; sample < 2000; sample++) {
		const Vector2 point_a(random.random(-250.0f, 250.0f), random.random(-250.0f, 250.0f));
		const Vector2 point_b = point_a + Vector2(random.random(-120.0f, 120.0f), random.random(-120.0f, 120.0f));
		Rect2 local_aabb(point_a, Vector2());
		local_aabb.expand_to(point_b);

		Transform2D modelview(
				random.random(-Math::PI, Math::PI),
				Vector2(random.random(-2.0f, 2.0f), random.random(-2.0f, 2.0f)),
				random.random(-0.5f, 0.5f),
				Vector2(random.random(-100.0f, 100.0f), random.random(-100.0f, 100.0f)));

		const Rect2 transformed_aabb = modelview.xform(local_aabb);
		const uint8_t mask = RasterizerCanvasGLES3::compute_shadow_occluder_quadrant_mask(transformed_aabb, far_distance);

		const Vector2 world_a = modelview.xform(point_a);
		const Vector2 world_b = modelview.xform(point_b);

		for (int quadrant = 0; quadrant < 4; quadrant++) {
			const bool mask_includes = (mask & (1u << quadrant)) != 0;
			if (!mask_includes) {
				culled_quadrants++;
				for (int step = 0; step <= 32; step++) {
					const float t = float(step) / 32.0f;
					const Vector2 point = world_a.lerp(world_b, t);
					REQUIRE_FALSE(point_visible_in_quadrant(quadrant_projections[quadrant], point));
				}
			}
		}
	}

	// Ensure culling actually eliminates a substantial fraction of quadrant draws.
	CHECK(culled_quadrants > 4000);
}

} // namespace TestGLES3CanvasShadowCull

#endif // GLES3_ENABLED
