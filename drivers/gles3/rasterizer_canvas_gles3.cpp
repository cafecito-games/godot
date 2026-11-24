/**************************************************************************/
/*  rasterizer_canvas_gles3.cpp                                           */
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

#include "rasterizer_canvas_gles3.h"

#ifdef GLES3_ENABLED

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/math/geometry_2d.h"
#include "core/math/transform_interpolator.h"
#include "drivers/gles3/rasterizer_util_gles3.h"
#include "drivers/gles3/storage/config.h"
#include "drivers/gles3/storage/mesh_storage.h"
#include "drivers/gles3/storage/particles_storage.h"
#include "drivers/gles3/storage/texture_storage.h"
#include "drivers/gles3/storage/utilities.h"
#include "servers/rendering/rendering_server_default.h"
#include "servers/rendering/rendering_server_globals.h"
#include "servers/rendering/rendering_server_types.h"

void RasterizerCanvasGLES3::_update_transform_2d_to_mat4(const Transform2D &p_transform, float *p_mat4) {
	p_mat4[0] = p_transform.columns[0][0];
	p_mat4[1] = p_transform.columns[0][1];
	p_mat4[2] = 0;
	p_mat4[3] = 0;
	p_mat4[4] = p_transform.columns[1][0];
	p_mat4[5] = p_transform.columns[1][1];
	p_mat4[6] = 0;
	p_mat4[7] = 0;
	p_mat4[8] = 0;
	p_mat4[9] = 0;
	p_mat4[10] = 1;
	p_mat4[11] = 0;
	p_mat4[12] = p_transform.columns[2][0];
	p_mat4[13] = p_transform.columns[2][1];
	p_mat4[14] = 0;
	p_mat4[15] = 1;
}

void RasterizerCanvasGLES3::_update_transform_2d_to_mat2x4(const Transform2D &p_transform, float *p_mat2x4) {
	p_mat2x4[0] = p_transform.columns[0][0];
	p_mat2x4[1] = p_transform.columns[1][0];
	p_mat2x4[2] = 0;
	p_mat2x4[3] = p_transform.columns[2][0];

	p_mat2x4[4] = p_transform.columns[0][1];
	p_mat2x4[5] = p_transform.columns[1][1];
	p_mat2x4[6] = 0;
	p_mat2x4[7] = p_transform.columns[2][1];
}

void RasterizerCanvasGLES3::_update_transform_2d_to_mat2x3(const Transform2D &p_transform, float *p_mat2x3) {
	p_mat2x3[0] = p_transform.columns[0][0];
	p_mat2x3[1] = p_transform.columns[0][1];
	p_mat2x3[2] = p_transform.columns[1][0];
	p_mat2x3[3] = p_transform.columns[1][1];
	p_mat2x3[4] = p_transform.columns[2][0];
	p_mat2x3[5] = p_transform.columns[2][1];
}

void RasterizerCanvasGLES3::_update_transform_to_mat4(const Transform3D &p_transform, float *p_mat4) {
	p_mat4[0] = p_transform.basis.rows[0][0];
	p_mat4[1] = p_transform.basis.rows[1][0];
	p_mat4[2] = p_transform.basis.rows[2][0];
	p_mat4[3] = 0;
	p_mat4[4] = p_transform.basis.rows[0][1];
	p_mat4[5] = p_transform.basis.rows[1][1];
	p_mat4[6] = p_transform.basis.rows[2][1];
	p_mat4[7] = 0;
	p_mat4[8] = p_transform.basis.rows[0][2];
	p_mat4[9] = p_transform.basis.rows[1][2];
	p_mat4[10] = p_transform.basis.rows[2][2];
	p_mat4[11] = 0;
	p_mat4[12] = p_transform.origin.x;
	p_mat4[13] = p_transform.origin.y;
	p_mat4[14] = p_transform.origin.z;
	p_mat4[15] = 1;
}

void RasterizerCanvasGLES3::canvas_render_items(RID p_to_render_target, Item *p_item_list, const Color &p_modulate, Light *p_light_list, Light *p_directional_light_list, const Transform2D &p_canvas_transform, RSE::CanvasItemTextureFilter p_default_filter, RSE::CanvasItemTextureRepeat p_default_repeat, bool p_snap_2d_vertices_to_pixel, bool &r_sdf_used, RenderingServerTypes::RenderInfo *r_render_info) {
	GLES3::TextureStorage *texture_storage = GLES3::TextureStorage::get_singleton();
	GLES3::MaterialStorage *material_storage = GLES3::MaterialStorage::get_singleton();
	GLES3::MeshStorage *mesh_storage = GLES3::MeshStorage::get_singleton();

	Transform2D canvas_transform_inverse = p_canvas_transform.affine_inverse();

	// Clear out any state that may have been left from the 3D pass.
	reset_canvas();

	_acquire_instance_data_buffer();

	//setup directional lights if exist

	uint32_t light_count = 0;
	uint32_t directional_light_count = 0;
	{
		Light *l = p_directional_light_list;
		uint32_t index = 0;

		while (l) {
			if (index == data.max_lights_per_render) {
				l->render_index_cache = -1;
				l = l->next_ptr;
				continue;
			}

			CanvasLight *clight = canvas_light_owner.get_or_null(l->light_internal);
			if (!clight) { //unused or invalid texture
				l->render_index_cache = -1;
				l = l->next_ptr;
				ERR_CONTINUE(!clight);
			}

			Vector2 canvas_light_dir = l->xform_cache.columns[1].normalized();

			state.light_uniforms[index].position[0] = -canvas_light_dir.x;
			state.light_uniforms[index].position[1] = -canvas_light_dir.y;

			_update_transform_2d_to_mat2x4(clight->shadow.directional_xform, state.light_uniforms[index].shadow_matrix);

			state.light_uniforms[index].height = l->height; //0..1 here

			for (int i = 0; i < 4; i++) {
				state.light_uniforms[index].shadow_color[i] = uint8_t(CLAMP(int32_t(l->shadow_color[i] * 255.0), 0, 255));
				state.light_uniforms[index].color[i] = l->color[i];
			}

			state.light_uniforms[index].color[3] *= l->energy; //use alpha for energy, so base color can go separate

			if (state.shadow_fb != 0) {
				state.light_uniforms[index].shadow_pixel_size = (1.0 / state.shadow_texture_size) * (1.0 + l->shadow_smooth);
				state.light_uniforms[index].shadow_z_far_inv = 1.0 / clight->shadow.z_far;
				state.light_uniforms[index].shadow_y_ofs = clight->shadow.y_offset;
			} else {
				state.light_uniforms[index].shadow_pixel_size = 1.0;
				state.light_uniforms[index].shadow_z_far_inv = 1.0;
				state.light_uniforms[index].shadow_y_ofs = 0;
			}

			state.light_uniforms[index].flags = l->blend_mode << LIGHT_FLAGS_BLEND_SHIFT;
			state.light_uniforms[index].flags |= l->shadow_filter << LIGHT_FLAGS_FILTER_SHIFT;

			// A light without a shadow band for this frame must not sample the atlas.
			if (clight->shadow.enabled && l->shadow_index_cache >= 0) {
				state.light_uniforms[index].flags |= LIGHT_FLAGS_HAS_SHADOW;
			}

			l->render_index_cache = index;

			index++;
			l = l->next_ptr;
		}

		light_count = index;
		directional_light_count = light_count;
		state.using_directional_lights = directional_light_count > 0;
	}

	//setup lights if exist

	state.active_point_lights.clear();
	{
		Light *l = p_light_list;
		uint32_t index = light_count;

		while (l) {
			if (index == data.max_lights_per_render) {
				l->render_index_cache = -1;
				l = l->next_ptr;
				continue;
			}

			CanvasLight *clight = canvas_light_owner.get_or_null(l->light_internal);
			if (!clight) { //unused or invalid texture
				l->render_index_cache = -1;
				l = l->next_ptr;
				ERR_CONTINUE(!clight);
			}

			Transform2D final_xform;
			if (!RSG::canvas->_interpolation_data.interpolation_enabled || !l->interpolated) {
				final_xform = l->xform_curr;
			} else {
				real_t f = Engine::get_singleton()->get_physics_interpolation_fraction();
				TransformInterpolator::interpolate_transform_2d(l->xform_prev, l->xform_curr, final_xform, f);
			}
			// Convert light position to canvas coordinates, as all computation is done in canvas coordinates to avoid precision loss.
			Vector2 canvas_light_pos = p_canvas_transform.xform(final_xform.get_origin());
			state.light_uniforms[index].position[0] = canvas_light_pos.x;
			state.light_uniforms[index].position[1] = canvas_light_pos.y;

			_update_transform_2d_to_mat2x4(l->light_shader_xform.affine_inverse(), state.light_uniforms[index].matrix);
			_update_transform_2d_to_mat2x4(l->xform_cache.affine_inverse(), state.light_uniforms[index].shadow_matrix);

			state.light_uniforms[index].height = l->height * (p_canvas_transform.columns[0].length() + p_canvas_transform.columns[1].length()) * 0.5; //approximate height conversion to the canvas size, since all calculations are done in canvas coords to avoid precision loss
			for (int i = 0; i < 4; i++) {
				state.light_uniforms[index].shadow_color[i] = uint8_t(CLAMP(int32_t(l->shadow_color[i] * 255.0), 0, 255));
				state.light_uniforms[index].color[i] = l->color[i];
			}

			state.light_uniforms[index].color[3] *= l->energy; //use alpha for energy, so base color can go separate

			if (state.shadow_fb != 0) {
				state.light_uniforms[index].shadow_pixel_size = (1.0 / state.shadow_texture_size) * (1.0 + l->shadow_smooth);
				state.light_uniforms[index].shadow_z_far_inv = 1.0 / clight->shadow.z_far;
				state.light_uniforms[index].shadow_y_ofs = clight->shadow.y_offset;
			} else {
				state.light_uniforms[index].shadow_pixel_size = 1.0;
				state.light_uniforms[index].shadow_z_far_inv = 1.0;
				state.light_uniforms[index].shadow_y_ofs = 0;
			}

			state.light_uniforms[index].flags = l->blend_mode << LIGHT_FLAGS_BLEND_SHIFT;
			state.light_uniforms[index].flags |= l->shadow_filter << LIGHT_FLAGS_FILTER_SHIFT;

			// A light without a shadow band for this frame must not sample the atlas.
			if (clight->shadow.enabled && l->shadow_index_cache >= 0) {
				state.light_uniforms[index].flags |= LIGHT_FLAGS_HAS_SHADOW;
			}

			if (clight->texture.is_valid()) {
				Rect2 atlas_rect = GLES3::TextureStorage::get_singleton()->texture_atlas_get_texture_rect(clight->texture);
				state.light_uniforms[index].atlas_rect[0] = atlas_rect.position.x;
				state.light_uniforms[index].atlas_rect[1] = atlas_rect.position.y;
				state.light_uniforms[index].atlas_rect[2] = atlas_rect.size.width;
				state.light_uniforms[index].atlas_rect[3] = atlas_rect.size.height;

			} else {
				state.light_uniforms[index].atlas_rect[0] = 0;
				state.light_uniforms[index].atlas_rect[1] = 0;
				state.light_uniforms[index].atlas_rect[2] = 0;
				state.light_uniforms[index].atlas_rect[3] = 0;
			}

			l->render_index_cache = index;

			ActivePointLight active_light;
			active_light.rect_cache = l->rect_cache;
			active_light.item_mask = l->item_mask;
			active_light.item_shadow_mask = l->item_shadow_mask;
			active_light.z_min = l->z_min;
			active_light.z_max = l->z_max;
			active_light.render_index = index;
			state.active_point_lights.push_back(active_light);

			index++;
			l = l->next_ptr;
		}

		light_count = index;
	}

	if (light_count > 0) {
		glBindBufferBase(GL_UNIFORM_BUFFER, LIGHT_UNIFORM_LOCATION, state.canvas_instance_data_buffers[state.current_data_buffer_index].light_ubo);

#ifdef WEB_ENABLED
		glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(LightUniform) * light_count, state.light_uniforms);
#else
		// On Desktop and mobile we map the memory without synchronizing for maximum speed.
		void *ubo = glMapBufferRange(GL_UNIFORM_BUFFER, 0, sizeof(LightUniform) * light_count, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
		memcpy(ubo, state.light_uniforms, sizeof(LightUniform) * light_count);
		glUnmapBuffer(GL_UNIFORM_BUFFER);
#endif

		GLuint texture_atlas = texture_storage->texture_atlas_get_texture();
		if (texture_atlas == 0) {
			GLES3::Texture *tex = texture_storage->get_texture(texture_storage->texture_gl_get_default(GLES3::DEFAULT_GL_TEXTURE_WHITE));
			texture_atlas = tex->tex_id;
		}
		glActiveTexture(GL_TEXTURE0 + GLES3::Config::get_singleton()->max_texture_image_units - 2);
		glBindTexture(GL_TEXTURE_2D, texture_atlas);
		GLuint shadow_tex = state.shadow_texture;
		if (shadow_tex == 0) {
			GLES3::Texture *tex = texture_storage->get_texture(texture_storage->texture_gl_get_default(GLES3::DEFAULT_GL_TEXTURE_WHITE));
			shadow_tex = tex->tex_id;
		}
		glActiveTexture(GL_TEXTURE0 + GLES3::Config::get_singleton()->max_texture_image_units - 3);
		glBindTexture(GL_TEXTURE_2D, shadow_tex);
	}

	{
		//update canvas state uniform buffer
		StateBuffer state_buffer;

		Size2i ssize = texture_storage->render_target_get_size(p_to_render_target);

		// If we've overridden the render target's color texture, then we need
		// to invert the Y axis, so 2D texture appear right side up.
		// We're probably rendering directly to an XR device.
		float y_scale = texture_storage->render_target_get_override_color(p_to_render_target).is_valid() ? -2.0f : 2.0f;

		Transform3D screen_transform;
		screen_transform.translate_local(-(ssize.width / 2.0f), -(ssize.height / 2.0f), 0.0f);
		screen_transform.scale(Vector3(2.0f / ssize.width, y_scale / ssize.height, 1.0f));
		_update_transform_to_mat4(screen_transform, state_buffer.screen_transform);
		_update_transform_2d_to_mat4(p_canvas_transform, state_buffer.canvas_transform);

		Transform2D normal_transform = p_canvas_transform;
		normal_transform.columns[0].normalize();
		normal_transform.columns[1].normalize();
		normal_transform.columns[2] = Vector2();
		_update_transform_2d_to_mat4(normal_transform, state_buffer.canvas_normal_transform);

		state_buffer.canvas_modulate[0] = p_modulate.r;
		state_buffer.canvas_modulate[1] = p_modulate.g;
		state_buffer.canvas_modulate[2] = p_modulate.b;
		state_buffer.canvas_modulate[3] = p_modulate.a;

		Size2 render_target_size = texture_storage->render_target_get_size(p_to_render_target);
		state_buffer.screen_pixel_size[0] = 1.0 / render_target_size.x;
		state_buffer.screen_pixel_size[1] = 1.0 / render_target_size.y;

		state_buffer.time = state.time;
		state_buffer.use_pixel_snap = p_snap_2d_vertices_to_pixel;

		state_buffer.directional_light_count = directional_light_count;

		Vector2 canvas_scale = p_canvas_transform.get_scale();

		state_buffer.sdf_to_screen[0] = render_target_size.width / canvas_scale.x;
		state_buffer.sdf_to_screen[1] = render_target_size.height / canvas_scale.y;

		state_buffer.screen_to_sdf[0] = 1.0 / state_buffer.sdf_to_screen[0];
		state_buffer.screen_to_sdf[1] = 1.0 / state_buffer.sdf_to_screen[1];

		Rect2 sdf_rect = texture_storage->render_target_get_sdf_rect(p_to_render_target);
		Rect2 sdf_tex_rect(sdf_rect.position / canvas_scale, sdf_rect.size / canvas_scale);

		state_buffer.sdf_to_tex[0] = 1.0 / sdf_tex_rect.size.width;
		state_buffer.sdf_to_tex[1] = 1.0 / sdf_tex_rect.size.height;
		state_buffer.sdf_to_tex[2] = -sdf_tex_rect.position.x / sdf_tex_rect.size.width;
		state_buffer.sdf_to_tex[3] = -sdf_tex_rect.position.y / sdf_tex_rect.size.height;

		state_buffer.tex_to_sdf = 1.0 / ((canvas_scale.x + canvas_scale.y) * 0.5);

		glBindBufferBase(GL_UNIFORM_BUFFER, BASE_UNIFORM_LOCATION, state.canvas_instance_data_buffers[state.current_data_buffer_index].state_ubo);
#ifdef WEB_ENABLED
		glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(StateBuffer), &state_buffer);
#else
		// On desktop and mobile, state_ubo is fence-protected per DataBuffer so unsynchronized mapping avoids driver buffer orphaning.
		void *state_ubo_data = glMapBufferRange(GL_UNIFORM_BUFFER, 0, sizeof(StateBuffer), GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
		if (state_ubo_data != nullptr) {
			memcpy(state_ubo_data, &state_buffer, sizeof(StateBuffer));
			glUnmapBuffer(GL_UNIFORM_BUFFER);
		} else {
			glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(StateBuffer), &state_buffer);
		}
#endif

		GLuint global_buffer = material_storage->global_shader_parameters_get_uniform_buffer();

		glBindBufferBase(GL_UNIFORM_BUFFER, GLOBAL_UNIFORM_LOCATION, global_buffer);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);
	}

	glActiveTexture(GL_TEXTURE0 + GLES3::Config::get_singleton()->max_texture_image_units - 5);
	glBindTexture(GL_TEXTURE_2D, texture_storage->render_target_get_sdf_texture(p_to_render_target));

	{
		state.default_filter = p_default_filter;
		state.default_repeat = p_default_repeat;
	}

	Size2 render_target_size = texture_storage->render_target_get_size(p_to_render_target);
	glViewport(0, 0, render_target_size.x, render_target_size.y);

	r_sdf_used = false;
	int item_count = 0;
	bool backbuffer_cleared = false;
	bool time_used = false;
	bool material_screen_texture_cached = false;
	bool material_screen_texture_mipmaps_cached = false;
	Rect2 back_buffer_rect;
	bool backbuffer_copy = false;
	bool backbuffer_gen_mipmaps = false;
	bool update_skeletons = false;

	Item *ci = p_item_list;
	Item *canvas_group_owner = nullptr;
	bool skip_item = false;

	state.last_item_index = 0;

	while (ci) {
		if (ci->copy_back_buffer && canvas_group_owner == nullptr) {
			backbuffer_copy = true;

			if (ci->copy_back_buffer->full) {
				back_buffer_rect = Rect2();
			} else {
				back_buffer_rect = ci->copy_back_buffer->rect;
			}
		}

		// Check material for something that may change flow of rendering, but do not bind for now.
		RID material = ci->material_owner == nullptr ? ci->material : ci->material_owner->material;
		if (material.is_valid()) {
			GLES3::CanvasMaterialData *md = static_cast<GLES3::CanvasMaterialData *>(material_storage->material_get_data(material, RSE::SHADER_CANVAS_ITEM));
			if (md && md->shader_data->valid) {
				if (md->shader_data->uses_screen_texture && canvas_group_owner == nullptr) {
					if (!material_screen_texture_cached) {
						backbuffer_copy = true;
						back_buffer_rect = Rect2();
						backbuffer_gen_mipmaps = md->shader_data->uses_screen_texture_mipmaps;
					} else if (!material_screen_texture_mipmaps_cached) {
						backbuffer_gen_mipmaps = md->shader_data->uses_screen_texture_mipmaps;
					}
				}

				if (md->shader_data->uses_sdf) {
					r_sdf_used = true;
				}
				if (md->shader_data->uses_time) {
					time_used = true;
				}
			}
		}

		if (ci->skeleton.is_valid()) {
			const Item::Command *c = ci->commands;

			while (c) {
				if (c->type == Item::Command::TYPE_MESH) {
					const Item::CommandMesh *cm = static_cast<const Item::CommandMesh *>(c);
					if (cm->mesh_instance.is_valid()) {
						mesh_storage->mesh_instance_check_for_update(cm->mesh_instance);
						mesh_storage->mesh_instance_set_canvas_item_transform(cm->mesh_instance, canvas_transform_inverse * ci->final_transform);
						update_skeletons = true;
					}
				}
				c = c->next;
			}
		}

		if (ci->canvas_group_owner != nullptr) {
			if (canvas_group_owner == nullptr) {
				if (update_skeletons) {
					mesh_storage->update_mesh_instances();
					update_skeletons = false;
				}
				// Canvas group begins here, render until before this item
				_render_items(p_to_render_target, item_count, canvas_transform_inverse, p_light_list, r_sdf_used, false, r_render_info, material_screen_texture_mipmaps_cached);
				item_count = 0;

				if (ci->canvas_group_owner->canvas_group->mode != RSE::CANVAS_GROUP_MODE_TRANSPARENT) {
					Rect2i group_rect = ci->canvas_group_owner->global_rect_cache;
					texture_storage->render_target_copy_to_back_buffer(p_to_render_target, group_rect, false);
					if (ci->canvas_group_owner->canvas_group->mode == RSE::CANVAS_GROUP_MODE_CLIP_AND_DRAW) {
						ci->canvas_group_owner->use_canvas_group = false;
						items[item_count++] = ci->canvas_group_owner;
					}
				} else if (!backbuffer_cleared) {
					texture_storage->render_target_clear_back_buffer(p_to_render_target, Rect2i(), Color(0, 0, 0, 0));
					backbuffer_cleared = true;
				}

				backbuffer_copy = false;
				canvas_group_owner = ci->canvas_group_owner; //continue until owner found
			}

			ci->canvas_group_owner = nullptr; //must be cleared
		}

		if (canvas_group_owner == nullptr && ci->canvas_group != nullptr && ci->canvas_group->mode != RSE::CANVAS_GROUP_MODE_CLIP_AND_DRAW) {
			skip_item = true;
		}

		if (ci == canvas_group_owner) {
			if (update_skeletons) {
				mesh_storage->update_mesh_instances();
				update_skeletons = false;
			}
			_render_items(p_to_render_target, item_count, canvas_transform_inverse, p_light_list, r_sdf_used, true, r_render_info, material_screen_texture_mipmaps_cached);
			item_count = 0;

			if (ci->canvas_group->blur_mipmaps) {
				texture_storage->render_target_gen_back_buffer_mipmaps(p_to_render_target, ci->global_rect_cache);
			}

			canvas_group_owner = nullptr;
			// Backbuffer is dirty now and needs to be re-cleared if another CanvasGroup needs it.
			backbuffer_cleared = false;

			// Tell the renderer to paint this as a canvas group
			ci->use_canvas_group = true;
		} else {
			ci->use_canvas_group = false;
		}

		if (backbuffer_copy) {
			if (update_skeletons) {
				mesh_storage->update_mesh_instances();
				update_skeletons = false;
			}
			//render anything pending, including clearing if no items

			_render_items(p_to_render_target, item_count, canvas_transform_inverse, p_light_list, r_sdf_used, false, r_render_info, material_screen_texture_mipmaps_cached);
			item_count = 0;

			texture_storage->render_target_copy_to_back_buffer(p_to_render_target, back_buffer_rect, backbuffer_gen_mipmaps);

			backbuffer_copy = false;
			material_screen_texture_cached = true; // After a backbuffer copy, screen texture makes no further copies.
			material_screen_texture_mipmaps_cached = backbuffer_gen_mipmaps;
			backbuffer_gen_mipmaps = false;
		}

		if (backbuffer_gen_mipmaps) {
			texture_storage->render_target_gen_back_buffer_mipmaps(p_to_render_target, back_buffer_rect);

			backbuffer_gen_mipmaps = false;
			material_screen_texture_mipmaps_cached = true;
		}

		// just add all items for now
		if (skip_item) {
			skip_item = false;
		} else {
			items[item_count++] = ci;
		}

		if (!ci->next || item_count == MAX_RENDER_ITEMS - 1) {
			if (update_skeletons) {
				mesh_storage->update_mesh_instances();
				update_skeletons = false;
			}
			_render_items(p_to_render_target, item_count, canvas_transform_inverse, p_light_list, r_sdf_used, canvas_group_owner != nullptr, r_render_info, material_screen_texture_mipmaps_cached);
			//then reset
			item_count = 0;
		}

		ci = ci->next;
	}

	if (time_used) {
		RenderingServerDefault::redraw_request();
	}

	// Clear out state used in 2D pass
	reset_canvas();
	state.current_data_buffer_index = (state.current_data_buffer_index + 1) % state.canvas_instance_data_buffers.size();
	state.current_instance_buffer_index = 0;
}

