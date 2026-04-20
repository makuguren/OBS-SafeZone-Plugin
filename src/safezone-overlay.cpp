/*
SafeZone Overlay for OBS - Overlay
Copyright (C) 2026 Dcoderz Philippines <mail@dcoderz.site>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "safezone-overlay.hpp"

#include <obs.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <graphics/matrix4.h>
#include <graphics/vec2.h>
#include <plugin-support.h>

#include <QMainWindow>
#include <QWidget>

/* --------------------------------------------------------------------------
 * Layout-compatible shims for OBS Studio's OBSQTDisplay / OBSBasicPreview.
 *
 * OBS Studio does not expose the preview's obs_display_t *, scaling amount,
 * or scroll offset through the public frontend API. These values live as
 * private members of OBSBasicPreview (which inherits OBSQTDisplay). The
 * approach below matches the memory layout of those classes byte-for-byte
 * so we can reinterpret_cast the preview QWidget and read them directly.
 *
 * Reference headers (from .deps/obs-studio-31.1.1/frontend/widgets/):
 *     OBSQTDisplay.hpp
 *     OBSBasicPreview.hpp
 *
 * This layout is tied to OBS Studio 31.x. If OBS Studio changes the
 * declaration order or adds/removes members in either class, these
 * structs must be updated. The draw callback performs a sanity check on
 * the read values and falls back to a centered fit if they look bogus.
 * -------------------------------------------------------------------------- */

namespace {

struct OBSQTDisplayShim : public QWidget {
	obs_display_t *display;     // OBSDisplay wraps obs_display_t *
	bool destroying;            // + 3 bytes padding
	uint32_t backgroundColor;
};

struct OBSBasicPreviewShim : public OBSQTDisplayShim {
	struct obs_sceneitem_crop startCrop;      // 16
	struct vec2 startItemPos;                  // 8
	struct vec2 cropSize;                      // 8
	obs_sceneitem_t *stretchGroup;             // 8 (OBSSceneItem = single ptr)
	obs_sceneitem_t *stretchItem;              // 8
	uint32_t stretchHandle;                    // 4 (enum class : uint32_t)
	float rotateAngle;                         // 4
	struct vec2 rotatePoint;                   // 8
	struct vec2 offsetPoint;                   // 8
	struct vec2 stretchItemSize;               // 8
	struct matrix4 screenToItem;               // 64 (16-byte aligned)
	struct matrix4 itemToScreen;               // 64
	struct matrix4 invGroupTransform;          // 64
	gs_texture_t *overflow;                    // 8
	gs_vertbuffer_t *rectFill;                 // 8
	gs_vertbuffer_t *circleFill;               // 8
	gs_effect_t *solidEffect;                  // 8
	gs_effect_t *stripedLineEffect;            // 8
	struct vec2 startPos;                      // 8
	struct vec2 mousePos;                      // 8
	struct vec2 lastMoveOffset;                // 8
	struct vec2 scrollingFrom;                 // 8
	struct vec2 scrollingOffset;               // 8  <-- read
	bool mouseDown;                            // 1
	bool mouseMoved;                           // 1
	bool mouseOverItems;                       // 1
	bool cropping;                             // 1
	bool locked;                               // 1
	bool scrollMode;                           // 1
	bool fixedScaling;                         // 1  <-- read
	bool selectionBox;                         // 1
	bool overflowHidden;                       // 1
	bool overflowSelectionHidden;              // 1
	bool overflowAlwaysVisible;                // 1 + 1 byte padding
	int32_t scalingLevel;                      // 4
	float scalingAmount;                       // 4  <-- read
	/* members past this point are unused by the overlay */
};

// From OBS Studio's OBSBasic.hpp:
//     #define PREVIEW_EDGE_SIZE 10
constexpr int PREVIEW_EDGE_SIZE = 10;

// Equivalent to OBS's GetScaleAndCenterPos (display-helpers.hpp).
inline void GetScaleAndCenterPos(int baseCX, int baseCY, int windowCX,
				 int windowCY, int &x, int &y, float &scale)
{
	const double windowAspect = double(windowCX) / double(windowCY);
	const double baseAspect = double(baseCX) / double(baseCY);

	int newCX, newCY;
	if (windowAspect > baseAspect) {
		scale = float(windowCY) / float(baseCY);
		newCX = int(double(windowCY) * baseAspect);
		newCY = windowCY;
	} else {
		scale = float(windowCX) / float(baseCX);
		newCX = windowCX;
		newCY = int(double(windowCX) / baseAspect);
	}

	x = windowCX / 2 - newCX / 2;
	y = windowCY / 2 - newCY / 2;
}

// Equivalent to OBS's GetCenterPosFromFixedScale.
inline void GetCenterPosFromFixedScale(int baseCX, int baseCY, int windowCX,
				       int windowCY, int &x, int &y,
				       float scale)
{
	x = int((float(windowCX) - float(baseCX) * scale) / 2.0f);
	y = int((float(windowCY) - float(baseCY) * scale) / 2.0f);
}

} // namespace