void RasterizerCanvasGLES3::_render_items(RID p_to_render_target, int p_item_count, const Transform2D &p_canvas_transform_inverse, Light *p_lights, bool &r_sdf_used, bool p_to_backbuffer, RenderingServerTypes::RenderInfo *r_render_info, bool p_backbuffer_has_mipmaps) {
	GLES3::MaterialStorage *material_storage = GLES3::MaterialStorage::get_singleton();

	canvas_begin(p_to_render_target, p_to_backbuffer, p_backbuffer_has_mipmaps);

	if (p_item_count <= 0) {
		// Nothing to draw, just call canvas_begin() to clear the render target and return.
		return;
	}

	uint32_t index = 0;
	Item *current_clip = nullptr;
	GLES3::CanvasShaderData *shader_data_cache = nullptr;

	// Record Batches.
	// First item always forms its own batch.
	bool batch_broken = false;
	_new_batch(batch_broken);

	// Override the start position and index as we want to start from where we finished off last time.
	state.canvas_instance_batches[state.current_batch_index].start = state.last_item_index;
	index = 0;

	for (int i = 0; i < p_item_count; i++) {
		Item *ci = items[i];

		if (ci->final_clip_owner != state.canvas_instance_batches[state.current_batch_index].clip) {
			_new_batch(batch_broken);
			state.canvas_instance_batches[state.current_batch_index].clip = ci->final_clip_owner;
			current_clip = ci->final_clip_owner;
		}

		RID material = ci->material_owner == nullptr ? ci->material : ci->material_owner->material;
		if (ci->use_canvas_group) {
			if (ci->canvas_group->mode == RSE::CANVAS_GROUP_MODE_CLIP_AND_DRAW) {
				material = default_clip_children_material;
			} else {
				if (material.is_null()) {
					if (ci->canvas_group->mode == RSE::CANVAS_GROUP_MODE_CLIP_ONLY) {
						material = default_clip_children_material;
					} else {
						material = default_canvas_group_material;
					}
				}
			}
		}

		if (material != state.canvas_instance_batches[state.current_batch_index].material) {
			_new_batch(batch_broken);

			GLES3::CanvasMaterialData *material_data = nullptr;
			if (material.is_valid()) {
				material_data = static_cast<GLES3::CanvasMaterialData *>(material_storage->material_get_data(material, RSE::SHADER_CANVAS_ITEM));
			}
			shader_data_cache = nullptr;
			if (material_data) {
				if (material_data->shader_data->version.is_valid() && material_data->shader_data->valid) {
					shader_data_cache = material_data->shader_data;
				}
			}

			state.canvas_instance_batches[state.current_batch_index].material = material;
			state.canvas_instance_batches[state.current_batch_index].material_data = material_data;
			if (shader_data_cache) {
				state.canvas_instance_batches[state.current_batch_index].vertex_input_mask = shader_data_cache->vertex_input_mask;
			}
		}

		GLES3::CanvasShaderData::BlendMode blend_mode = shader_data_cache ? shader_data_cache->blend_mode : GLES3::CanvasShaderData::BLEND_MODE_MIX;

		if (!ci->repeat_size.x && !ci->repeat_size.y) {
			_record_item_commands(ci, p_to_render_target, p_canvas_transform_inverse, current_clip, blend_mode, p_lights, index, batch_broken, r_sdf_used, Point2());
		} else {
			Point2 start_pos = ci->repeat_size * -(ci->repeat_times / 2);
			Point2 offset;

			int repeat_times_x = ci->repeat_size.x ? ci->repeat_times : 0;
			int repeat_times_y = ci->repeat_size.y ? ci->repeat_times : 0;
			for (int ry = 0; ry <= repeat_times_y; ry++) {
				offset.y = start_pos.y + ry * ci->repeat_size.y;
				for (int rx = 0; rx <= repeat_times_x; rx++) {
					offset.x = start_pos.x + rx * ci->repeat_size.x;
					_record_item_commands(ci, p_to_render_target, p_canvas_transform_inverse, current_clip, blend_mode, p_lights, index, batch_broken, r_sdf_used, offset);
				}
			}
		}
	}

	if (state.current_batch_index == 0 && state.canvas_instance_batches[0].instance_count == 0) {
		// Nothing to render, just return.
		return;
	}

	// Copy over all data needed for rendering.
	glBindBuffer(GL_ARRAY_BUFFER, state.canvas_instance_data_buffers[state.current_data_buffer_index].instance_buffers[state.current_instance_buffer_index]);
#ifdef WEB_ENABLED
	glBufferSubData(GL_ARRAY_BUFFER, state.last_item_index * sizeof(InstanceData), sizeof(InstanceData) * index, state.instance_data_array);
#else
	// On Desktop and mobile we map the memory without synchronizing for maximum speed.
	void *buffer = glMapBufferRange(GL_ARRAY_BUFFER, state.last_item_index * sizeof(InstanceData), index * sizeof(InstanceData), GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
	memcpy(buffer, state.instance_data_array, index * sizeof(InstanceData));
	glUnmapBuffer(GL_ARRAY_BUFFER);
#endif

	glDisable(GL_SCISSOR_TEST);
	current_clip = nullptr;

	GLES3::CanvasShaderData::BlendMode last_blend_mode = GLES3::CanvasShaderData::BLEND_MODE_MIX;
	Color last_blend_color;

	state.current_tex = RID();
	state.current_normal_texture_id = 0;
	state.current_specular_texture_id = 0;

	const GLES3::Config *config = GLES3::Config::get_singleton();
	const uint64_t base_specialization = config->float_texture_supported ? 0 : CanvasShaderGLES3::USE_RGBA_SHADOWS;
	const int max_safe_material_textures = config->max_texture_image_units - 7;
	GLuint current_vao = 0;
	GLuint current_array_buffer = 0;
	GLES3::CanvasMaterialData *current_material_data = nullptr;
	RID current_shader_version;
	uint64_t current_specialization = 0;
	bool has_bound_shader = false;
	bool current_shader_valid = false;
	uint32_t current_batch_flags = 0;
	uint32_t current_specular_shininess = 0;
	bool batch_uniforms_bound = false;

	for (uint32_t i = 0; i <= state.current_batch_index; i++) {
		// Skipping when there is no instances.
		if (state.canvas_instance_batches[i].instance_count == 0) {
			continue;
		}

		//setup clip
		if (current_clip != state.canvas_instance_batches[i].clip) {
			current_clip = state.canvas_instance_batches[i].clip;
			if (current_clip) {
				glEnable(GL_SCISSOR_TEST);
				glScissor(current_clip->final_clip_rect.position.x, current_clip->final_clip_rect.position.y, current_clip->final_clip_rect.size.x, current_clip->final_clip_rect.size.y);
			} else {
				glDisable(GL_SCISSOR_TEST);
			}
		}

		GLES3::CanvasMaterialData *material_data = state.canvas_instance_batches[i].material_data;
		CanvasShaderGLES3::ShaderVariant variant = CanvasShaderGLES3::MODE_DEFAULT;
		uint64_t specialization = state.canvas_instance_batches[i].specialization;
		specialization |= base_specialization;
		RID shader_version = data.canvas_shader_default_version;

		if (material_data) {
			if (material_data->shader_data->version.is_valid() && material_data->shader_data->valid) {
				// Bind uniform buffer and textures. Skip re-binding when the same UBO-only material
				// stays active; materials with texture uniforms re-bind so shared textures keep their
				// declared filter/repeat state.
				const int texture_cache_size = material_data->get_texture_cache_size();
				if (material_data != current_material_data || texture_cache_size > 0) {
					material_data->bind_uniforms();
					if (texture_cache_size >= max_safe_material_textures) {
						state.current_normal_texture_id = 0;
						state.current_specular_texture_id = 0;
					}
				}
				shader_version = material_data->shader_data->version;
			}
		}
		current_material_data = material_data;

		if (!has_bound_shader || shader_version != current_shader_version || specialization != current_specialization) {
			current_shader_valid = material_storage->shaders.canvas_shader.version_bind_shader(shader_version, variant, specialization);
			has_bound_shader = true;
			current_shader_version = shader_version;
			current_specialization = specialization;
			batch_uniforms_bound = false;
		}
		if (!current_shader_valid) {
			continue;
		}

		// Bind per-batch uniforms only when changed on the active program.
		const uint32_t batch_flags = state.canvas_instance_batches[i].flags;
		const uint32_t batch_specular_shininess = state.canvas_instance_batches[i].specular_shininess;
		if (!batch_uniforms_bound || batch_flags != current_batch_flags) {
			material_storage->shaders.canvas_shader.version_set_uniform(CanvasShaderGLES3::BATCH_FLAGS, batch_flags, shader_version, variant, specialization);
			current_batch_flags = batch_flags;
		}
		if (!batch_uniforms_bound || batch_specular_shininess != current_specular_shininess) {
			material_storage->shaders.canvas_shader.version_set_uniform(CanvasShaderGLES3::SPECULAR_SHININESS_IN, batch_specular_shininess, shader_version, variant, specialization);
			current_specular_shininess = batch_specular_shininess;
		}
		batch_uniforms_bound = true;

		GLES3::CanvasShaderData::BlendMode blend_mode = state.canvas_instance_batches[i].blend_mode;
		Color blend_color = state.canvas_instance_batches[i].blend_color;

		if (last_blend_mode != blend_mode || (blend_mode == GLES3::CanvasShaderData::BLEND_MODE_LCD && last_blend_color != blend_color)) {
			if (last_blend_mode == GLES3::CanvasShaderData::BLEND_MODE_DISABLED) {
				// re-enable it
				glEnable(GL_BLEND);
			} else if (blend_mode == GLES3::CanvasShaderData::BLEND_MODE_DISABLED) {
				// disable it
				glDisable(GL_BLEND);
			}

			switch (blend_mode) {
				case GLES3::CanvasShaderData::BLEND_MODE_DISABLED: {
					// Nothing to do here.
				} break;
				case GLES3::CanvasShaderData::BLEND_MODE_LCD: {
					glBlendEquation(GL_FUNC_ADD);
					if (state.transparent_render_target) {
						glBlendFuncSeparate(GL_CONSTANT_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
					} else {
						glBlendFuncSeparate(GL_CONSTANT_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
					}
					glBlendColor(blend_color.r, blend_color.g, blend_color.b, blend_color.a);

				} break;
				case GLES3::CanvasShaderData::BLEND_MODE_MIX: {
					glBlendEquation(GL_FUNC_ADD);
					if (state.transparent_render_target) {
						glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
					} else {
						glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
					}

				} break;
				case GLES3::CanvasShaderData::BLEND_MODE_ADD: {
					glBlendEquation(GL_FUNC_ADD);
					if (state.transparent_render_target) {
						glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_SRC_ALPHA, GL_ONE);
					} else {
						glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
					}

				} break;
				case GLES3::CanvasShaderData::BLEND_MODE_SUB: {
					glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
					if (state.transparent_render_target) {
						glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_SRC_ALPHA, GL_ONE);
					} else {
						glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
					}
				} break;
				case GLES3::CanvasShaderData::BLEND_MODE_MUL: {
					glBlendEquation(GL_FUNC_ADD);
					if (state.transparent_render_target) {
						glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_DST_ALPHA, GL_ZERO);
					} else {
						glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
					}

				} break;
				case GLES3::CanvasShaderData::BLEND_MODE_PMALPHA: {
					glBlendEquation(GL_FUNC_ADD);
					if (state.transparent_render_target) {
						glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
					} else {
						glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
					}

				} break;
			}
			last_blend_mode = blend_mode;
			last_blend_color = blend_color;
		}

		_render_batch(p_lights, i, current_vao, current_array_buffer, r_render_info);
	}

	if (current_vao != 0) {
		glBindVertexArray(0);
	}
	glDisable(GL_SCISSOR_TEST);
	state.current_batch_index = 0;
	state.canvas_instance_batches.clear();
	state.last_item_index += index;
}

void RasterizerCanvasGLES3::_record_item_commands(const Item *p_item, RID p_render_target, const Transform2D &p_canvas_transform_inverse, Item *&current_clip, GLES3::CanvasShaderData::BlendMode p_blend_mode, Light *p_lights, uint32_t &r_index, bool &r_batch_broken, bool &r_sdf_used, const Point2 &p_repeat_offset) {
	RSE::CanvasItemTextureFilter texture_filter = p_item->texture_filter == RSE::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT ? state.default_filter : p_item->texture_filter;
	const uint64_t specialization_command_mask = ~(CanvasShaderGLES3::USE_NINEPATCH | CanvasShaderGLES3::USE_PRIMITIVE | CanvasShaderGLES3::USE_ATTRIBUTES | CanvasShaderGLES3::USE_INSTANCING);

	if (texture_filter != state.canvas_instance_batches[state.current_batch_index].filter) {
		_new_batch(r_batch_broken);

		state.canvas_instance_batches[state.current_batch_index].filter = texture_filter;
	}

	Transform2D base_transform = p_item->final_transform;
	if (p_item->repeat_source_item && (p_repeat_offset.x || p_repeat_offset.y)) {
		base_transform.columns[2] += p_item->repeat_source_item->final_transform.basis_xform(p_repeat_offset);
	}
	base_transform = p_canvas_transform_inverse * base_transform;

	Transform2D draw_transform; // Used by transform command
	bool has_draw_transform = false;

	Color base_color = p_item->final_modulate;
	uint32_t base_flags = 0;
	Size2 texpixel_size;

	bool reclip = false;

	bool skipping = false;

	// TODO: consider making lights a per-batch property and then baking light operations in the shader for better performance.
	uint32_t lights[4] = { 0, 0, 0, 0 };

	uint16_t light_count = 0;
	uint16_t shadow_mask = 0;

	{
		const uint32_t active_light_count = state.active_point_lights.size();
		if (active_light_count > 0) {
			const ActivePointLight *active_lights = state.active_point_lights.ptr();
			const uint32_t item_light_mask = p_item->light_mask;
			const int32_t item_z_final = p_item->z_final;
			const Rect2 &item_rect = p_item->global_rect_cache;
			const uint16_t max_lights_for_item = data.max_lights_per_item - 1;
			for (uint32_t i = 0; i < active_light_count; i++) {
				const ActivePointLight &light = active_lights[i];
				if ((item_light_mask & light.item_mask) && item_z_final >= light.z_min && item_z_final <= light.z_max && item_rect.intersects(light.rect_cache)) {
					lights[light_count >> 2] |= light.render_index << ((light_count & 3) * 8);

					if (item_light_mask & light.item_shadow_mask) {
						shadow_mask |= 1 << light_count;
					}

					light_count++;

					if (light_count == max_lights_for_item) {
						break;
					}
				}
			}
		}

		base_flags |= light_count << INSTANCE_FLAGS_LIGHT_COUNT_SHIFT;
		base_flags |= shadow_mask << INSTANCE_FLAGS_SHADOW_MASKED_SHIFT;
	}

	bool lights_disabled = light_count == 0 && !state.using_directional_lights;

	if (lights_disabled != bool(state.canvas_instance_batches[state.current_batch_index].specialization & CanvasShaderGLES3::DISABLE_LIGHTING)) {
		_new_batch(r_batch_broken);
		state.canvas_instance_batches[state.current_batch_index].specialization ^= CanvasShaderGLES3::DISABLE_LIGHTING;
	}

	const RSE::CanvasItemTextureRepeat base_texture_repeat = p_item->texture_repeat == RSE::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT ? state.default_repeat : p_item->texture_repeat;
	const Item::Command *c = p_item->commands;
	while (c) {
		if (skipping && c->type != Item::Command::TYPE_ANIMATION_SLICE) {
			c = c->next;
			continue;
		}

		if (c->type == Item::Command::TYPE_TRANSFORM) {
			const Item::CommandTransform *transform = static_cast<const Item::CommandTransform *>(c);
			draw_transform = transform->xform;
			has_draw_transform = true;
			c = c->next;
			continue;
		}

		if (c->type == Item::Command::TYPE_CLIP_IGNORE) {
			const Item::CommandClipIgnore *ci = static_cast<const Item::CommandClipIgnore *>(c);
			if (current_clip) {
				if (ci->ignore != reclip) {
					_new_batch(r_batch_broken);
					if (ci->ignore) {
						state.canvas_instance_batches[state.current_batch_index].clip = nullptr;
						reclip = true;
					} else {
						state.canvas_instance_batches[state.current_batch_index].clip = current_clip;
						reclip = false;
					}
				}
			}
			c = c->next;
			r_batch_broken = false;
			continue;
		}

		if (c->type == Item::Command::TYPE_ANIMATION_SLICE) {
			const Item::CommandAnimationSlice *as = static_cast<const Item::CommandAnimationSlice *>(c);
			double current_time = RSG::rasterizer->get_total_time();
			double local_time = Math::fposmod(current_time - as->offset, as->animation_length);
			skipping = !(local_time >= as->slice_begin && local_time < as->slice_end);

			RenderingServerDefault::redraw_request(); // animation visible means redraw request
			c = c->next;
			continue;
		}

		if (c->type != Item::Command::TYPE_MESH) {
			// For Meshes, this gets updated below.
			_update_transform_2d_to_mat2x3(has_draw_transform ? (base_transform * draw_transform) : base_transform, state.instance_data_array[r_index].world);
		}

		state.instance_data_array[r_index].lights[0] = lights[0];
		state.instance_data_array[r_index].lights[1] = lights[1];
		state.instance_data_array[r_index].lights[2] = lights[2];
		state.instance_data_array[r_index].lights[3] = lights[3];

		state.instance_data_array[r_index].flags = base_flags;
		state.instance_data_array[r_index].instance_uniforms_ofs = p_item->instance_allocated_shader_uniforms_offset;

		// blend_color is only consumed by glBlendColor() in BLEND_MODE_LCD; keeping it default
		// for all other blend modes allows differently modulated items to stay in the same batch.
		Color blend_color;
		GLES3::CanvasShaderData::BlendMode blend_mode = p_blend_mode;
		RSE::CanvasItemTextureRepeat texture_repeat = base_texture_repeat;
		if (c->type == Item::Command::TYPE_RECT) {
			const Item::CommandRect *rect = static_cast<const Item::CommandRect *>(c);
			if (rect->flags & CANVAS_RECT_LCD) {
				blend_mode = GLES3::CanvasShaderData::BLEND_MODE_LCD;
				blend_color = rect->modulate * base_color;
			}

			if (rect->flags & CANVAS_RECT_TILE) {
				texture_repeat = RSE::CanvasItemTextureRepeat::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED;
			}
		}
		if (texture_repeat != state.canvas_instance_batches[state.current_batch_index].repeat) {
			_new_batch(r_batch_broken);
			state.canvas_instance_batches[state.current_batch_index].repeat = texture_repeat;
		}

		if (blend_mode != state.canvas_instance_batches[state.current_batch_index].blend_mode || (blend_mode == GLES3::CanvasShaderData::BLEND_MODE_LCD && blend_color != state.canvas_instance_batches[state.current_batch_index].blend_color)) {
			_new_batch(r_batch_broken);
			state.canvas_instance_batches[state.current_batch_index].blend_mode = blend_mode;
			state.canvas_instance_batches[state.current_batch_index].blend_color = blend_color;
		}

		switch (c->type) {
			case Item::Command::TYPE_RECT: {
				const Item::CommandRect *rect = static_cast<const Item::CommandRect *>(c);
				if (rect->texture != state.canvas_instance_batches[state.current_batch_index].tex || state.canvas_instance_batches[state.current_batch_index].command_type != Item::Command::TYPE_RECT) {
					_new_batch(r_batch_broken);
					state.canvas_instance_batches[state.current_batch_index].tex = rect->texture;
					state.canvas_instance_batches[state.current_batch_index].command_type = Item::Command::TYPE_RECT;
					state.canvas_instance_batches[state.current_batch_index].command = c;
					state.canvas_instance_batches[state.current_batch_index].specialization &= specialization_command_mask;
					state.canvas_instance_batches[state.current_batch_index].flags = 0;
				}

				_prepare_canvas_texture(rect->texture, state.canvas_instance_batches[state.current_batch_index].filter, state.canvas_instance_batches[state.current_batch_index].repeat, r_index, texpixel_size);

				Rect2 src_rect;
				Rect2 dst_rect;

				if (rect->texture != RID()) {
					src_rect = (rect->flags & CANVAS_RECT_REGION) ? Rect2(rect->source.position * texpixel_size, rect->source.size * texpixel_size) : Rect2(0, 0, 1, 1);
					dst_rect = Rect2(rect->rect.position, rect->rect.size);

					if (dst_rect.size.width < 0) {
						dst_rect.position.x += dst_rect.size.width;
						dst_rect.size.width *= -1;
					}
					if (dst_rect.size.height < 0) {
						dst_rect.position.y += dst_rect.size.height;
						dst_rect.size.height *= -1;
					}

					if (rect->flags & CANVAS_RECT_FLIP_H) {
						src_rect.size.x *= -1;
					}

					if (rect->flags & CANVAS_RECT_FLIP_V) {
						src_rect.size.y *= -1;
					}

					if (rect->flags & CANVAS_RECT_TRANSPOSE) {
						state.instance_data_array[r_index].flags |= INSTANCE_FLAGS_TRANSPOSE_RECT;
					}

					if (rect->flags & CANVAS_RECT_CLIP_UV) {
						state.instance_data_array[r_index].flags |= INSTANCE_FLAGS_CLIP_RECT_UV;
					}

				} else {
					dst_rect = Rect2(rect->rect.position, rect->rect.size);

					if (dst_rect.size.width < 0) {
						dst_rect.position.x += dst_rect.size.width;
						dst_rect.size.width *= -1;
					}
					if (dst_rect.size.height < 0) {
						dst_rect.position.y += dst_rect.size.height;
						dst_rect.size.height *= -1;
					}

					src_rect = Rect2(0, 0, 1, 1);
				}

				if (rect->flags & CANVAS_RECT_MSDF) {
					state.instance_data_array[r_index].flags |= INSTANCE_FLAGS_USE_MSDF;
					state.instance_data_array[r_index].msdf[0] = rect->px_range; // Pixel range.
					state.instance_data_array[r_index].msdf[1] = rect->outline; // Outline size.
					state.instance_data_array[r_index].msdf[2] = 0.f; // Reserved.
					state.instance_data_array[r_index].msdf[3] = 0.f; // Reserved.
				} else {
					if (rect->flags & CANVAS_RECT_LCD) {
						state.instance_data_array[r_index].flags |= INSTANCE_FLAGS_USE_LCD;
					}
					state.instance_data_array[r_index].msdf[0] = 0.f;
					state.instance_data_array[r_index].msdf[1] = 0.f;
					state.instance_data_array[r_index].msdf[2] = 0.f;
					state.instance_data_array[r_index].msdf[3] = 0.f;
				}

				state.instance_data_array[r_index].modulation[0] = rect->modulate.r * base_color.r;
				state.instance_data_array[r_index].modulation[1] = rect->modulate.g * base_color.g;
				state.instance_data_array[r_index].modulation[2] = rect->modulate.b * base_color.b;
				state.instance_data_array[r_index].modulation[3] = rect->modulate.a * base_color.a;

				state.instance_data_array[r_index].src_rect[0] = src_rect.position.x;
				state.instance_data_array[r_index].src_rect[1] = src_rect.position.y;
				state.instance_data_array[r_index].src_rect[2] = src_rect.size.width;
				state.instance_data_array[r_index].src_rect[3] = src_rect.size.height;

				state.instance_data_array[r_index].dst_rect[0] = dst_rect.position.x;
				state.instance_data_array[r_index].dst_rect[1] = dst_rect.position.y;
				state.instance_data_array[r_index].dst_rect[2] = dst_rect.size.width;
				state.instance_data_array[r_index].dst_rect[3] = dst_rect.size.height;

				_add_to_batch(r_index, r_batch_broken);
			} break;

			case Item::Command::TYPE_NINEPATCH: {
				const Item::CommandNinePatch *np = static_cast<const Item::CommandNinePatch *>(c);

				if (np->texture != state.canvas_instance_batches[state.current_batch_index].tex || state.canvas_instance_batches[state.current_batch_index].command_type != Item::Command::TYPE_NINEPATCH) {
					_new_batch(r_batch_broken);
					state.canvas_instance_batches[state.current_batch_index].tex = np->texture;
					state.canvas_instance_batches[state.current_batch_index].command_type = Item::Command::TYPE_NINEPATCH;
					state.canvas_instance_batches[state.current_batch_index].command = c;
					state.canvas_instance_batches[state.current_batch_index].specialization &= specialization_command_mask;
					state.canvas_instance_batches[state.current_batch_index].specialization |= CanvasShaderGLES3::USE_NINEPATCH;
					state.canvas_instance_batches[state.current_batch_index].flags = 0;
				}

				_prepare_canvas_texture(np->texture, state.canvas_instance_batches[state.current_batch_index].filter, state.canvas_instance_batches[state.current_batch_index].repeat, r_index, texpixel_size);

				Rect2 src_rect;
				Rect2 dst_rect(np->rect.position.x, np->rect.position.y, np->rect.size.x, np->rect.size.y);

				if (np->texture == RID()) {
					texpixel_size = Size2(1, 1);
					src_rect = Rect2(0, 0, 1, 1);

				} else {
					if (np->source != Rect2()) {
						src_rect = Rect2(np->source.position.x * texpixel_size.width, np->source.position.y * texpixel_size.height, np->source.size.x * texpixel_size.width, np->source.size.y * texpixel_size.height);
						state.instance_data_array[r_index].color_texture_pixel_size[0] = 1.0 / np->source.size.width;
						state.instance_data_array[r_index].color_texture_pixel_size[1] = 1.0 / np->source.size.height;

					} else {
						src_rect = Rect2(0, 0, 1, 1);
					}
				}

				state.instance_data_array[r_index].modulation[0] = np->color.r * base_color.r;
				state.instance_data_array[r_index].modulation[1] = np->color.g * base_color.g;
				state.instance_data_array[r_index].modulation[2] = np->color.b * base_color.b;
				state.instance_data_array[r_index].modulation[3] = np->color.a * base_color.a;

				state.instance_data_array[r_index].src_rect[0] = src_rect.position.x;
				state.instance_data_array[r_index].src_rect[1] = src_rect.position.y;
				state.instance_data_array[r_index].src_rect[2] = src_rect.size.width;
				state.instance_data_array[r_index].src_rect[3] = src_rect.size.height;

				state.instance_data_array[r_index].dst_rect[0] = dst_rect.position.x;
				state.instance_data_array[r_index].dst_rect[1] = dst_rect.position.y;
				state.instance_data_array[r_index].dst_rect[2] = dst_rect.size.width;
				state.instance_data_array[r_index].dst_rect[3] = dst_rect.size.height;

				state.instance_data_array[r_index].flags |= int(np->axis_x) << INSTANCE_FLAGS_NINEPATCH_H_MODE_SHIFT;
				state.instance_data_array[r_index].flags |= int(np->axis_y) << INSTANCE_FLAGS_NINEPATCH_V_MODE_SHIFT;

				if (np->draw_center) {
					state.instance_data_array[r_index].flags |= INSTANCE_FLAGS_NINEPACH_DRAW_CENTER;
				}

				state.instance_data_array[r_index].ninepatch_margins[0] = np->margin[SIDE_LEFT];
				state.instance_data_array[r_index].ninepatch_margins[1] = np->margin[SIDE_TOP];
				state.instance_data_array[r_index].ninepatch_margins[2] = np->margin[SIDE_RIGHT];
				state.instance_data_array[r_index].ninepatch_margins[3] = np->margin[SIDE_BOTTOM];

				_add_to_batch(r_index, r_batch_broken);

				// Restore if overridden.
				state.instance_data_array[r_index].color_texture_pixel_size[0] = texpixel_size.x;
				state.instance_data_array[r_index].color_texture_pixel_size[1] = texpixel_size.y;
			} break;

			case Item::Command::TYPE_POLYGON: {
				const Item::CommandPolygon *polygon = static_cast<const Item::CommandPolygon *>(c);

				// Polygon's can't be batched, so always create a new batch
				_new_batch(r_batch_broken);

				state.canvas_instance_batches[state.current_batch_index].tex = polygon->texture;
				state.canvas_instance_batches[state.current_batch_index].command_type = Item::Command::TYPE_POLYGON;
				state.canvas_instance_batches[state.current_batch_index].command = c;
				state.canvas_instance_batches[state.current_batch_index].specialization &= specialization_command_mask;
				state.canvas_instance_batches[state.current_batch_index].specialization |= CanvasShaderGLES3::USE_ATTRIBUTES;
				state.canvas_instance_batches[state.current_batch_index].flags = 0;

				_prepare_canvas_texture(polygon->texture, state.canvas_instance_batches[state.current_batch_index].filter, state.canvas_instance_batches[state.current_batch_index].repeat, r_index, texpixel_size);

				state.instance_data_array[r_index].modulation[0] = base_color.r;
				state.instance_data_array[r_index].modulation[1] = base_color.g;
				state.instance_data_array[r_index].modulation[2] = base_color.b;
				state.instance_data_array[r_index].modulation[3] = base_color.a;

				for (int j = 0; j < 4; j++) {
					state.instance_data_array[r_index].src_rect[j] = 0;
					state.instance_data_array[r_index].dst_rect[j] = 0;
					state.instance_data_array[r_index].ninepatch_margins[j] = 0;
				}

				_add_to_batch(r_index, r_batch_broken);
			} break;

			case Item::Command::TYPE_PRIMITIVE: {
				const Item::CommandPrimitive *primitive = static_cast<const Item::CommandPrimitive *>(c);

				if (primitive->point_count != state.canvas_instance_batches[state.current_batch_index].primitive_points || state.canvas_instance_batches[state.current_batch_index].command_type != Item::Command::TYPE_PRIMITIVE || primitive->texture != state.canvas_instance_batches[state.current_batch_index].tex) {
					_new_batch(r_batch_broken);
					state.canvas_instance_batches[state.current_batch_index].tex = primitive->texture;
					state.canvas_instance_batches[state.current_batch_index].primitive_points = primitive->point_count;
					state.canvas_instance_batches[state.current_batch_index].command_type = Item::Command::TYPE_PRIMITIVE;
					state.canvas_instance_batches[state.current_batch_index].command = c;
					state.canvas_instance_batches[state.current_batch_index].specialization &= specialization_command_mask;
					state.canvas_instance_batches[state.current_batch_index].specialization |= CanvasShaderGLES3::USE_PRIMITIVE;
					state.canvas_instance_batches[state.current_batch_index].flags = 0;
				}

				_prepare_canvas_texture(state.canvas_instance_batches[state.current_batch_index].tex, state.canvas_instance_batches[state.current_batch_index].filter, state.canvas_instance_batches[state.current_batch_index].repeat, r_index, texpixel_size);

				const uint32_t written_points = MIN(3u, primitive->point_count);
				for (uint32_t j = 0; j < written_points; j++) {
					state.instance_data_array[r_index].points[j * 2 + 0] = primitive->points[j].x;
					state.instance_data_array[r_index].points[j * 2 + 1] = primitive->points[j].y;
					state.instance_data_array[r_index].uvs[j * 2 + 0] = primitive->uvs[j].x;
					state.instance_data_array[r_index].uvs[j * 2 + 1] = primitive->uvs[j].y;
					Color col = primitive->colors[j] * base_color;
					state.instance_data_array[r_index].colors[j * 2 + 0] = (uint32_t(Math::make_half_float(col.g)) << 16) | Math::make_half_float(col.r);
					state.instance_data_array[r_index].colors[j * 2 + 1] = (uint32_t(Math::make_half_float(col.a)) << 16) | Math::make_half_float(col.b);
				}
				for (uint32_t j = written_points; j < 3u; j++) {
					state.instance_data_array[r_index].points[j * 2 + 0] = 0.0f;
					state.instance_data_array[r_index].points[j * 2 + 1] = 0.0f;
					state.instance_data_array[r_index].uvs[j * 2 + 0] = 0.0f;
					state.instance_data_array[r_index].uvs[j * 2 + 1] = 0.0f;
					state.instance_data_array[r_index].colors[j * 2 + 0] = 0;
					state.instance_data_array[r_index].colors[j * 2 + 1] = 0;
				}

				_add_to_batch(r_index, r_batch_broken);

				if (primitive->point_count == 4) {
					// Reset base data.
					_update_transform_2d_to_mat2x3(has_draw_transform ? (base_transform * draw_transform) : base_transform, state.instance_data_array[r_index].world);
					_prepare_canvas_texture(state.canvas_instance_batches[state.current_batch_index].tex, state.canvas_instance_batches[state.current_batch_index].filter, state.canvas_instance_batches[state.current_batch_index].repeat, r_index, texpixel_size);
					state.instance_data_array[r_index].lights[0] = lights[0];
					state.instance_data_array[r_index].lights[1] = lights[1];
					state.instance_data_array[r_index].lights[2] = lights[2];
					state.instance_data_array[r_index].lights[3] = lights[3];
					state.instance_data_array[r_index].flags = base_flags;
					state.instance_data_array[r_index].instance_uniforms_ofs = p_item->instance_allocated_shader_uniforms_offset;

					for (uint32_t j = 0; j < 3; j++) {
						int offset = j == 0 ? 0 : 1;
						// Second triangle in the quad. Uses vertices 0, 2, 3.
						state.instance_data_array[r_index].points[j * 2 + 0] = primitive->points[j + offset].x;
						state.instance_data_array[r_index].points[j * 2 + 1] = primitive->points[j + offset].y;
						state.instance_data_array[r_index].uvs[j * 2 + 0] = primitive->uvs[j + offset].x;
						state.instance_data_array[r_index].uvs[j * 2 + 1] = primitive->uvs[j + offset].y;
						Color col = primitive->colors[j + offset] * base_color;
						state.instance_data_array[r_index].colors[j * 2 + 0] = (uint32_t(Math::make_half_float(col.g)) << 16) | Math::make_half_float(col.r);
						state.instance_data_array[r_index].colors[j * 2 + 1] = (uint32_t(Math::make_half_float(col.a)) << 16) | Math::make_half_float(col.b);
					}

					_add_to_batch(r_index, r_batch_broken);
				}
			} break;

			case Item::Command::TYPE_MESH:
			case Item::Command::TYPE_MULTIMESH:
			case Item::Command::TYPE_PARTICLES: {
				// Meshes can't be batched, so always create a new batch.
				_new_batch(r_batch_broken);

				Color modulate(1, 1, 1, 1);
				state.canvas_instance_batches[state.current_batch_index].specialization &= specialization_command_mask;
				state.canvas_instance_batches[state.current_batch_index].specialization |= CanvasShaderGLES3::USE_ATTRIBUTES;
				state.canvas_instance_batches[state.current_batch_index].flags = 0;
				if (c->type == Item::Command::TYPE_MESH) {
					const Item::CommandMesh *m = static_cast<const Item::CommandMesh *>(c);
					state.canvas_instance_batches[state.current_batch_index].tex = m->texture;
					_update_transform_2d_to_mat2x3((has_draw_transform ? (base_transform * draw_transform) : base_transform) * m->transform, state.instance_data_array[r_index].world);
					modulate = m->modulate;

				} else if (c->type == Item::Command::TYPE_MULTIMESH) {
					const Item::CommandMultiMesh *mm = static_cast<const Item::CommandMultiMesh *>(c);
					state.canvas_instance_batches[state.current_batch_index].tex = mm->texture;
					state.canvas_instance_batches[state.current_batch_index].specialization |= CanvasShaderGLES3::USE_INSTANCING;

					if (GLES3::MeshStorage::get_singleton()->multimesh_uses_colors(mm->multimesh)) {
						state.canvas_instance_batches[state.current_batch_index].flags |= BATCH_FLAGS_INSTANCING_HAS_COLORS;
					}
					if (GLES3::MeshStorage::get_singleton()->multimesh_uses_custom_data(mm->multimesh)) {
						state.canvas_instance_batches[state.current_batch_index].flags |= BATCH_FLAGS_INSTANCING_HAS_CUSTOM_DATA;
					}
				} else if (c->type == Item::Command::TYPE_PARTICLES) {
					GLES3::ParticlesStorage *particles_storage = GLES3::ParticlesStorage::get_singleton();
					GLES3::TextureStorage *texture_storage = GLES3::TextureStorage::get_singleton();

					const Item::CommandParticles *pt = static_cast<const Item::CommandParticles *>(c);
					RID particles = pt->particles;
					state.canvas_instance_batches[state.current_batch_index].tex = pt->texture;
					state.canvas_instance_batches[state.current_batch_index].specialization |= CanvasShaderGLES3::USE_INSTANCING;
					state.canvas_instance_batches[state.current_batch_index].flags |= BATCH_FLAGS_INSTANCING_HAS_COLORS;
					state.canvas_instance_batches[state.current_batch_index].flags |= BATCH_FLAGS_INSTANCING_HAS_CUSTOM_DATA;

					if (particles_storage->particles_has_collision(particles) && texture_storage->render_target_is_sdf_enabled(p_render_target)) {
						// Pass collision information.
						Transform2D xform = p_item->final_transform;

						GLuint sdf_texture = texture_storage->render_target_get_sdf_texture(p_render_target);

						Rect2 to_screen;
						{
							Rect2 sdf_rect = texture_storage->render_target_get_sdf_rect(p_render_target);

							to_screen.size = Vector2(1.0 / sdf_rect.size.width, 1.0 / sdf_rect.size.height);
							to_screen.position = -sdf_rect.position * to_screen.size;
						}

						particles_storage->particles_set_canvas_sdf_collision(pt->particles, true, xform, to_screen, sdf_texture);
					} else {
						particles_storage->particles_set_canvas_sdf_collision(pt->particles, false, Transform2D(), Rect2(), 0);
					}
					r_sdf_used |= particles_storage->particles_has_collision(particles);
				}

				state.canvas_instance_batches[state.current_batch_index].command = c;
				state.canvas_instance_batches[state.current_batch_index].command_type = c->type;

				_prepare_canvas_texture(state.canvas_instance_batches[state.current_batch_index].tex, state.canvas_instance_batches[state.current_batch_index].filter, state.canvas_instance_batches[state.current_batch_index].repeat, r_index, texpixel_size);

				state.instance_data_array[r_index].modulation[0] = base_color.r * modulate.r;
				state.instance_data_array[r_index].modulation[1] = base_color.g * modulate.g;
				state.instance_data_array[r_index].modulation[2] = base_color.b * modulate.b;
				state.instance_data_array[r_index].modulation[3] = base_color.a * modulate.a;

				for (int j = 0; j < 4; j++) {
					state.instance_data_array[r_index].src_rect[j] = 0;
					state.instance_data_array[r_index].dst_rect[j] = 0;
					state.instance_data_array[r_index].ninepatch_margins[j] = 0;
				}
				_add_to_batch(r_index, r_batch_broken);
			} break;

			case Item::Command::TYPE_TRANSFORM:
			case Item::Command::TYPE_CLIP_IGNORE:
			case Item::Command::TYPE_ANIMATION_SLICE: {
				// Handled before instance data recording above.
			} break;
		}

		c = c->next;
		r_batch_broken = false;
	}

	if (current_clip && reclip) {
		//will make it re-enable clipping if needed afterwards
		current_clip = nullptr;
	}
}