SafeZoneOverlay *SafeZoneOverlay::s_instance = nullptr;

bool SafeZoneOverlay::enable()
{
	if (s_instance)
		return true;

	auto *overlay = new SafeZoneOverlay();

	auto *mainWindow = static_cast<QMainWindow *>(
		obs_frontend_get_main_window());
	if (!mainWindow) {
		obs_log(LOG_WARNING,
			"SafeZone Overlay: unable to get main window");
		delete overlay;
		return false;
	}

	auto *previewWidget = mainWindow->findChild<QWidget *>("preview");
	if (!previewWidget) {
		obs_log(LOG_WARNING,
			"SafeZone Overlay: preview widget not found");
		delete overlay;
		return false;
	}

	auto *preview =
		reinterpret_cast<OBSBasicPreviewShim *>(previewWidget);
	overlay->m_previewWidget = previewWidget;
	overlay->m_previewDisplay = preview->display;

	if (!overlay->m_previewDisplay) {
		obs_log(LOG_WARNING,
			"SafeZone Overlay: preview display is null (OBS "
			"still starting up?)");
		delete overlay;
		return false;
	}

	if (!overlay->loadTexture()) {
		delete overlay;
		return false;
	}

	obs_display_add_draw_callback(overlay->m_previewDisplay,
				      SafeZoneOverlay::drawCallback,
				      overlay);

	s_instance = overlay;
	obs_log(LOG_INFO, "SafeZone Overlay enabled");
	return true;
}

void SafeZoneOverlay::disable()
{
	if (!s_instance)
		return;

	// OBS Studio destroys the main preview's obs_display_t very early in
	// OBSBasic::closeEvent (via ui->preview->DestroyDisplay(), which
	// sets OBSBasicPreview::display = nullptr and frees the struct),
	// long BEFORE OBS_FRONTEND_EVENT_EXIT is dispatched and long before
	// our dock's Qt destructor runs. That makes s_instance->m_previewDisplay
	// a dangling pointer at teardown time; passing it to
	// obs_display_remove_draw_callback crashes inside pthreads.
	//
	// Re-read the preview's current display pointer through the shim
	// instead, and only call the remove API if:
	//   1) the preview QWidget is still alive (QPointer non-null), and
	//   2) preview->display is still non-null (DestroyDisplay hasn't
	//      run yet), and
	//   3) it matches the display we originally registered against.
	obs_display_t *liveDisplay = nullptr;
	if (s_instance->m_previewWidget) {
		auto *preview = reinterpret_cast<OBSBasicPreviewShim *>(
			s_instance->m_previewWidget.data());
		liveDisplay = preview->display;
	}

	if (liveDisplay && liveDisplay == s_instance->m_previewDisplay) {
		obs_display_remove_draw_callback(
			liveDisplay, SafeZoneOverlay::drawCallback,
			s_instance);
	}

	delete s_instance;
	s_instance = nullptr;

	obs_log(LOG_INFO, "SafeZone Overlay disabled");
}

bool SafeZoneOverlay::isEnabled()
{
	return s_instance != nullptr;
}

SafeZoneOverlay::SafeZoneOverlay() = default;

SafeZoneOverlay::~SafeZoneOverlay()
{
	freeTexture();
}

bool SafeZoneOverlay::loadTexture()
{
	const char *candidates[] = {
		"safezone-overlay.png",
		"safezone-overlay.jpg",
		"safezone-overlay.jpeg",
		"safezone-overlay.bmp",
	};

	char *resolved = nullptr;
	for (const char *name : candidates) {
		resolved = obs_module_file(name);
		if (resolved)
			break;
	}

	if (!resolved) {
		obs_log(LOG_WARNING,
			"SafeZone Overlay: no overlay image found in data/ "
			"(expected safezone-overlay.png)");
		return false;
	}

	auto *image = static_cast<gs_image_file4_t *>(
		bzalloc(sizeof(gs_image_file4_t)));

	gs_image_file4_init(image, resolved, GS_IMAGE_ALPHA_PREMULTIPLY_SRGB);
	bfree(resolved);

	if (!image->image3.image2.image.loaded) {
		obs_log(LOG_WARNING,
			"SafeZone Overlay: failed to decode overlay image");
		gs_image_file4_free(image);
		bfree(image);
		return false;
	}

	obs_enter_graphics();
	gs_image_file4_init_texture(image);
	obs_leave_graphics();

	if (!image->image3.image2.image.texture) {
		obs_log(LOG_WARNING,
			"SafeZone Overlay: failed to upload overlay texture");
		obs_enter_graphics();
		gs_image_file4_free(image);
		obs_leave_graphics();
		bfree(image);
		return false;
	}

	m_imageFile = image;
	m_texture = image->image3.image2.image.texture;
	m_textureWidth = image->image3.image2.image.cx;
	m_textureHeight = image->image3.image2.image.cy;

	obs_log(LOG_INFO,
		"SafeZone Overlay: loaded texture (%ux%u)",
		m_textureWidth, m_textureHeight);
	return true;
}