_FORCE_INLINE_ static uint32_t _indices_to_primitives(RSE::PrimitiveType p_primitive, uint32_t p_indices) {
	static const uint32_t divisor[RSE::PRIMITIVE_MAX] = { 1, 2, 1, 3, 1 };
	static const uint32_t subtractor[RSE::PRIMITIVE_MAX] = { 0, 0, 1, 0, 2 };
	return (p_indices - subtractor[p_primitive]) / divisor[p_primitive];
}

void RasterizerCanvasGLES3::_render_batch(Light *p_lights, uint32_t p_index, GLuint &r_current_vao, GLuint &r_current_array_buffer, RenderingServerTypes::RenderInfo *r_render_info) {
	ERR_FAIL_NULL(state.canvas_instance_batches[state.current_batch_index].command);

	// Used by Polygon and Mesh.
	static const GLenum prim[5] = { GL_POINTS, GL_LINES, GL_LINE_STRIP, GL_TRIANGLES, GL_TRIANGLE_STRIP };

	_bind_canvas_texture(state.canvas_instance_batches[p_index].tex, state.canvas_instance_batches[p_index].filter, state.canvas_instance_batches[p_index].repeat);

	switch (state.canvas_instance_batches[p_index].command_type) {
		case Item::Command::TYPE_RECT:
		case Item::Command::TYPE_NINEPATCH: {
			if (r_current_vao != data.indexed_quad_array) {
				glBindVertexArray(data.indexed_quad_array);
				r_current_vao = data.indexed_quad_array;
			}
			const GLuint instance_buffer = state.canvas_instance_data_buffers[state.current_data_buffer_index].instance_buffers[state.canvas_instance_batches[p_index].instance_buffer_index];
			if (r_current_array_buffer != instance_buffer) {
				glBindBuffer(GL_ARRAY_BUFFER, instance_buffer);
				r_current_array_buffer = instance_buffer;
			}
			uint32_t range_start = state.canvas_instance_batches[p_index].start * sizeof(InstanceData);
			_set_instance_attribute_pointers(range_start, false);

			glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr, state.canvas_instance_batches[p_index].instance_count);

			if (r_render_info) {
				r_render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RSE::VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME] += state.canvas_instance_batches[p_index].instance_count;
				r_render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RSE::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME] += 2 * state.canvas_instance_batches[p_index].instance_count;
				r_render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME]++;
			}

		} break;

		case Item::Command::TYPE_POLYGON: {
			const Item::CommandPolygon *polygon = static_cast<const Item::CommandPolygon *>(state.canvas_instance_batches[p_index].command);

			PolygonBuffers *pb = polygon_buffers.polygons.getptr(polygon->polygon.polygon_id);
			ERR_FAIL_NULL(pb);

			glBindVertexArray(pb->vertex_array);
			glBindBuffer(GL_ARRAY_BUFFER, state.canvas_instance_data_buffers[state.current_data_buffer_index].instance_buffers[state.canvas_instance_batches[p_index].instance_buffer_index]);

			uint32_t range_start = state.canvas_instance_batches[p_index].start * sizeof(InstanceData);
			_enable_attributes(range_start, false);

			if (pb->color_disabled && pb->color != Color(1.0, 1.0, 1.0, 1.0)) {
				glVertexAttrib4f(RSE::ARRAY_COLOR, pb->color.r, pb->color.g, pb->color.b, pb->color.a);
			}

			if (pb->index_buffer != 0) {
				glDrawElementsInstanced(prim[polygon->primitive], pb->count, GL_UNSIGNED_INT, nullptr, 1);
			} else {
				glDrawArraysInstanced(prim[polygon->primitive], 0, pb->count, 1);
			}
			glBindVertexArray(0);
			r_current_vao = 0;
			r_current_array_buffer = 0;

			if (pb->color_disabled && pb->color != Color(1.0, 1.0, 1.0, 1.0)) {
				// Reset so this doesn't pollute other draw calls.
				glVertexAttrib4f(RSE::ARRAY_COLOR, 1.0, 1.0, 1.0, 1.0);
			}

			if (r_render_info) {
				r_render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RSE::VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME]++;
				r_render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RSE::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME] += _indices_to_primitives(polygon->primitive, pb->count);
				r_render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME]++;
			}
		} break;

		case Item::Command::TYPE_PRIMITIVE: {
			if (r_current_vao != data.canvas_quad_array) {
				glBindVertexArray(data.canvas_quad_array);
				r_current_vao = data.canvas_quad_array;
			}
			const GLuint instance_buffer = state.canvas_instance_data_buffers[state.current_data_buffer_index].instance_buffers[state.canvas_instance_batches[p_index].instance_buffer_index];
			if (r_current_array_buffer != instance_buffer) {
				glBindBuffer(GL_ARRAY_BUFFER, instance_buffer);
				r_current_array_buffer = instance_buffer;
			}
			uint32_t range_start = state.canvas_instance_batches[p_index].start * sizeof(InstanceData);
			_set_instance_attribute_pointers(range_start, true);

			const GLenum primitive[5] = { GL_POINTS, GL_POINTS, GL_LINES, GL_TRIANGLES, GL_TRIANGLES };
			int instance_count = state.canvas_instance_batches[p_index].instance_count;
			ERR_FAIL_COND(instance_count <= 0);
			if (instance_count >= 1) {
				glDrawArraysInstanced(primitive[state.canvas_instance_batches[p_index].primitive_points], 0, state.canvas_instance_batches[p_index].primitive_points, instance_count);
			}
			if (r_render_info) {
				const RSE::PrimitiveType rs_primitive[5] = { RSE::PRIMITIVE_POINTS, RSE::PRIMITIVE_POINTS, RSE::PRIMITIVE_LINES, RSE::PRIMITIVE_TRIANGLES, RSE::PRIMITIVE_TRIANGLES };
				r_render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RSE::VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME] += instance_count;
				r_render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RSE::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME] += _indices_to_primitives(rs_primitive[state.canvas_instance_batches[p_index].primitive_points], state.canvas_instance_batches[p_index].primitive_points) * instance_count;
				r_render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME]++;
			}

		} break;

		case Item::Command::TYPE_MESH:
		case Item::Command::TYPE_MULTIMESH:
		case Item::Command::TYPE_PARTICLES: {
			GLES3::MeshStorage *mesh_storage = GLES3::MeshStorage::get_singleton();
			GLES3::ParticlesStorage *particles_storage = GLES3::ParticlesStorage::get_singleton();
			RID mesh;
			RID mesh_instance;
			uint32_t instance_count = 1;
			GLuint instance_buffer = 0;
			uint32_t instance_stride = 0;
			uint32_t instance_color_offset = 0;
			bool instance_uses_color = false;
			bool instance_uses_custom_data = false;
			bool use_instancing = false;

			if (state.canvas_instance_batches[p_index].command_type == Item::Command::TYPE_MESH) {
				const Item::CommandMesh *m = static_cast<const Item::CommandMesh *>(state.canvas_instance_batches[p_index].command);
				mesh = m->mesh;
				mesh_instance = m->mesh_instance;

			} else if (state.canvas_instance_batches[p_index].command_type == Item::Command::TYPE_MULTIMESH) {
				const Item::CommandMultiMesh *mm = static_cast<const Item::CommandMultiMesh *>(state.canvas_instance_batches[p_index].command);
				RID multimesh = mm->multimesh;
				mesh = mesh_storage->multimesh_get_mesh(multimesh);

				if (mesh_storage->multimesh_get_transform_format(multimesh) != RSE::MULTIMESH_TRANSFORM_2D) {
					break;
				}

				instance_count = mesh_storage->multimesh_get_instances_to_draw(multimesh);

				if (instance_count == 0) {
					break;
				}

				instance_buffer = mesh_storage->multimesh_get_gl_buffer(multimesh);
				instance_stride = mesh_storage->multimesh_get_stride(multimesh);
				instance_color_offset = mesh_storage->multimesh_get_color_offset(multimesh);
				instance_uses_color = mesh_storage->multimesh_uses_colors(multimesh);
				instance_uses_custom_data = mesh_storage->multimesh_uses_custom_data(multimesh);
				use_instancing = true;

			} else if (state.canvas_instance_batches[p_index].command_type == Item::Command::TYPE_PARTICLES) {
				const Item::CommandParticles *pt = static_cast<const Item::CommandParticles *>(state.canvas_instance_batches[p_index].command);
				RID particles = pt->particles;
				mesh = particles_storage->particles_get_draw_pass_mesh(particles, 0);

				ERR_BREAK(particles_storage->particles_get_mode(particles) != RSE::PARTICLES_MODE_2D);
				particles_storage->particles_request_process(particles);

				if (particles_storage->particles_is_inactive(particles)) {
					break;
				}

				RenderingServerDefault::redraw_request(); // Active particles means redraw request.

				int dpc = particles_storage->particles_get_draw_passes(particles);
				if (dpc == 0) {
					break; // Nothing to draw.
				}

				instance_count = particles_storage->particles_get_amount(particles);
				instance_buffer = particles_storage->particles_get_gl_buffer(particles);
				instance_stride = 12; // 8 bytes for instance transform and 4 bytes for packed color and custom.
				instance_color_offset = 8; // 8 bytes for instance transform.
				instance_uses_color = true;
				instance_uses_custom_data = true;
				use_instancing = true;
			}

			ERR_FAIL_COND(mesh.is_null());

			uint32_t surf_count = mesh_storage->mesh_get_surface_count(mesh);

			for (uint32_t j = 0; j < surf_count; j++) {
				void *surface = mesh_storage->mesh_get_surface(mesh, j);

				RSE::PrimitiveType primitive = mesh_storage->mesh_surface_get_primitive(surface);
				ERR_CONTINUE(primitive < 0 || primitive >= RSE::PRIMITIVE_MAX);

				GLuint vertex_array_gl = 0;
				GLuint index_array_gl = 0;

				uint64_t vertex_input_mask = state.canvas_instance_batches[p_index].vertex_input_mask;

				if (mesh_instance.is_valid()) {
					mesh_storage->mesh_instance_surface_get_vertex_arrays_and_format(mesh_instance, j, vertex_input_mask, false, vertex_array_gl);
				} else {
					mesh_storage->mesh_surface_get_vertex_arrays_and_format(surface, vertex_input_mask, false, vertex_array_gl);
				}

				index_array_gl = mesh_storage->mesh_surface_get_index_buffer(surface, 0);
				bool use_index_buffer = false;
				glBindVertexArray(vertex_array_gl);
				r_current_vao = vertex_array_gl;
				glBindBuffer(GL_ARRAY_BUFFER, state.canvas_instance_data_buffers[state.current_data_buffer_index].instance_buffers[state.canvas_instance_batches[p_index].instance_buffer_index]);
				r_current_array_buffer = 0;

				uint32_t range_start = state.canvas_instance_batches[p_index].start * sizeof(InstanceData);
				_enable_attributes(range_start, false, instance_count);

				if (index_array_gl != 0) {
					glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_array_gl);
					use_index_buffer = true;
				}

				if (use_instancing) {
					if (instance_buffer == 0) {
						break;
					}
					// Bind instance buffers.
					glBindBuffer(GL_ARRAY_BUFFER, instance_buffer);
					glEnableVertexAttribArray(1);
					glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, instance_stride * sizeof(float), CAST_INT_TO_UCHAR_PTR(0));
					glVertexAttribDivisor(1, 1);
					glEnableVertexAttribArray(2);
					glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, instance_stride * sizeof(float), CAST_INT_TO_UCHAR_PTR(4 * 4));
					glVertexAttribDivisor(2, 1);

					if (instance_uses_color || instance_uses_custom_data) {
						glEnableVertexAttribArray(5);
						glVertexAttribIPointer(5, 4, GL_UNSIGNED_INT, instance_stride * sizeof(float), CAST_INT_TO_UCHAR_PTR(instance_color_offset * sizeof(float)));
						glVertexAttribDivisor(5, 1);
					} else {
						// Set all default instance color and custom data values to 1.0 or 0.0 using a compressed format.
						uint16_t zero = Math::make_half_float(0.0f);
						uint16_t one = Math::make_half_float(1.0f);
						GLuint default_color = (uint32_t(one) << 16) | one;
						GLuint default_custom = (uint32_t(zero) << 16) | zero;
						glVertexAttribI4ui(5, default_color, default_color, default_custom, default_custom);
					}
				}

				GLenum primitive_gl = prim[int(primitive)];

				uint32_t vertex_count = mesh_storage->mesh_surface_get_vertices_drawn_count(surface);

				if (use_index_buffer) {
					glDrawElementsInstanced(primitive_gl, vertex_count, mesh_storage->mesh_surface_get_index_type(surface), nullptr, instance_count);
				} else {
					glDrawArraysInstanced(primitive_gl, 0, vertex_count, instance_count);
				}
				glBindBuffer(GL_ARRAY_BUFFER, 0);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
				r_current_array_buffer = 0;
				if (use_instancing) {
					glDisableVertexAttribArray(5);
					glDisableVertexAttribArray(8);
					glDisableVertexAttribArray(9);
					glDisableVertexAttribArray(10);
				}
				glBindVertexArray(0);
				r_current_vao = 0;
				if (r_render_info) {
					// Meshes, Particles, and MultiMesh are always just one object with one draw call.
					r_render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RSE::VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME]++;
					r_render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RSE::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME] += _indices_to_primitives(primitive, vertex_count) * instance_count;
					r_render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME]++;
				}
			}

		} break;
		case Item::Command::TYPE_TRANSFORM:
		case Item::Command::TYPE_CLIP_IGNORE:
		case Item::Command::TYPE_ANIMATION_SLICE: {
			// Can ignore these as they only impact batch creation.
		} break;
	}
}

void RasterizerCanvasGLES3::_add_to_batch(uint32_t &r_index, bool &r_batch_broken) {
	state.canvas_instance_batches[state.current_batch_index].instance_count++;
	r_index++;
	if (r_index + state.last_item_index >= data.max_instances_per_buffer) {
		// Copy over all data needed for rendering right away
		// then go back to recording item commands.
		glBindBuffer(GL_ARRAY_BUFFER, state.canvas_instance_data_buffers[state.current_data_buffer_index].instance_buffers[state.current_instance_buffer_index]);
#ifdef WEB_ENABLED
		glBufferSubData(GL_ARRAY_BUFFER, state.last_item_index * sizeof(InstanceData), sizeof(InstanceData) * r_index, state.instance_data_array);
#else
		// On Desktop and mobile we map the memory without synchronizing for maximum speed.
		void *buffer = glMapBufferRange(GL_ARRAY_BUFFER, state.last_item_index * sizeof(InstanceData), r_index * sizeof(InstanceData), GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
		memcpy(buffer, state.instance_data_array, r_index * sizeof(InstanceData));
		glUnmapBuffer(GL_ARRAY_BUFFER);
#endif
		_allocate_instance_buffer();
		r_index = 0;
		state.last_item_index = 0;
		r_batch_broken = false; // Force a new batch to be created
		_new_batch(r_batch_broken);
		state.canvas_instance_batches[state.current_batch_index].start = 0;
	}
}

void RasterizerCanvasGLES3::_new_batch(bool &r_batch_broken) {
	if (state.canvas_instance_batches.is_empty()) {
		Batch new_batch;
		new_batch.instance_buffer_index = state.current_instance_buffer_index;
		state.canvas_instance_batches.push_back(new_batch);
		return;
	}

	if (r_batch_broken || state.canvas_instance_batches[state.current_batch_index].instance_count == 0) {
		return;
	}

	r_batch_broken = true;

	// Copy the properties of the current batch, we will manually update the things that changed.
	Batch new_batch = state.canvas_instance_batches[state.current_batch_index];
	new_batch.instance_count = 0;
	new_batch.start = state.canvas_instance_batches[state.current_batch_index].start + state.canvas_instance_batches[state.current_batch_index].instance_count;
	new_batch.instance_buffer_index = state.current_instance_buffer_index;
	state.current_batch_index++;
	state.canvas_instance_batches.push_back(new_batch);
}

void RasterizerCanvasGLES3::_enable_attributes(uint32_t p_start, bool p_primitive, uint32_t p_rate) {
	uint32_t split = p_primitive ? 13 : 14;
	for (uint32_t i = 8; i < split; i++) {
		glEnableVertexAttribArray(i);
		glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData), CAST_INT_TO_UCHAR_PTR(p_start + (i - 8) * 4 * sizeof(float)));
		glVertexAttribDivisor(i, p_rate);
	}
	for (uint32_t i = split; i <= 15; i++) {
		glEnableVertexAttribArray(i);
		glVertexAttribIPointer(i, 4, GL_UNSIGNED_INT, sizeof(InstanceData), CAST_INT_TO_UCHAR_PTR(p_start + (i - 8) * 4 * sizeof(float)));
		glVertexAttribDivisor(i, p_rate);
	}
}

void RasterizerCanvasGLES3::_set_instance_attribute_pointers(uint32_t p_start, bool p_primitive) {
	const uint32_t split = p_primitive ? 13 : 14;
	for (uint32_t i = 8; i < split; i++) {
		glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData), CAST_INT_TO_UCHAR_PTR(p_start + (i - 8) * 4 * sizeof(float)));
	}
	for (uint32_t i = split; i <= 15; i++) {
		glVertexAttribIPointer(i, 4, GL_UNSIGNED_INT, sizeof(InstanceData), CAST_INT_TO_UCHAR_PTR(p_start + (i - 8) * 4 * sizeof(float)));
	}
}
RID RasterizerCanvasGLES3::light_create() {
	CanvasLight canvas_light;
	return canvas_light_owner.make_rid(canvas_light);
}

void RasterizerCanvasGLES3::light_set_texture(RID p_rid, RID p_texture) {
	GLES3::TextureStorage *texture_storage = GLES3::TextureStorage::get_singleton();

	CanvasLight *cl = canvas_light_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(cl);
	if (cl->texture == p_texture) {
		return;
	}

	ERR_FAIL_COND(p_texture.is_valid() && !texture_storage->owns_texture(p_texture));

	if (cl->texture.is_valid()) {
		texture_storage->texture_remove_from_texture_atlas(cl->texture);
	}
	cl->texture = p_texture;

	if (cl->texture.is_valid()) {
		texture_storage->texture_add_to_texture_atlas(cl->texture);
	}
}

void RasterizerCanvasGLES3::light_set_use_shadow(RID p_rid, bool p_enable) {
	CanvasLight *cl = canvas_light_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(cl);

	if (cl->shadow.enabled && !p_enable) {
		_release_shadow_band(p_rid, cl);
	}
	cl->shadow.enabled = p_enable;
}

bool RasterizerCanvasGLES3::shadow_occluder_lists_match(const LocalVector<CachedShadowOccluder> &p_first, const LocalVector<CachedShadowOccluder> &p_second) {
	if (p_first.size() != p_second.size()) {
		return false;
	}
	constexpr real_t basis_epsilon = real_t(1e-5);
	constexpr real_t origin_epsilon = real_t(1e-3);
	for (uint32_t i = 0; i < p_first.size(); i++) {
		const CachedShadowOccluder &first = p_first[i];
		const CachedShadowOccluder &second = p_second[i];
		if (first.polygon_id != second.polygon_id || first.polygon_revision != second.polygon_revision) {
			return false;
		}
		if (Math::abs(first.modelview.columns[0].x - second.modelview.columns[0].x) > basis_epsilon ||
				Math::abs(first.modelview.columns[0].y - second.modelview.columns[0].y) > basis_epsilon ||
				Math::abs(first.modelview.columns[1].x - second.modelview.columns[1].x) > basis_epsilon ||
				Math::abs(first.modelview.columns[1].y - second.modelview.columns[1].y) > basis_epsilon ||
				Math::abs(first.modelview.columns[2].x - second.modelview.columns[2].x) > origin_epsilon ||
				Math::abs(first.modelview.columns[2].y - second.modelview.columns[2].y) > origin_epsilon) {
			return false;
		}
	}
	return true;
}

int32_t RasterizerCanvasGLES3::select_shadow_band(const LocalVector<ShadowBandCache> &p_bands, RID p_light_rid, int32_t p_preferred_band, uint64_t p_current_pass) {
	const uint32_t band_count = p_bands.size();
	if (band_count == 0) {
		return -1;
	}
	if (p_preferred_band >= 0 && uint32_t(p_preferred_band) < band_count && p_bands[p_preferred_band].light_rid == p_light_rid) {
		return p_preferred_band;
	}

	int32_t best_band = -1;
	uint64_t oldest_pass = UINT64_MAX;
	for (uint32_t i = 0; i < band_count; i++) {
		const ShadowBandCache &band = p_bands[i];
		if (band.last_used_pass == p_current_pass) {
			continue;
		}
		if (!band.valid && band.light_rid.is_null()) {
			return int32_t(i);
		}
		if (best_band < 0 || band.last_used_pass < oldest_pass) {
			oldest_pass = band.last_used_pass;
			best_band = int32_t(i);
		}
	}
	return best_band;
}

int32_t RasterizerCanvasGLES3::_acquire_shadow_band(RID p_light_rid, CanvasLight *p_light) {
	const int32_t band_index = select_shadow_band(state.shadow_band_cache, p_light_rid, p_light->cached_shadow_band, state.shadow_pass_counter);
	ERR_FAIL_COND_V(band_index < 0, -1);

	ShadowBandCache &band = state.shadow_band_cache[band_index];
	if (band.light_rid != p_light_rid) {
		if (band.light_rid.is_valid()) {
			CanvasLight *previous_owner = canvas_light_owner.get_or_null(band.light_rid);
			if (previous_owner != nullptr && previous_owner->cached_shadow_band == band_index) {
				previous_owner->cached_shadow_band = -1;
			}
		}
		band.light_rid = p_light_rid;
		band.valid = false;
		band.occluders.clear();
	}
	band.last_used_pass = state.shadow_pass_counter;
	p_light->cached_shadow_band = band_index;
	return band_index;
}

void RasterizerCanvasGLES3::_release_shadow_band(RID p_light_rid, CanvasLight *p_light) {
	const int32_t band_index = p_light->cached_shadow_band;
	if (band_index >= 0 && uint32_t(band_index) < state.shadow_band_cache.size()) {
		ShadowBandCache &band = state.shadow_band_cache[band_index];
		if (band.light_rid == p_light_rid) {
			band.light_rid = RID();
			band.valid = false;
			band.occluders.clear();
		}
	}
	p_light->cached_shadow_band = -1;
}

uint8_t RasterizerCanvasGLES3::compute_shadow_occluder_quadrant_mask(const Rect2 &p_light_local_aabb, float p_far) {
	const Rect2 bounds = p_light_local_aabb.abs();
	const float min_x = bounds.position.x;
	const float max_x = min_x + bounds.size.x;
	const float min_y = bounds.position.y;
	const float max_y = min_y + bounds.size.y;

	uint8_t mask = 0;
	// Quadrant 0 (+X): 0 <= x <= p_far and -x <= y <= x.
	if (max_x >= 0.0f && min_x <= p_far && max_x >= min_y && max_x >= -max_y) {
		mask |= 1u << 0;
	}
	// Quadrant 1 (+Y): 0 <= y <= p_far and -y <= x <= y.
	if (max_y >= 0.0f && min_y <= p_far && max_y >= min_x && max_y >= -max_x) {
		mask |= 1u << 1;
	}
	// Quadrant 2 (-X): -p_far <= x <= 0 and x <= y <= -x.
	if (min_x <= 0.0f && max_x >= -p_far && -min_x >= min_y && -min_x >= -max_y) {
		mask |= 1u << 2;
	}
	// Quadrant 3 (-Y): -p_far <= y <= 0 and y <= x <= -y.
	if (min_y <= 0.0f && max_y >= -p_far && -min_y >= min_x && -min_y >= -max_x) {
		mask |= 1u << 3;
	}
	return mask;
}

void RasterizerCanvasGLES3::_invalidate_shadow_framebuffer(bool p_invalidate_color, bool p_invalidate_depth) {
	GLenum attachments[2];
	GLsizei attachment_count = 0;
	if (p_invalidate_color) {
		attachments[attachment_count++] = GL_COLOR_ATTACHMENT0;
	}
	if (p_invalidate_depth) {
		attachments[attachment_count++] = GL_DEPTH_ATTACHMENT;
	}
	if (attachment_count == 0) {
		return;
	}
#ifdef GLAD_ENABLED
	if (glInvalidateFramebuffer != nullptr) {
		glInvalidateFramebuffer(GL_FRAMEBUFFER, attachment_count, attachments);
	}
#else
	glInvalidateFramebuffer(GL_FRAMEBUFFER, attachment_count, attachments);
#endif
}

void RasterizerCanvasGLES3::_apply_shadow_cull_mode(RSE::CanvasOccluderPolygonCullMode p_cull_mode) {
	if (p_cull_mode == state.shadow_cull_mode) {
		return;
	}
	if (p_cull_mode == RSE::CANVAS_OCCLUDER_POLYGON_CULL_DISABLED) {
		glDisable(GL_CULL_FACE);
	} else {
		if (state.shadow_cull_mode == RSE::CANVAS_OCCLUDER_POLYGON_CULL_DISABLED) {
			glEnable(GL_CULL_FACE);
		}
		glCullFace(p_cull_mode == RSE::CANVAS_OCCLUDER_POLYGON_CULL_CLOCKWISE ? GL_FRONT : GL_BACK);
	}
	state.shadow_cull_mode = p_cull_mode;
}

void RasterizerCanvasGLES3::shadow_pass_begin() {
	if (state.shadow_pass_active) {
		return;
	}
	state.shadow_pass_active = true;
	state.shadow_pass_counter++;
	_update_shadow_atlas();
}

void RasterizerCanvasGLES3::_ensure_shadow_pass_bound() {
	if (state.shadow_fb_bound || state.shadow_fb == 0) {
		return;
	}
	state.shadow_fb_bound = true;
	state.shadow_cull_mode = RSE::CANVAS_OCCLUDER_POLYGON_CULL_DISABLED;

	glBindFramebuffer(GL_FRAMEBUFFER, state.shadow_fb);
	// Cached shadow bands live in the color attachment across frames, so only invalidate
	// the transient depth renderbuffer to avoid discarding cached bands on TBDR GPUs.
	_invalidate_shadow_framebuffer(false, true);

	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDisable(GL_BLEND);
	glEnable(GL_SCISSOR_TEST);
	glCullFace(GL_BACK);
	glDisable(GL_CULL_FACE);

	GLES3::Config *config = GLES3::Config::get_singleton();
	CanvasOcclusionShaderGLES3::ShaderVariant variant = config->float_texture_supported ? CanvasOcclusionShaderGLES3::MODE_SHADOW : CanvasOcclusionShaderGLES3::MODE_SHADOW_RGBA;
	state.shadow_shader_bound = shadow_render.shader.version_bind_shader(shadow_render.shader_version, variant);
}

void RasterizerCanvasGLES3::shadow_pass_end() {
	if (!state.shadow_pass_active) {
		return;
	}
	state.shadow_pass_active = false;
	if (!state.shadow_fb_bound) {
		return;
	}
	state.shadow_fb_bound = false;
	state.shadow_shader_bound = false;

	glBindVertexArray(0);
	// Discard the depth renderbuffer so TBDR GPUs do not write it back to memory.
	_invalidate_shadow_framebuffer(false, true);
	glBindFramebuffer(GL_FRAMEBUFFER, GLES3::TextureStorage::system_fbo);
	glDepthMask(GL_FALSE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	state.shadow_cull_mode = RSE::CANVAS_OCCLUDER_POLYGON_CULL_DISABLED;
}

void RasterizerCanvasGLES3::light_update_shadow(RID p_rid, int p_shadow_index, const Transform2D &p_light_xform, int p_light_mask, float p_near, float p_far, LightOccluderInstance *p_occluders, const Rect2 &p_light_rect) {
	GLES3::Config *config = GLES3::Config::get_singleton();

	CanvasLight *cl = canvas_light_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(cl);
	ERR_FAIL_COND(!cl->shadow.enabled);
	ERR_FAIL_INDEX(p_shadow_index, (int)data.max_lights_per_render);

	const bool owns_shadow_pass = !state.shadow_pass_active;
	if (owns_shadow_pass) {
		shadow_pass_begin();
	}

	const int32_t shadow_band = _acquire_shadow_band(p_rid, cl);
	if (shadow_band < 0) {
		if (owns_shadow_pass) {
			shadow_pass_end();
		}
		return;
	}

	cl->shadow.z_far = p_far;
	cl->shadow.y_offset = float(shadow_band * 2 + 1) / float(data.max_lights_per_render * 2);

	if (state.shadow_fb == 0) {
		if (owns_shadow_pass) {
			shadow_pass_end();
		}
		return;
	}

	state.shadow_occluder_entries.clear();
	state.shadow_occluder_cache_scratch.clear();
	uint8_t combined_quadrant_mask = 0;

	for (LightOccluderInstance *instance = p_occluders; instance != nullptr; instance = instance->next) {
		OccluderPolygon *co = occluder_polygon_owner.get_or_null(instance->occluder);

		if (!co || co->vertex_array == 0 || !(p_light_mask & instance->light_mask)) {
			continue;
		}
		if (!p_light_rect.intersects_transformed(instance->xform_cache, instance->aabb_cache)) {
			continue;
		}

		const Transform2D modelview = p_light_xform * instance->xform_cache;
		const uint8_t quadrant_mask = compute_shadow_occluder_quadrant_mask(modelview.xform(instance->aabb_cache), p_far);
		if (quadrant_mask == 0) {
			continue;
		}

		state.shadow_occluder_entries.push_back({ co, modelview, quadrant_mask });
		state.shadow_occluder_cache_scratch.push_back({ instance->occluder.get_id(), co->revision, reinterpret_cast<uintptr_t>(instance), modelview });
		combined_quadrant_mask |= quadrant_mask;
	}

	if (state.shadow_occluder_cache_scratch.size() > 1) {
		state.shadow_occluder_cache_scratch.sort();
	}

	ShadowBandCache &band = state.shadow_band_cache[shadow_band];
	if (band.valid &&
			!band.is_directional &&
			band.atlas_generation == state.shadow_atlas_generation &&
			Math::abs(band.near_plane - p_near) <= 1e-5f &&
			Math::abs(band.far_plane - p_far) <= 1e-4f &&
			shadow_occluder_lists_match(band.occluders, state.shadow_occluder_cache_scratch)) {
		cl->shadow.z_far = band.far_plane;
		if (owns_shadow_pass) {
			shadow_pass_end();
		}
		return;
	}

	_ensure_shadow_pass_bound();

	glViewport(0, shadow_band * 2, state.shadow_texture_size, 2);
	glScissor(0, shadow_band * 2, state.shadow_texture_size, 2);
	glClearColor(p_far, p_far, p_far, 1.0);
	RasterizerUtilGLES3::clear_depth(1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	if (!state.shadow_shader_bound) {
		band.valid = false;
		if (owns_shadow_pass) {
			shadow_pass_end();
		}
		return;
	}

	if (state.shadow_occluder_entries.is_empty()) {
		band.atlas_generation = state.shadow_atlas_generation;
		band.valid = true;
		band.is_directional = false;
		band.near_plane = p_near;
		band.far_plane = p_far;
		band.occluders.clear();
		if (owns_shadow_pass) {
			shadow_pass_end();
		}
		return;
	}

	CanvasOcclusionShaderGLES3::ShaderVariant variant = config->float_texture_supported ? CanvasOcclusionShaderGLES3::MODE_SHADOW : CanvasOcclusionShaderGLES3::MODE_SHADOW_RGBA;

	Projection projection;
	{
		real_t fov = 90;
		real_t nearp = p_near;
		real_t farp = p_far;
		real_t aspect = 1.0;

		real_t ymax = nearp * Math::tan(Math::deg_to_rad(fov * 0.5));
		real_t ymin = -ymax;
		real_t xmin = ymin * aspect;
		real_t xmax = ymax * aspect;

		projection.set_frustum(xmin, xmax, ymin, ymax, nearp, farp);
	}

	// Precomputed:
	// Vector3 cam_target = Basis::from_euler(Vector3(0, 0, Math::TAU * ((i + 3) / 4.0))).xform(Vector3(0, 1, 0));
	// projection = projection * Projection(Transform3D().looking_at(cam_targets[i], Vector3(0, 0, -1)).affine_inverse());
	const Projection projections[4] = {
		projection * Projection(Vector4(0, 0, -1, 0), Vector4(1, 0, 0, 0), Vector4(0, -1, 0, 0), Vector4(0, 0, 0, 1)),

		projection * Projection(Vector4(-1, 0, 0, 0), Vector4(0, 0, -1, 0), Vector4(0, -1, 0, 0), Vector4(0, 0, 0, 1)),

		projection * Projection(Vector4(0, 0, 1, 0), Vector4(-1, 0, 0, 0), Vector4(0, -1, 0, 0), Vector4(0, 0, 0, 1)),

		projection * Projection(Vector4(1, 0, 0, 0), Vector4(0, 0, 1, 0), Vector4(0, -1, 0, 0), Vector4(0, 0, 0, 1))
	};

	static const Vector2 directions[4] = { Vector2(1, 0), Vector2(0, 1), Vector2(-1, 0), Vector2(0, -1) };
	shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES3::Z_FAR, p_far, shadow_render.shader_version, variant);

	for (int quadrant = 0; quadrant < 4; quadrant++) {
		const uint8_t quadrant_bit = uint8_t(1u << quadrant);
		if ((combined_quadrant_mask & quadrant_bit) == 0) {
			continue;
		}

		glViewport((state.shadow_texture_size / 4) * quadrant, shadow_band * 2, (state.shadow_texture_size / 4), 2);
		shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES3::PROJECTION, projections[quadrant], shadow_render.shader_version, variant);
		shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES3::DIRECTION, directions[quadrant].x, directions[quadrant].y, shadow_render.shader_version, variant);

		for (const ShadowOccluderEntry &entry : state.shadow_occluder_entries) {
			if ((entry.quadrant_mask & quadrant_bit) == 0) {
				continue;
			}

			const Transform2D &modelview = entry.modelview;
			shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES3::MODELVIEW1, modelview.columns[0][0], modelview.columns[1][0], 0, modelview.columns[2][0], shadow_render.shader_version, variant);
			shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES3::MODELVIEW2, modelview.columns[0][1], modelview.columns[1][1], 0, modelview.columns[2][1], shadow_render.shader_version, variant);

			_apply_shadow_cull_mode(entry.polygon->cull_mode);

			glBindVertexArray(entry.polygon->vertex_array);
			glDrawElements(GL_TRIANGLES, 3 * entry.polygon->line_point_count, GL_UNSIGNED_SHORT, nullptr);
		}
	}

	band.atlas_generation = state.shadow_atlas_generation;
	band.valid = true;
	band.is_directional = false;
	band.near_plane = p_near;
	band.far_plane = p_far;
	band.occluders = state.shadow_occluder_cache_scratch;

	if (owns_shadow_pass) {
		shadow_pass_end();
	}
}