void SafeZoneOverlay::freeTexture()
{
	if (!m_imageFile)
		return;

	auto *image = static_cast<gs_image_file4_t *>(m_imageFile);

	obs_enter_graphics();
	gs_image_file4_free(image);
	obs_leave_graphics();

	bfree(image);

	m_imageFile = nullptr;
	m_texture = nullptr;
	m_textureWidth = 0;
	m_textureHeight = 0;
}

void SafeZoneOverlay::drawCallback(void *data, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<SafeZoneOverlay *>(data);
	if (!self || !self->m_texture || !self->m_previewWidget || cx == 0 ||
	    cy == 0)
		return;

	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;
	if (ovi.base_width == 0 || ovi.base_height == 0)
		return;

	// Read the live preview state directly from OBSBasicPreview's memory.
	// This matches the values OBS uses to place the rendered canvas, so
	// the overlay stays pixel-perfectly aligned with the preview frame
	// under Ctrl+Scroll zoom and Spacebar pan.
	auto *preview = reinterpret_cast<OBSBasicPreviewShim *>(
		self->m_previewWidget.data());

	const bool fixedScaling = preview->fixedScaling;
	float scalingAmount = preview->scalingAmount;
	const float scrollX = preview->scrollingOffset.x;
	const float scrollY = preview->scrollingOffset.y;

	// Sanity check - a bogus scalingAmount indicates the layout shim is
	// out of date with this OBS build; fall back to safe defaults.
	if (!(scalingAmount > 0.0f && scalingAmount < 1000.0f)) {
		scalingAmount = 1.0f;
	}

	// cx / cy arrive in device pixels, matching GetPixelSize(ui->preview)
	// in OBS's ResizePreview. The inner "window" area is shrunk by the
	// preview edge margin on each side.
	const int windowCX = int(cx) - PREVIEW_EDGE_SIZE * 2;
	const int windowCY = int(cy) - PREVIEW_EDGE_SIZE * 2;
	if (windowCX <= 0 || windowCY <= 0)
		return;

	int previewX = 0;
	int previewY = 0;
	float previewScale = 1.0f;

	if (fixedScaling) {
		previewScale = scalingAmount;
		GetCenterPosFromFixedScale(int(ovi.base_width),
					   int(ovi.base_height), windowCX,
					   windowCY, previewX, previewY,
					   previewScale);
		previewX += int(scrollX);
		previewY += int(scrollY);
	} else {
		GetScaleAndCenterPos(int(ovi.base_width),
				     int(ovi.base_height), windowCX,
				     windowCY, previewX, previewY,
				     previewScale);
	}

	previewX += PREVIEW_EDGE_SIZE;
	previewY += PREVIEW_EDGE_SIZE;

	const int previewCX = int(previewScale * float(ovi.base_width));
	const int previewCY = int(previewScale * float(ovi.base_height));

	// Draw the overlay texture in canvas coordinates inside the same
	// viewport OBS uses for the scene. Because we use the same math, the
	// overlay automatically tracks preview resize, canvas resolution,
	// Ctrl+Scroll zoom, and Spacebar pan.
	gs_viewport_push();
	gs_projection_push();

	gs_set_viewport(previewX, previewY, previewCX, previewCY);
	gs_ortho(0.0f, float(ovi.base_width), 0.0f, float(ovi.base_height),
		 -100.0f, 100.0f);

	gs_blend_state_push();
	gs_reset_blend_state();
	gs_enable_blending(true);
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image =
		gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture(image, self->m_texture);

	while (gs_effect_loop(effect, "Draw")) {
		gs_draw_sprite(self->m_texture, 0, ovi.base_width,
			       ovi.base_height);
	}

	gs_blend_state_pop();
	gs_projection_pop();
	gs_viewport_pop();
}