void RasterizerCanvasGLES3::light_update_directional_shadow(RID p_rid, int p_shadow_index, const Transform2D &p_light_xform, int p_light_mask, float p_cull_distance, const Rect2 &p_clip_rect, LightOccluderInstance *p_occluders) {
	GLES3::Config *config = GLES3::Config::get_singleton();

	CanvasLight *cl = canvas_light_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(cl);
	ERR_FAIL_COND(!cl->shadow.enabled);
	ERR_FAIL_INDEX(p_shadow_index, (int)data.max_lights_per_render);

	const bool owns_shadow_pass = !state.shadow_pass_active;
	if (owns_shadow_pass) {
		shadow_pass_begin();
	}

	const int32_t shadow_band = _acquire_shadow_band(p_rid, cl);
	if (shadow_band < 0) {
		if (owns_shadow_pass) {
			shadow_pass_end();
		}
		return;
	}

	Vector2 light_dir = p_light_xform.columns[1].normalized();

	Vector2 center = p_clip_rect.get_center();

	float to_edge_distance = Math::abs(light_dir.dot(p_clip_rect.get_support(-light_dir)) - light_dir.dot(center));

	Vector2 from_pos = center - light_dir * (to_edge_distance + p_cull_distance);
	float distance = to_edge_distance * 2.0 + p_cull_distance;
	float half_size = p_clip_rect.size.length() * 0.5; //shadow length, must keep this no matter the angle

	cl->shadow.z_far = distance;
	cl->shadow.y_offset = float(shadow_band * 2 + 1) / float(data.max_lights_per_render * 2);

	Transform2D to_light_xform;

	to_light_xform[2] = from_pos;
	to_light_xform[1] = light_dir;
	to_light_xform[0] = -light_dir.orthogonal();

	to_light_xform.invert();

	Transform2D to_shadow;
	to_shadow.columns[0].x = 1.0 / -(half_size * 2.0);
	to_shadow.columns[2].x = 0.5;

	cl->shadow.directional_xform = to_shadow * to_light_xform;

	if (state.shadow_fb == 0) {
		if (owns_shadow_pass) {
			shadow_pass_end();
		}
		return;
	}

	state.shadow_occluder_entries.clear();
	state.shadow_occluder_cache_scratch.clear();
	for (LightOccluderInstance *instance = p_occluders; instance != nullptr; instance = instance->next) {
		OccluderPolygon *co = occluder_polygon_owner.get_or_null(instance->occluder);

		if (!co || co->vertex_array == 0 || !(p_light_mask & instance->light_mask)) {
			continue;
		}

		const Transform2D modelview = to_light_xform * instance->xform_cache;
		state.shadow_occluder_entries.push_back({ co, modelview, 1u });
		state.shadow_occluder_cache_scratch.push_back({ instance->occluder.get_id(), co->revision, reinterpret_cast<uintptr_t>(instance), modelview });
	}

	if (state.shadow_occluder_cache_scratch.size() > 1) {
		state.shadow_occluder_cache_scratch.sort();
	}

	ShadowBandCache &band = state.shadow_band_cache[shadow_band];
	if (band.valid &&
			band.is_directional &&
			band.atlas_generation == state.shadow_atlas_generation &&
			Math::abs(band.near_plane - half_size) <= 1e-3f &&
			Math::abs(band.far_plane - distance) <= 1e-3f &&
			shadow_occluder_lists_match(band.occluders, state.shadow_occluder_cache_scratch)) {
		cl->shadow.z_far = band.far_plane;
		if (owns_shadow_pass) {
			shadow_pass_end();
		}
		return;
	}

	_ensure_shadow_pass_bound();

	glViewport(0, shadow_band * 2, state.shadow_texture_size, 2);
	glScissor(0, shadow_band * 2, state.shadow_texture_size, 2);
	glClearColor(1.0, 1.0, 1.0, 1.0);
	RasterizerUtilGLES3::clear_depth(1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	if (!state.shadow_shader_bound) {
		band.valid = false;
		if (owns_shadow_pass) {
			shadow_pass_end();
		}
		return;
	}

	if (state.shadow_occluder_entries.is_empty()) {
		band.atlas_generation = state.shadow_atlas_generation;
		band.valid = true;
		band.is_directional = true;
		band.near_plane = half_size;
		band.far_plane = distance;
		band.occluders.clear();
		if (owns_shadow_pass) {
			shadow_pass_end();
		}
		return;
	}

	CanvasOcclusionShaderGLES3::ShaderVariant variant = config->float_texture_supported ? CanvasOcclusionShaderGLES3::MODE_SHADOW : CanvasOcclusionShaderGLES3::MODE_SHADOW_RGBA;

	Projection projection;
	projection.set_orthogonal(-half_size, half_size, -0.5, 0.5, 0.0, distance);
	projection = projection * Projection(Transform3D().looking_at(Vector3(0, 1, 0), Vector3(0, 0, -1)).affine_inverse());

	shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES3::PROJECTION, projection, shadow_render.shader_version, variant);
	shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES3::DIRECTION, 0.0, 1.0, shadow_render.shader_version, variant);
	shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES3::Z_FAR, distance, shadow_render.shader_version, variant);

	for (const ShadowOccluderEntry &entry : state.shadow_occluder_entries) {
		const Transform2D &modelview = entry.modelview;
		shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES3::MODELVIEW1, modelview.columns[0][0], modelview.columns[1][0], 0, modelview.columns[2][0], shadow_render.shader_version, variant);
		shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES3::MODELVIEW2, modelview.columns[0][1], modelview.columns[1][1], 0, modelview.columns[2][1], shadow_render.shader_version, variant);

		_apply_shadow_cull_mode(entry.polygon->cull_mode);

		glBindVertexArray(entry.polygon->vertex_array);
		glDrawElements(GL_TRIANGLES, 3 * entry.polygon->line_point_count, GL_UNSIGNED_SHORT, nullptr);
	}

	band.atlas_generation = state.shadow_atlas_generation;
	band.valid = true;
	band.is_directional = true;
	band.near_plane = half_size;
	band.far_plane = distance;
	band.occluders = state.shadow_occluder_cache_scratch;

	if (owns_shadow_pass) {
		shadow_pass_end();
	}
}

void RasterizerCanvasGLES3::_update_shadow_atlas() {
	GLES3::Config *config = GLES3::Config::get_singleton();

	if (state.shadow_fb == 0) {
		glActiveTexture(GL_TEXTURE0);

		glGenFramebuffers(1, &state.shadow_fb);
		glBindFramebuffer(GL_FRAMEBUFFER, state.shadow_fb);

		glGenRenderbuffers(1, &state.shadow_depth_buffer);
		glBindRenderbuffer(GL_RENDERBUFFER, state.shadow_depth_buffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, state.shadow_texture_size, data.max_lights_per_render * 2);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, state.shadow_depth_buffer);

		glGenTextures(1, &state.shadow_texture);
		glBindTexture(GL_TEXTURE_2D, state.shadow_texture);
		if (config->float_texture_supported) {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, state.shadow_texture_size, data.max_lights_per_render * 2, 0, GL_RED, GL_FLOAT, nullptr);
		} else {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, state.shadow_texture_size, data.max_lights_per_render * 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		}

		// The atlas stores occluder distances, not colors, so it must be point sampled:
		// blending two neighboring texels produces a depth that belongs to neither
		// occluder, and the four 90 degree faces packed across each row would bleed
		// into each other at the quadrant seams. The shadow filters do their own
		// multi-tap offsets by shadow_pixel_size, so hardware filtering only added a
		// second, unintended blur on top. This also keeps the GL_R32F atlas sampleable
		// where GL_OES_texture_float_linear is missing (Config::float_texture_linear_supported),
		// which otherwise leaves the texture incomplete and every lookup returning 0.
		// GL_TEXTURE_WRAP_S stays GL_REPEAT so that filter taps crossing a row edge
		// wrap onto the neighboring face, which is geometrically continuous; wrapping
		// in T instead aliases onto another light's band, so T clamps.
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, state.shadow_texture, 0);

		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			glDeleteFramebuffers(1, &state.shadow_fb);
			glDeleteTextures(1, &state.shadow_texture);
			glDeleteRenderbuffers(1, &state.shadow_depth_buffer);
			state.shadow_fb = 0;
			state.shadow_texture = 0;
			state.shadow_depth_buffer = 0;
			WARN_PRINT("Could not create CanvasItem shadow atlas, status: " + GLES3::TextureStorage::get_singleton()->get_framebuffer_error(status));
		} else {
			state.shadow_atlas_generation++;
			GLES3::Utilities::get_singleton()->texture_allocated_data(state.shadow_texture, state.shadow_texture_size * data.max_lights_per_render * 2 * 4, "2D shadow atlas texture");
		}
		glBindFramebuffer(GL_FRAMEBUFFER, GLES3::TextureStorage::system_fbo);
	}
}

void RasterizerCanvasGLES3::render_sdf(RID p_render_target, LightOccluderInstance *p_occluders) {
	GLES3::TextureStorage *texture_storage = GLES3::TextureStorage::get_singleton();

	GLuint fb = texture_storage->render_target_get_sdf_framebuffer(p_render_target);
	Rect2i rect = texture_storage->render_target_get_sdf_rect(p_render_target);

	Transform2D to_sdf;
	to_sdf.columns[0] *= rect.size.width;
	to_sdf.columns[1] *= rect.size.height;
	to_sdf.columns[2] = rect.position;

	Transform2D to_clip;
	to_clip.columns[0] *= 2.0;
	to_clip.columns[1] *= 2.0;
	to_clip.columns[2] = -Vector2(1.0, 1.0);

	to_clip = to_clip * to_sdf.affine_inverse();

	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glViewport(0, 0, rect.size.width, rect.size.height);

	glDepthMask(GL_FALSE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);

	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	CanvasOcclusionShaderGLES3::ShaderVariant variant = CanvasOcclusionShaderGLES3::MODE_SDF;
	bool success = shadow_render.shader.version_bind_shader(shadow_render.shader_version, variant);
	if (!success) {
		return;
	}

	shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES3::PROJECTION, Projection(), shadow_render.shader_version, variant);
	shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES3::DIRECTION, 0.0, 0.0, shadow_render.shader_version, variant);
	shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES3::Z_FAR, 0.0, shadow_render.shader_version, variant);

	LightOccluderInstance *instance = p_occluders;

	while (instance) {
		OccluderPolygon *oc = occluder_polygon_owner.get_or_null(instance->occluder);

		if (!oc || oc->sdf_vertex_array == 0 || !instance->sdf_collision) {
			instance = instance->next;
			continue;
		}

		Transform2D modelview = to_clip * instance->xform_cache;
		shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES3::MODELVIEW1, modelview.columns[0][0], modelview.columns[1][0], 0, modelview.columns[2][0], shadow_render.shader_version, variant);
		shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES3::MODELVIEW2, modelview.columns[0][1], modelview.columns[1][1], 0, modelview.columns[2][1], shadow_render.shader_version, variant);

		glBindVertexArray(oc->sdf_vertex_array);
		glDrawElements(oc->sdf_is_lines ? GL_LINES : GL_TRIANGLES, oc->sdf_index_count, GL_UNSIGNED_INT, nullptr);

		instance = instance->next;
	}

	texture_storage->render_target_sdf_process(p_render_target); //done rendering, process it
	glBindVertexArray(0);
	glBindFramebuffer(GL_FRAMEBUFFER, GLES3::TextureStorage::system_fbo);
}

RID RasterizerCanvasGLES3::occluder_polygon_create() {
	OccluderPolygon occluder;

	return occluder_polygon_owner.make_rid(occluder);
}

void RasterizerCanvasGLES3::occluder_polygon_set_shape(RID p_occluder, const Vector<Vector2> &p_points, bool p_closed) {
	OccluderPolygon *oc = occluder_polygon_owner.get_or_null(p_occluder);
	ERR_FAIL_NULL(oc);
	oc->revision++;

	Vector<Vector2> lines;

	if (p_points.size()) {
		int lc = p_points.size() * 2;

		lines.resize(lc - (p_closed ? 0 : 2));
		{
			Vector2 *w = lines.ptrw();
			const Vector2 *r = p_points.ptr();

			int max = lc / 2;
			if (!p_closed) {
				max--;
			}
			for (int i = 0; i < max; i++) {
				Vector2 a = r[i];
				Vector2 b = r[(i + 1) % (lc / 2)];
				w[i * 2 + 0] = a;
				w[i * 2 + 1] = b;
			}
		}
	}

	if (oc->line_point_count != lines.size() && oc->vertex_array != 0) {
		glDeleteVertexArrays(1, &oc->vertex_array);
		GLES3::Utilities::get_singleton()->buffer_free_data(oc->vertex_buffer);
		GLES3::Utilities::get_singleton()->buffer_free_data(oc->index_buffer);

		oc->vertex_array = 0;
		oc->vertex_buffer = 0;
		oc->index_buffer = 0;
	}

	if (lines.size()) {
		Vector<uint8_t> geometry;
		Vector<uint8_t> indices;
		int lc = lines.size();

		geometry.resize(lc * 6 * sizeof(float));
		indices.resize(lc * 3 * sizeof(uint16_t));

		{
			uint8_t *vw = geometry.ptrw();
			float *vwptr = reinterpret_cast<float *>(vw);
			uint8_t *iw = indices.ptrw();
			uint16_t *iwptr = (uint16_t *)iw;

			const Vector2 *lr = lines.ptr();

			const int POLY_HEIGHT = 16384;

			for (int i = 0; i < lc / 2; i++) {
				vwptr[i * 12 + 0] = lr[i * 2 + 0].x;
				vwptr[i * 12 + 1] = lr[i * 2 + 0].y;
				vwptr[i * 12 + 2] = POLY_HEIGHT;

				vwptr[i * 12 + 3] = lr[i * 2 + 1].x;
				vwptr[i * 12 + 4] = lr[i * 2 + 1].y;
				vwptr[i * 12 + 5] = POLY_HEIGHT;

				vwptr[i * 12 + 6] = lr[i * 2 + 1].x;
				vwptr[i * 12 + 7] = lr[i * 2 + 1].y;
				vwptr[i * 12 + 8] = -POLY_HEIGHT;

				vwptr[i * 12 + 9] = lr[i * 2 + 0].x;
				vwptr[i * 12 + 10] = lr[i * 2 + 0].y;
				vwptr[i * 12 + 11] = -POLY_HEIGHT;

				iwptr[i * 6 + 0] = i * 4 + 0;
				iwptr[i * 6 + 1] = i * 4 + 1;
				iwptr[i * 6 + 2] = i * 4 + 2;

				iwptr[i * 6 + 3] = i * 4 + 2;
				iwptr[i * 6 + 4] = i * 4 + 3;
				iwptr[i * 6 + 5] = i * 4 + 0;
			}
		}

		if (oc->vertex_array == 0) {
			oc->line_point_count = lc;
			glGenVertexArrays(1, &oc->vertex_array);
			glBindVertexArray(oc->vertex_array);
			glGenBuffers(1, &oc->vertex_buffer);
			glBindBuffer(GL_ARRAY_BUFFER, oc->vertex_buffer);

			GLES3::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, oc->vertex_buffer, lc * 6 * sizeof(float), geometry.ptr(), GL_STATIC_DRAW, "Occluder polygon vertex buffer");

			glEnableVertexAttribArray(RSE::ARRAY_VERTEX);
			glVertexAttribPointer(RSE::ARRAY_VERTEX, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

			glGenBuffers(1, &oc->index_buffer);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, oc->index_buffer);
			GLES3::Utilities::get_singleton()->buffer_allocate_data(GL_ELEMENT_ARRAY_BUFFER, oc->index_buffer, 3 * lc * sizeof(uint16_t), indices.ptr(), GL_STATIC_DRAW, "Occluder polygon index buffer");

			glBindVertexArray(0);
		} else {
			glBindBuffer(GL_ARRAY_BUFFER, oc->vertex_buffer);
			glBufferData(GL_ARRAY_BUFFER, lc * 6 * sizeof(float), geometry.ptr(), GL_STATIC_DRAW);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, oc->index_buffer);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, 3 * lc * sizeof(uint16_t), indices.ptr(), GL_STATIC_DRAW);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}
	}

	// sdf

	Vector<int> sdf_indices;

	if (p_points.size()) {
		if (p_closed) {
			sdf_indices = Geometry2D::triangulate_polygon(p_points);
			oc->sdf_is_lines = false;
		} else {
			int max = p_points.size();
			sdf_indices.resize(max * 2);

			int *iw = sdf_indices.ptrw();
			for (int i = 0; i < max; i++) {
				iw[i * 2 + 0] = i;
				iw[i * 2 + 1] = (i + 1) % max;
			}
			oc->sdf_is_lines = true;
		}
	}

	if (oc->sdf_index_count != sdf_indices.size() && oc->sdf_point_count != p_points.size() && oc->sdf_vertex_array != 0) {
		glDeleteVertexArrays(1, &oc->sdf_vertex_array);
		GLES3::Utilities::get_singleton()->buffer_free_data(oc->sdf_vertex_buffer);
		GLES3::Utilities::get_singleton()->buffer_free_data(oc->sdf_index_buffer);

		oc->sdf_vertex_array = 0;
		oc->sdf_vertex_buffer = 0;
		oc->sdf_index_buffer = 0;

		oc->sdf_index_count = sdf_indices.size();
		oc->sdf_point_count = p_points.size();
	}

	if (sdf_indices.size()) {
		if (oc->sdf_vertex_array == 0) {
			oc->sdf_index_count = sdf_indices.size();
			oc->sdf_point_count = p_points.size();
			glGenVertexArrays(1, &oc->sdf_vertex_array);
			glBindVertexArray(oc->sdf_vertex_array);
			glGenBuffers(1, &oc->sdf_vertex_buffer);
			glBindBuffer(GL_ARRAY_BUFFER, oc->sdf_vertex_buffer);

			GLES3::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, oc->sdf_vertex_buffer, oc->sdf_point_count * 2 * sizeof(float), p_points.ptr(), GL_STATIC_DRAW, "Occluder polygon SDF vertex buffer");

			glEnableVertexAttribArray(RSE::ARRAY_VERTEX);
			glVertexAttribPointer(RSE::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

			glGenBuffers(1, &oc->sdf_index_buffer);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, oc->sdf_index_buffer);
			GLES3::Utilities::get_singleton()->buffer_allocate_data(GL_ELEMENT_ARRAY_BUFFER, oc->sdf_index_buffer, oc->sdf_index_count * sizeof(uint32_t), sdf_indices.ptr(), GL_STATIC_DRAW, "Occluder polygon SDF index buffer");

			glBindVertexArray(0);
		} else {
			glBindBuffer(GL_ARRAY_BUFFER, oc->sdf_vertex_buffer);
			glBufferData(GL_ARRAY_BUFFER, p_points.size() * 2 * sizeof(float), p_points.ptr(), GL_STATIC_DRAW);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, oc->sdf_index_buffer);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, sdf_indices.size() * sizeof(uint32_t), sdf_indices.ptr(), GL_STATIC_DRAW);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}
	}
}

void RasterizerCanvasGLES3::occluder_polygon_set_cull_mode(RID p_occluder, RSE::CanvasOccluderPolygonCullMode p_mode) {
	OccluderPolygon *oc = occluder_polygon_owner.get_or_null(p_occluder);
	ERR_FAIL_NULL(oc);
	if (oc->cull_mode != p_mode) {
		oc->cull_mode = p_mode;
		oc->revision++;
	}
}

void RasterizerCanvasGLES3::set_shadow_texture_size(int p_size) {
	GLES3::Config *config = GLES3::Config::get_singleton();
	p_size = Math::nearest_power_of_2_templated(p_size);

	if (p_size > config->max_texture_size) {
		p_size = config->max_texture_size;
		WARN_PRINT("Attempting to set CanvasItem shadow atlas size to " + itos(p_size) + " which is beyond limit of " + itos(config->max_texture_size) + "supported by hardware.");
	}

	if (p_size == state.shadow_texture_size) {
		return;
	}
	state.shadow_texture_size = p_size;

	if (state.shadow_fb != 0) {
		glDeleteFramebuffers(1, &state.shadow_fb);
		GLES3::Utilities::get_singleton()->texture_free_data(state.shadow_texture);
		glDeleteRenderbuffers(1, &state.shadow_depth_buffer);
		state.shadow_fb = 0;
		state.shadow_texture = 0;
		state.shadow_depth_buffer = 0;
		state.shadow_fb_bound = false;
		state.shadow_shader_bound = false;
		state.shadow_atlas_generation++;
	}
	_update_shadow_atlas();
}

bool RasterizerCanvasGLES3::free(RID p_rid) {
	if (canvas_light_owner.owns(p_rid)) {
		CanvasLight *cl = canvas_light_owner.get_or_null(p_rid);
		ERR_FAIL_NULL_V(cl, false);
		_release_shadow_band(p_rid, cl);
		canvas_light_owner.free(p_rid);
	} else if (occluder_polygon_owner.owns(p_rid)) {
		occluder_polygon_set_shape(p_rid, Vector<Vector2>(), false);
		occluder_polygon_owner.free(p_rid);
	} else {
		return false;
	}

	return true;
}

void RasterizerCanvasGLES3::update() {
}

void RasterizerCanvasGLES3::canvas_begin(RID p_to_render_target, bool p_to_backbuffer, bool p_backbuffer_has_mipmaps) {
	GLES3::TextureStorage *texture_storage = GLES3::TextureStorage::get_singleton();
	GLES3::Config *config = GLES3::Config::get_singleton();

	GLES3::RenderTarget *render_target = texture_storage->get_render_target(p_to_render_target);

	if (p_to_backbuffer) {
		glBindFramebuffer(GL_FRAMEBUFFER, render_target->backbuffer_fbo);
		glActiveTexture(GL_TEXTURE0 + config->max_texture_image_units - 4);
		GLES3::Texture *tex = texture_storage->get_texture(texture_storage->texture_gl_get_default(GLES3::DEFAULT_GL_TEXTURE_WHITE));
		glBindTexture(GL_TEXTURE_2D, tex->tex_id);
	} else {
		glBindFramebuffer(GL_FRAMEBUFFER, render_target->fbo);
		glActiveTexture(GL_TEXTURE0 + config->max_texture_image_units - 4);
		glBindTexture(GL_TEXTURE_2D, render_target->backbuffer);
		if (render_target->backbuffer != 0) {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, p_backbuffer_has_mipmaps ? render_target->mipmap_count - 1 : 0);
		}
	}

	if (render_target->is_transparent || p_to_backbuffer) {
		state.transparent_render_target = true;
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	} else {
		state.transparent_render_target = false;
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
	}

	if (render_target && render_target->clear_requested) {
		const Color &col = render_target->clear_color;
		glClearColor(col.r, col.g, col.b, render_target->is_transparent ? col.a : 1.0f);

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		render_target->clear_requested = false;
	}

	glActiveTexture(GL_TEXTURE0);
	GLES3::Texture *tex = texture_storage->get_texture(texture_storage->texture_gl_get_default(GLES3::DEFAULT_GL_TEXTURE_WHITE));
	glBindTexture(GL_TEXTURE_2D, tex->tex_id);
}

void RasterizerCanvasGLES3::_bind_canvas_texture(RID p_texture, RSE::CanvasItemTextureFilter p_base_filter, RSE::CanvasItemTextureRepeat p_base_repeat) {
	GLES3::TextureStorage *texture_storage = GLES3::TextureStorage::get_singleton();
	GLES3::Config *config = GLES3::Config::get_singleton();

	if (p_texture == RID()) {
		p_texture = default_canvas_texture;
	}

	if (state.current_tex == p_texture && state.current_filter_mode == p_base_filter && state.current_repeat_mode == p_base_repeat) {
		return;
	}

	state.current_tex = p_texture;
	state.current_filter_mode = p_base_filter;
	state.current_repeat_mode = p_base_repeat;

	GLES3::CanvasTexture *ct = nullptr;

	GLES3::Texture *t = texture_storage->get_texture(p_texture);

	if (t) {
		ERR_FAIL_NULL(t->canvas_texture);
		ct = t->canvas_texture;
		if (t->render_target) {
			t->render_target->used_in_frame = true;
		}
	} else {
		ct = texture_storage->get_canvas_texture(p_texture);
	}

	if (!ct) {
		// Invalid Texture RID.
		_bind_canvas_texture(default_canvas_texture, p_base_filter, p_base_repeat);
		return;
	}

	RSE::CanvasItemTextureFilter filter = ct->texture_filter != RSE::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT ? ct->texture_filter : p_base_filter;
	ERR_FAIL_COND(filter == RSE::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT);

	RSE::CanvasItemTextureRepeat repeat = ct->texture_repeat != RSE::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT ? ct->texture_repeat : p_base_repeat;
	ERR_FAIL_COND(repeat == RSE::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT);

	GLES3::Texture *texture = texture_storage->get_texture(ct->diffuse);

	if (!texture) {
		GLES3::Texture *tex = texture_storage->get_texture(texture_storage->texture_gl_get_default(GLES3::DEFAULT_GL_TEXTURE_WHITE));
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex->tex_id);
	} else {
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture->tex_id);
		texture->gl_set_filter(filter);
		texture->gl_set_repeat(repeat);
		if (texture->render_target) {
			texture->render_target->used_in_frame = true;
		}
	}

	GLES3::Texture *normal_map = texture_storage->get_texture(ct->normal_map);

	if (!normal_map) {
		GLES3::Texture *tex = texture_storage->get_texture(texture_storage->texture_gl_get_default(GLES3::DEFAULT_GL_TEXTURE_NORMAL));
		if (state.current_normal_texture_id != tex->tex_id) {
			glActiveTexture(GL_TEXTURE0 + config->max_texture_image_units - 6);
			glBindTexture(GL_TEXTURE_2D, tex->tex_id);
			state.current_normal_texture_id = tex->tex_id;
		}
	} else {
		glActiveTexture(GL_TEXTURE0 + config->max_texture_image_units - 6);
		glBindTexture(GL_TEXTURE_2D, normal_map->tex_id);
		state.current_normal_texture_id = normal_map->tex_id;
		normal_map->gl_set_filter(filter);
		normal_map->gl_set_repeat(repeat);
		if (normal_map->render_target) {
			normal_map->render_target->used_in_frame = true;
		}
	}

	GLES3::Texture *specular_map = texture_storage->get_texture(ct->specular);

	if (!specular_map) {
		GLES3::Texture *tex = texture_storage->get_texture(texture_storage->texture_gl_get_default(GLES3::DEFAULT_GL_TEXTURE_WHITE));
		if (state.current_specular_texture_id != tex->tex_id) {
			glActiveTexture(GL_TEXTURE0 + config->max_texture_image_units - 7);
			glBindTexture(GL_TEXTURE_2D, tex->tex_id);
			state.current_specular_texture_id = tex->tex_id;
		}
	} else {
		glActiveTexture(GL_TEXTURE0 + config->max_texture_image_units - 7);
		glBindTexture(GL_TEXTURE_2D, specular_map->tex_id);
		state.current_specular_texture_id = specular_map->tex_id;
		specular_map->gl_set_filter(filter);
		specular_map->gl_set_repeat(repeat);
		if (specular_map->render_target) {
			specular_map->render_target->used_in_frame = true;
		}
	}
}

void RasterizerCanvasGLES3::_prepare_canvas_texture(RID p_texture, RSE::CanvasItemTextureFilter p_base_filter, RSE::CanvasItemTextureRepeat p_base_repeat, uint32_t &r_index, Size2 &r_texpixel_size) {
	GLES3::TextureStorage *texture_storage = GLES3::TextureStorage::get_singleton();

	if (p_texture == RID()) {
		p_texture = default_canvas_texture;
	}

	GLES3::CanvasTexture *ct = nullptr;

	GLES3::Texture *t = texture_storage->get_texture(p_texture);

	if (t) {
		//regular texture
		if (!t->canvas_texture) {
			t->canvas_texture = memnew(GLES3::CanvasTexture);
			t->canvas_texture->diffuse = p_texture;
		}

		ct = t->canvas_texture;
	} else {
		ct = texture_storage->get_canvas_texture(p_texture);
	}

	if (!ct) {
		// Invalid Texture RID.
		_prepare_canvas_texture(default_canvas_texture, p_base_filter, p_base_repeat, r_index, r_texpixel_size);
		return;
	}

	GLES3::Texture *texture = texture_storage->get_texture(ct->diffuse);
	Size2i size_cache;

	// Cache default white resource ID.
	const RID default_texture_id = texture_storage->texture_gl_get_default(GLES3::DEFAULT_GL_TEXTURE_WHITE);

	// If no texture is assigned, assign default white.
	if (!texture) {
		ct->diffuse = default_texture_id;
	}

	// Enforce a 1x1 size if default white texture.
	size_cache = ct->diffuse == default_texture_id ? Size2i(1, 1) : Size2i(texture->width, texture->height);

	GLES3::Texture *normal_map = texture_storage->get_texture(ct->normal_map);

	if (ct->specular_color.a < 0.999) {
		state.canvas_instance_batches[state.current_batch_index].flags |= BATCH_FLAGS_DEFAULT_SPECULAR_MAP_USED;
	} else {
		state.canvas_instance_batches[state.current_batch_index].flags &= ~BATCH_FLAGS_DEFAULT_SPECULAR_MAP_USED;
	}

	if (normal_map) {
		state.canvas_instance_batches[state.current_batch_index].flags |= BATCH_FLAGS_DEFAULT_NORMAL_MAP_USED;
	} else {
		state.canvas_instance_batches[state.current_batch_index].flags &= ~BATCH_FLAGS_DEFAULT_NORMAL_MAP_USED;
	}

	state.canvas_instance_batches[state.current_batch_index].specular_shininess = uint32_t(CLAMP(ct->specular_color.a * 255.0, 0, 255)) << 24;
	state.canvas_instance_batches[state.current_batch_index].specular_shininess |= uint32_t(CLAMP(ct->specular_color.b * 255.0, 0, 255)) << 16;
	state.canvas_instance_batches[state.current_batch_index].specular_shininess |= uint32_t(CLAMP(ct->specular_color.g * 255.0, 0, 255)) << 8;
	state.canvas_instance_batches[state.current_batch_index].specular_shininess |= uint32_t(CLAMP(ct->specular_color.r * 255.0, 0, 255));

	r_texpixel_size.x = 1.0 / float(size_cache.x);
	r_texpixel_size.y = 1.0 / float(size_cache.y);

	state.instance_data_array[r_index].color_texture_pixel_size[0] = r_texpixel_size.x;
	state.instance_data_array[r_index].color_texture_pixel_size[1] = r_texpixel_size.y;
}

void RasterizerCanvasGLES3::reset_canvas() {
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);

	glActiveTexture(GL_TEXTURE0 + GLES3::Config::get_singleton()->max_texture_image_units - 2);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0 + GLES3::Config::get_singleton()->max_texture_image_units - 3);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void RasterizerCanvasGLES3::draw_lens_distortion_rect(const Rect2 &p_rect, float p_k1, float p_k2, const Vector2 &p_eye_center, float p_oversample) {
}

RendererCanvasRender::PolygonID RasterizerCanvasGLES3::request_polygon(const Vector<int> &p_indices, const Vector<Point2> &p_points, const Vector<Color> &p_colors, const Vector<Point2> &p_uvs, const Vector<int> &p_bones, const Vector<float> &p_weights, int p_count) {
	// We interleave the vertex data into one big VBO to improve cache coherence
	uint32_t vertex_count = p_points.size();
	uint32_t stride = 2;
	if ((uint32_t)p_colors.size() == vertex_count) {
		stride += 4;
	}
	if ((uint32_t)p_uvs.size() == vertex_count) {
		stride += 2;
	}
	if ((uint32_t)p_bones.size() == vertex_count * 4 && (uint32_t)p_weights.size() == vertex_count * 4) {
		stride += 4;
	}

	PolygonBuffers pb;
	glGenBuffers(1, &pb.vertex_buffer);
	glGenVertexArrays(1, &pb.vertex_array);
	glBindVertexArray(pb.vertex_array);
	pb.count = vertex_count;
	pb.index_buffer = 0;

	uint32_t buffer_size = stride * p_points.size();

	Vector<uint8_t> polygon_buffer;
	polygon_buffer.resize(buffer_size * sizeof(float));
	{
		glBindBuffer(GL_ARRAY_BUFFER, pb.vertex_buffer);
		uint8_t *r = polygon_buffer.ptrw();
		float *fptr = reinterpret_cast<float *>(r);
		uint32_t *uptr = (uint32_t *)r;
		uint32_t base_offset = 0;
		{
			// Always uses vertex positions
			glEnableVertexAttribArray(RSE::ARRAY_VERTEX);
			glVertexAttribPointer(RSE::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, stride * sizeof(float), nullptr);
			const Vector2 *points_ptr = p_points.ptr();

			for (uint32_t i = 0; i < vertex_count; i++) {
				fptr[base_offset + i * stride + 0] = points_ptr[i].x;
				fptr[base_offset + i * stride + 1] = points_ptr[i].y;
			}

			base_offset += 2;
		}

		// Next add colors
		if ((uint32_t)p_colors.size() == vertex_count) {
			glEnableVertexAttribArray(RSE::ARRAY_COLOR);
			glVertexAttribPointer(RSE::ARRAY_COLOR, 4, GL_FLOAT, GL_FALSE, stride * sizeof(float), CAST_INT_TO_UCHAR_PTR(base_offset * sizeof(float)));

			const Color *color_ptr = p_colors.ptr();

			for (uint32_t i = 0; i < vertex_count; i++) {
				fptr[base_offset + i * stride + 0] = color_ptr[i].r;
				fptr[base_offset + i * stride + 1] = color_ptr[i].g;
				fptr[base_offset + i * stride + 2] = color_ptr[i].b;
				fptr[base_offset + i * stride + 3] = color_ptr[i].a;
			}
			base_offset += 4;
		} else {
			glDisableVertexAttribArray(RSE::ARRAY_COLOR);
			pb.color_disabled = true;
			pb.color = p_colors.size() == 1 ? p_colors[0] : Color(1.0, 1.0, 1.0, 1.0);
		}

		if ((uint32_t)p_uvs.size() == vertex_count) {
			glEnableVertexAttribArray(RSE::ARRAY_TEX_UV);
			glVertexAttribPointer(RSE::ARRAY_TEX_UV, 2, GL_FLOAT, GL_FALSE, stride * sizeof(float), CAST_INT_TO_UCHAR_PTR(base_offset * sizeof(float)));

			const Vector2 *uv_ptr = p_uvs.ptr();

			for (uint32_t i = 0; i < vertex_count; i++) {
				fptr[base_offset + i * stride + 0] = uv_ptr[i].x;
				fptr[base_offset + i * stride + 1] = uv_ptr[i].y;
			}

			base_offset += 2;
		} else {
			glDisableVertexAttribArray(RSE::ARRAY_TEX_UV);
		}

		if ((uint32_t)p_indices.size() == vertex_count * 4 && (uint32_t)p_weights.size() == vertex_count * 4) {
			glEnableVertexAttribArray(RSE::ARRAY_BONES);
			glVertexAttribPointer(RSE::ARRAY_BONES, 4, GL_UNSIGNED_INT, GL_FALSE, stride * sizeof(float), CAST_INT_TO_UCHAR_PTR(base_offset * sizeof(float)));

			const int *bone_ptr = p_bones.ptr();

			for (uint32_t i = 0; i < vertex_count; i++) {
				uint16_t *bone16w = (uint16_t *)&uptr[base_offset + i * stride];

				bone16w[0] = bone_ptr[i * 4 + 0];
				bone16w[1] = bone_ptr[i * 4 + 1];
				bone16w[2] = bone_ptr[i * 4 + 2];
				bone16w[3] = bone_ptr[i * 4 + 3];
			}

			base_offset += 2;
		} else {
			glDisableVertexAttribArray(RSE::ARRAY_BONES);
		}

		if ((uint32_t)p_weights.size() == vertex_count * 4) {
			glEnableVertexAttribArray(RSE::ARRAY_WEIGHTS);
			glVertexAttribPointer(RSE::ARRAY_WEIGHTS, 4, GL_FLOAT, GL_FALSE, stride * sizeof(float), CAST_INT_TO_UCHAR_PTR(base_offset * sizeof(float)));

			const float *weight_ptr = p_weights.ptr();

			for (uint32_t i = 0; i < vertex_count; i++) {
				uint16_t *weight16w = (uint16_t *)&uptr[base_offset + i * stride];

				weight16w[0] = CLAMP(weight_ptr[i * 4 + 0] * 65535, 0, 65535);
				weight16w[1] = CLAMP(weight_ptr[i * 4 + 1] * 65535, 0, 65535);
				weight16w[2] = CLAMP(weight_ptr[i * 4 + 2] * 65535, 0, 65535);
				weight16w[3] = CLAMP(weight_ptr[i * 4 + 3] * 65535, 0, 65535);
			}

			base_offset += 2;
		} else {
			glDisableVertexAttribArray(RSE::ARRAY_WEIGHTS);
		}

		ERR_FAIL_COND_V(base_offset != stride, 0);
		GLES3::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, pb.vertex_buffer, vertex_count * stride * sizeof(float), polygon_buffer.ptr(), GL_STATIC_DRAW, "Polygon 2D vertex buffer");
	}

	if (p_indices.size()) {
		//create indices, as indices were requested
		Vector<uint8_t> index_buffer;
		index_buffer.resize(p_count * sizeof(int32_t));
		{
			uint8_t *w = index_buffer.ptrw();
			memcpy(w, p_indices.ptr(), sizeof(int32_t) * p_count);
		}
		glGenBuffers(1, &pb.index_buffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pb.index_buffer);
		GLES3::Utilities::get_singleton()->buffer_allocate_data(GL_ELEMENT_ARRAY_BUFFER, pb.index_buffer, p_count * 4, index_buffer.ptr(), GL_STATIC_DRAW, "Polygon 2D index buffer");
		pb.count = p_count;
	}

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	PolygonID id = polygon_buffers.last_id++;

	polygon_buffers.polygons[id] = pb;

	return id;
}

void RasterizerCanvasGLES3::free_polygon(PolygonID p_polygon) {
	PolygonBuffers *pb_ptr = polygon_buffers.polygons.getptr(p_polygon);
	ERR_FAIL_NULL(pb_ptr);

	PolygonBuffers &pb = *pb_ptr;

	if (pb.index_buffer != 0) {
		GLES3::Utilities::get_singleton()->buffer_free_data(pb.index_buffer);
	}

	glDeleteVertexArrays(1, &pb.vertex_array);
	GLES3::Utilities::get_singleton()->buffer_free_data(pb.vertex_buffer);

	polygon_buffers.polygons.erase(p_polygon);
}

// Creates a new uniform buffer and uses it right away
// This expands the instance buffer continually
// In theory allocations can reach as high as number of windows * 3 frames
// because OpenGL can start rendering subsequent frames before finishing the current one
void RasterizerCanvasGLES3::_allocate_instance_data_buffer() {
	GLuint new_buffers[3];
	glGenBuffers(3, new_buffers);
	// Batch UBO.
	glBindBuffer(GL_ARRAY_BUFFER, new_buffers[0]);
	GLES3::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, new_buffers[0], data.max_instance_buffer_size, nullptr, GL_STREAM_DRAW, "2D Batch UBO[" + itos(state.current_data_buffer_index) + "][0]");
	// Light uniform buffer.
	glBindBuffer(GL_UNIFORM_BUFFER, new_buffers[1]);
	GLES3::Utilities::get_singleton()->buffer_allocate_data(GL_UNIFORM_BUFFER, new_buffers[1], sizeof(LightUniform) * data.max_lights_per_render, nullptr, GL_STREAM_DRAW, "2D Lights UBO[" + itos(state.current_data_buffer_index) + "]");
	// State buffer.
	glBindBuffer(GL_UNIFORM_BUFFER, new_buffers[2]);
	GLES3::Utilities::get_singleton()->buffer_allocate_data(GL_UNIFORM_BUFFER, new_buffers[2], sizeof(StateBuffer), nullptr, GL_STREAM_DRAW, "2D State UBO[" + itos(state.current_data_buffer_index) + "]");

	state.current_data_buffer_index = (state.current_data_buffer_index + 1);
	DataBuffer db;
	db.instance_buffers.push_back(new_buffers[0]);
	db.light_ubo = new_buffers[1];
	db.state_ubo = new_buffers[2];
	db.last_frame_used = RSG::rasterizer->get_frame_number();
	state.canvas_instance_data_buffers.insert(state.current_data_buffer_index, db);
	state.current_data_buffer_index = state.current_data_buffer_index % state.canvas_instance_data_buffers.size();
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}
void RasterizerCanvasGLES3::_allocate_instance_buffer() {
	state.current_instance_buffer_index++;

	if (int(state.current_instance_buffer_index) < state.canvas_instance_data_buffers[state.current_data_buffer_index].instance_buffers.size()) {
		// We already allocated another buffer in a previous frame, so we can just use it.
		return;
	}

	GLuint new_buffer;
	glGenBuffers(1, &new_buffer);

	glBindBuffer(GL_ARRAY_BUFFER, new_buffer);
	GLES3::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, new_buffer, data.max_instance_buffer_size, nullptr, GL_STREAM_DRAW, "Batch UBO[" + itos(state.current_data_buffer_index) + "][" + itos(state.canvas_instance_data_buffers[state.current_data_buffer_index].instance_buffers.size()) + "]");

	state.canvas_instance_data_buffers[state.current_data_buffer_index].instance_buffers.push_back(new_buffer);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void RasterizerCanvasGLES3::_poll_frame_fences() {
	uint32_t remove_count = 0;
	while (remove_count < state.frame_fences.size()) {
		const FrameFence &frame_fence = state.frame_fences[remove_count];
		GLint sync_status = GL_UNSIGNALED;
		glGetSynciv(frame_fence.fence, GL_SYNC_STATUS, 1, nullptr, &sync_status);
		if (sync_status != GL_SIGNALED) {
			break;
		}
		if (frame_fence.epoch > state.completed_fence_epoch) {
			state.completed_fence_epoch = frame_fence.epoch;
		}
		glDeleteSync(frame_fence.fence);
		remove_count++;
	}
	if (remove_count > 0) {
		const uint32_t remaining = state.frame_fences.size() - remove_count;
		for (uint32_t index = 0; index < remaining; index++) {
			state.frame_fences[index] = state.frame_fences[index + remove_count];
		}
		state.frame_fences.resize(remaining);
	}
}

void RasterizerCanvasGLES3::_wait_for_fence_epoch(uint64_t p_target_epoch) {
	uint32_t remove_count = 0;
	while (remove_count < state.frame_fences.size()) {
		const FrameFence &frame_fence = state.frame_fences[remove_count];
		if (frame_fence.epoch > p_target_epoch) {
			break;
		}
#ifndef WEB_ENABLED
		// On web, glBufferSubData synchronizes implicitly and WebGL dislikes blocking waits.
		// Wait on every fence up to p_target_epoch (signaled fences return immediately) so
		// multi-context frames cannot advance completed_fence_epoch past an unsignaled fence.
		glClientWaitSync(frame_fence.fence, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000); // Wait for up to 100ms.
#endif
		if (frame_fence.epoch > state.completed_fence_epoch) {
			state.completed_fence_epoch = frame_fence.epoch;
		}
		glDeleteSync(frame_fence.fence);
		remove_count++;
	}
	if (remove_count > 0) {
		const uint32_t remaining = state.frame_fences.size() - remove_count;
		for (uint32_t index = 0; index < remaining; index++) {
			state.frame_fences[index] = state.frame_fences[index + remove_count];
		}
		state.frame_fences.resize(remaining);
	}
	if (p_target_epoch > state.completed_fence_epoch) {
		state.completed_fence_epoch = p_target_epoch;
	}
}

void RasterizerCanvasGLES3::flush_frame_fence() {
	if (!state.canvas_used_in_frame) {
		return;
	}
	state.canvas_used_in_frame = false;

	if (state.frame_fences.size() >= 8) {
		_poll_frame_fences();
	}

	const uint64_t epoch = state.next_fence_epoch++;
	GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	if (sync != GLsync()) {
		state.frame_fences.push_back({ epoch, sync });
	} else if (epoch > state.completed_fence_epoch) {
		state.completed_fence_epoch = epoch;
	}
}

void RasterizerCanvasGLES3::_acquire_instance_data_buffer() {
	const uint64_t current_frame = RSG::rasterizer->get_frame_number();
	if (state.last_polled_frame != current_frame) {
		state.last_polled_frame = current_frame;
		_poll_frame_fences();
	}

	DataBuffer &current_buffer = state.canvas_instance_data_buffers[state.current_data_buffer_index];
	if (current_buffer.last_fence_epoch > state.completed_fence_epoch) {
		if (current_buffer.last_fence_epoch == state.next_fence_epoch) {
			// Already used in the current un-fenced epoch; allocate a fresh DataBuffer.
			_allocate_instance_data_buffer();
		} else {
			_poll_frame_fences();
			if (current_buffer.last_fence_epoch > state.completed_fence_epoch) {
				if (current_frame >= 2 && current_buffer.last_frame_used < current_frame - 2) {
					_wait_for_fence_epoch(current_buffer.last_fence_epoch);
				} else {
					_allocate_instance_data_buffer();
				}
			}
		}
	}

	DataBuffer &acquired_buffer = state.canvas_instance_data_buffers[state.current_data_buffer_index];
	acquired_buffer.last_frame_used = current_frame;
	acquired_buffer.last_fence_epoch = state.next_fence_epoch;
	state.canvas_used_in_frame = true;
}

void RasterizerCanvasGLES3::set_time(double p_time) {
	state.time = p_time;
}

RasterizerCanvasGLES3 *RasterizerCanvasGLES3::singleton = nullptr;

RasterizerCanvasGLES3 *RasterizerCanvasGLES3::get_singleton() {
	return singleton;
}

RasterizerCanvasGLES3::RasterizerCanvasGLES3() {
	singleton = this;
	GLES3::TextureStorage *texture_storage = GLES3::TextureStorage::get_singleton();
	GLES3::MaterialStorage *material_storage = GLES3::MaterialStorage::get_singleton();
	GLES3::Config *config = GLES3::Config::get_singleton();

	glVertexAttrib4f(RSE::ARRAY_COLOR, 1.0, 1.0, 1.0, 1.0);

	polygon_buffers.last_id = 1;
	// quad buffer
	{
		glGenBuffers(1, &data.canvas_quad_vertices);
		glBindBuffer(GL_ARRAY_BUFFER, data.canvas_quad_vertices);

		const float qv[8] = {
			0, 0,
			0, 1,
			1, 1,
			1, 0
		};

		glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 8, qv, GL_STATIC_DRAW);

		glBindBuffer(GL_ARRAY_BUFFER, 0);

		glGenVertexArrays(1, &data.canvas_quad_array);
		glBindVertexArray(data.canvas_quad_array);
		glBindBuffer(GL_ARRAY_BUFFER, data.canvas_quad_vertices);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
		glEnableVertexAttribArray(0);
		for (uint32_t attribute = 8; attribute <= 15; attribute++) {
			glEnableVertexAttribArray(attribute);
			glVertexAttribDivisor(attribute, 1);
		}
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0); //unbind
	}

	{
		//particle quad buffers

		glGenBuffers(1, &data.particle_quad_vertices);
		glBindBuffer(GL_ARRAY_BUFFER, data.particle_quad_vertices);
		{
			//quad of size 1, with pivot on the center for particles, then regular UVS. Color is general plus fetched from particle
			const float qv[16] = {
				-0.5, -0.5,
				0.0, 0.0,
				-0.5, 0.5,
				0.0, 1.0,
				0.5, 0.5,
				1.0, 1.0,
				0.5, -0.5,
				1.0, 0.0
			};

			glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 16, qv, GL_STATIC_DRAW);
		}

		glBindBuffer(GL_ARRAY_BUFFER, 0); //unbind

		glGenVertexArrays(1, &data.particle_quad_array);
		glBindVertexArray(data.particle_quad_array);
		glBindBuffer(GL_ARRAY_BUFFER, data.particle_quad_vertices);
		glEnableVertexAttribArray(RSE::ARRAY_VERTEX);
		glVertexAttribPointer(RSE::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, nullptr);
		glEnableVertexAttribArray(RSE::ARRAY_TEX_UV);
		glVertexAttribPointer(RSE::ARRAY_TEX_UV, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, CAST_INT_TO_UCHAR_PTR(8));
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0); //unbind
	}

	// ninepatch buffers
	{
		// array buffer
		glGenBuffers(1, &data.ninepatch_vertices);
		glBindBuffer(GL_ARRAY_BUFFER, data.ninepatch_vertices);

		glBufferData(GL_ARRAY_BUFFER, sizeof(float) * (16 + 16) * 2, nullptr, GL_DYNAMIC_DRAW);

		glBindBuffer(GL_ARRAY_BUFFER, 0);

		// element buffer
		glGenBuffers(1, &data.ninepatch_elements);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ninepatch_elements);

#define _EIDX(y, x) (y * 4 + x)
		uint8_t elems[3 * 2 * 9] = {
			// first row

			_EIDX(0, 0), _EIDX(0, 1), _EIDX(1, 1),
			_EIDX(1, 1), _EIDX(1, 0), _EIDX(0, 0),

			_EIDX(0, 1), _EIDX(0, 2), _EIDX(1, 2),
			_EIDX(1, 2), _EIDX(1, 1), _EIDX(0, 1),

			_EIDX(0, 2), _EIDX(0, 3), _EIDX(1, 3),
			_EIDX(1, 3), _EIDX(1, 2), _EIDX(0, 2),

			// second row

			_EIDX(1, 0), _EIDX(1, 1), _EIDX(2, 1),
			_EIDX(2, 1), _EIDX(2, 0), _EIDX(1, 0),

			// the center one would be here, but we'll put it at the end
			// so it's easier to disable the center and be able to use
			// one draw call for both

			_EIDX(1, 2), _EIDX(1, 3), _EIDX(2, 3),
			_EIDX(2, 3), _EIDX(2, 2), _EIDX(1, 2),

			// third row

			_EIDX(2, 0), _EIDX(2, 1), _EIDX(3, 1),
			_EIDX(3, 1), _EIDX(3, 0), _EIDX(2, 0),

			_EIDX(2, 1), _EIDX(2, 2), _EIDX(3, 2),
			_EIDX(3, 2), _EIDX(3, 1), _EIDX(2, 1),

			_EIDX(2, 2), _EIDX(2, 3), _EIDX(3, 3),
			_EIDX(3, 3), _EIDX(3, 2), _EIDX(2, 2),

			// center field

			_EIDX(1, 1), _EIDX(1, 2), _EIDX(2, 2),
			_EIDX(2, 2), _EIDX(2, 1), _EIDX(1, 1)
		};
#undef _EIDX

		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(elems), elems, GL_STATIC_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	if (config->max_uniform_buffer_size < 65536) {
		data.max_lights_per_render = 64;
	} else {
		data.max_lights_per_render = 256;
	}

	// Reserve 3 Uniform Buffers for instance data Frame N, N+1 and N+2
	data.max_instances_per_buffer = uint32_t(GLOBAL_GET("rendering/gl_compatibility/item_buffer_size"));
	data.max_instance_buffer_size = data.max_instances_per_buffer * sizeof(InstanceData); // 16,384 instances * 128 bytes = 2,097,152 bytes = 2,048 kb
	state.canvas_instance_data_buffers.resize(3);
	state.canvas_instance_batches.reserve(200);

	for (int i = 0; i < 3; i++) {
		GLuint new_buffers[3];
		glGenBuffers(3, new_buffers);
		// Batch UBO.
		glBindBuffer(GL_ARRAY_BUFFER, new_buffers[0]);
		GLES3::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, new_buffers[0], data.max_instance_buffer_size, nullptr, GL_STREAM_DRAW, "Batch UBO[0][0]");
		// Light uniform buffer.
		glBindBuffer(GL_UNIFORM_BUFFER, new_buffers[1]);
		GLES3::Utilities::get_singleton()->buffer_allocate_data(GL_UNIFORM_BUFFER, new_buffers[1], sizeof(LightUniform) * data.max_lights_per_render, nullptr, GL_STREAM_DRAW, "2D lights UBO[0]");
		// State buffer.
		glBindBuffer(GL_UNIFORM_BUFFER, new_buffers[2]);
		GLES3::Utilities::get_singleton()->buffer_allocate_data(GL_UNIFORM_BUFFER, new_buffers[2], sizeof(StateBuffer), nullptr, GL_STREAM_DRAW, "2D state UBO[0]");
		DataBuffer db;
		db.instance_buffers.push_back(new_buffers[0]);
		db.light_ubo = new_buffers[1];
		db.state_ubo = new_buffers[2];
		db.last_frame_used = 0;
		db.last_fence_epoch = 0;
		state.canvas_instance_data_buffers[i] = db;
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	state.instance_data_array = memnew_arr(InstanceData, data.max_instances_per_buffer);
	state.light_uniforms = memnew_arr(LightUniform, data.max_lights_per_render);
	state.shadow_band_cache.resize(data.max_lights_per_render);

	{
		const uint32_t indices[6] = { 0, 2, 1, 3, 2, 0 };
		glGenVertexArrays(1, &data.indexed_quad_array);
		glBindVertexArray(data.indexed_quad_array);
		glBindBuffer(GL_ARRAY_BUFFER, data.canvas_quad_vertices);
		glGenBuffers(1, &data.indexed_quad_buffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.indexed_quad_buffer);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint32_t) * 6, indices, GL_STATIC_DRAW);
		for (uint32_t attribute = 8; attribute <= 15; attribute++) {
			glEnableVertexAttribArray(attribute);
			glVertexAttribDivisor(attribute, 1);
		}
		glBindVertexArray(0);
	}

	String global_defines;
	global_defines += "#define MAX_GLOBAL_SHADER_UNIFORMS 256\n"; // TODO: this is arbitrary for now
	global_defines += "#define MAX_LIGHTS " + itos(data.max_lights_per_render) + "\n";

	GLES3::MaterialStorage::get_singleton()->shaders.canvas_shader.initialize(global_defines, 1);
	data.canvas_shader_default_version = GLES3::MaterialStorage::get_singleton()->shaders.canvas_shader.version_create();

	state.shadow_texture_size = GLOBAL_GET("rendering/2d/shadow_atlas/size");
	shadow_render.shader.initialize();
	shadow_render.shader_version = shadow_render.shader.version_create();

	{
		default_canvas_group_shader = material_storage->shader_allocate();
		material_storage->shader_initialize(default_canvas_group_shader);

		material_storage->shader_set_code(default_canvas_group_shader, R"(
// Default CanvasGroup shader.

shader_type canvas_item;
render_mode unshaded;

uniform sampler2D screen_texture : hint_screen_texture, repeat_disable, filter_nearest;

void fragment() {
	vec4 c = textureLod(screen_texture, SCREEN_UV, 0.0);

	if (c.a > 0.0001) {
		c.rgb /= c.a;
	}

	COLOR *= c;
}
)");
		default_canvas_group_material = material_storage->material_allocate();
		material_storage->material_initialize(default_canvas_group_material);

		material_storage->material_set_shader(default_canvas_group_material, default_canvas_group_shader);
	}

	{
		default_clip_children_shader = material_storage->shader_allocate();
		material_storage->shader_initialize(default_clip_children_shader);

		material_storage->shader_set_code(default_clip_children_shader, R"(
// Default clip children shader.

shader_type canvas_item;
render_mode unshaded;

uniform sampler2D screen_texture : hint_screen_texture, repeat_disable, filter_nearest;

void fragment() {
	vec4 c = textureLod(screen_texture, SCREEN_UV, 0.0);
	COLOR.rgb = c.rgb;
}
)");
		default_clip_children_material = material_storage->material_allocate();
		material_storage->material_initialize(default_clip_children_material);

		material_storage->material_set_shader(default_clip_children_material, default_clip_children_shader);
	}

	default_canvas_texture = texture_storage->canvas_texture_allocate();
	texture_storage->canvas_texture_initialize(default_canvas_texture);

	state.time = 0.0;
}

RasterizerCanvasGLES3::~RasterizerCanvasGLES3() {
	singleton = nullptr;

	GLES3::MaterialStorage *material_storage = GLES3::MaterialStorage::get_singleton();
	material_storage->shaders.canvas_shader.version_free(data.canvas_shader_default_version);
	shadow_render.shader.version_free(shadow_render.shader_version);
	material_storage->material_free(default_canvas_group_material);
	material_storage->shader_free(default_canvas_group_shader);
	material_storage->material_free(default_clip_children_material);
	material_storage->shader_free(default_clip_children_shader);
	singleton = nullptr;

	glDeleteBuffers(1, &data.canvas_quad_vertices);
	glDeleteVertexArrays(1, &data.canvas_quad_array);

	glDeleteBuffers(1, &data.indexed_quad_buffer);
	glDeleteVertexArrays(1, &data.indexed_quad_array);

	GLES3::TextureStorage::get_singleton()->canvas_texture_free(default_canvas_texture);
	memdelete_arr(state.instance_data_array);
	memdelete_arr(state.light_uniforms);

	if (state.shadow_fb != 0) {
		glDeleteFramebuffers(1, &state.shadow_fb);
		GLES3::Utilities::get_singleton()->texture_free_data(state.shadow_texture);
		glDeleteRenderbuffers(1, &state.shadow_depth_buffer);
		state.shadow_fb = 0;
		state.shadow_texture = 0;
		state.shadow_depth_buffer = 0;
	}

	for (const FrameFence &frame_fence : state.frame_fences) {
		if (frame_fence.fence != GLsync()) {
			glDeleteSync(frame_fence.fence);
		}
	}
	state.frame_fences.clear();

	for (uint32_t i = 0; i < state.canvas_instance_data_buffers.size(); i++) {
		for (int j = 0; j < state.canvas_instance_data_buffers[i].instance_buffers.size(); j++) {
			if (state.canvas_instance_data_buffers[i].instance_buffers[j]) {
				GLES3::Utilities::get_singleton()->buffer_free_data(state.canvas_instance_data_buffers[i].instance_buffers[j]);
			}
		}
		if (state.canvas_instance_data_buffers[i].light_ubo) {
			GLES3::Utilities::get_singleton()->buffer_free_data(state.canvas_instance_data_buffers[i].light_ubo);
		}
		if (state.canvas_instance_data_buffers[i].state_ubo) {
			GLES3::Utilities::get_singleton()->buffer_free_data(state.canvas_instance_data_buffers[i].state_ubo);
		}
	}
}

#endif // GLES3_ENABLED
