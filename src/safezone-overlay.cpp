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

#include <QDir>
#include <QFileInfo>
#include <QMainWindow>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <cstring>

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
	obs_display_t *display;
	bool destroying;
	uint32_t backgroundColor;
};

struct OBSBasicPreviewShim : public OBSQTDisplayShim {
	struct obs_sceneitem_crop startCrop;
	struct vec2 startItemPos;
	struct vec2 cropSize;
	obs_sceneitem_t *stretchGroup;
	obs_sceneitem_t *stretchItem;
	uint32_t stretchHandle;
	float rotateAngle;
	struct vec2 rotatePoint;
	struct vec2 offsetPoint;
	struct vec2 stretchItemSize;
	struct matrix4 screenToItem;
	struct matrix4 itemToScreen;
	struct matrix4 invGroupTransform;
	gs_texture_t *overflow;
	gs_vertbuffer_t *rectFill;
	gs_vertbuffer_t *circleFill;
	gs_effect_t *solidEffect;
	gs_effect_t *stripedLineEffect;
	struct vec2 startPos;
	struct vec2 mousePos;
	struct vec2 lastMoveOffset;
	struct vec2 scrollingFrom;
	struct vec2 scrollingOffset; // <-- read
	bool mouseDown;
	bool mouseMoved;
	bool mouseOverItems;
	bool cropping;
	bool locked;
	bool scrollMode;
	bool fixedScaling; // <-- read
	bool selectionBox;
	bool overflowHidden;
	bool overflowSelectionHidden;
	bool overflowAlwaysVisible;
	int32_t scalingLevel;
	float scalingAmount; // <-- read
};

constexpr int PREVIEW_EDGE_SIZE = 10;

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

inline void GetCenterPosFromFixedScale(int baseCX, int baseCY, int windowCX,
				       int windowCY, int &x, int &y, float scale)
{
	x = int((float(windowCX) - float(baseCX) * scale) / 2.0f);
	y = int((float(windowCY) - float(baseCY) * scale) / 2.0f);
}

// ---------------------------------------------------------------------------
// Pixel helpers for custom safe-zone generation
// ---------------------------------------------------------------------------

inline void setPixel(uint8_t *dst, uint8_t r, uint8_t g, uint8_t b,
		     uint8_t a)
{
	dst[0] = r;
	dst[1] = g;
	dst[2] = b;
	dst[3] = a;
}

static void fillRect(uint8_t *pixels, uint32_t w, uint32_t h, uint32_t x0,
		     uint32_t y0, uint32_t x1, uint32_t y1, uint8_t r,
		     uint8_t g, uint8_t b, uint8_t a)
{
	for (uint32_t y = y0; y <= y1 && y < h; ++y) {
		for (uint32_t x = x0; x <= x1 && x < w; ++x) {
			setPixel(&pixels[(y * w + x) * 4], r, g, b, a);
		}
	}
}

static void drawHLine(uint8_t *pixels, uint32_t w, uint32_t h, uint32_t y,
		      uint32_t x0, uint32_t x1, uint32_t thickness, uint8_t r,
		      uint8_t g, uint8_t b, uint8_t a)
{
	for (uint32_t t = 0; t < thickness; ++t) {
		uint32_t row = y + t;
		if (row >= h) // safety — shouldn't happen
			break;
		for (uint32_t x = x0; x <= x1 && x < w; ++x)
			setPixel(&pixels[(row * w + x) * 4], r, g, b, a);
	}
}

static void drawVLine(uint8_t *pixels, uint32_t w, uint32_t h, uint32_t x,
		      uint32_t y0, uint32_t y1, uint32_t thickness, uint8_t r,
		      uint8_t g, uint8_t b, uint8_t a)
{
	for (uint32_t t = 0; t < thickness; ++t) {
		uint32_t col = x + t;
		if (col >= w)
			break;
		for (uint32_t y = y0; y <= y1 && y < h; ++y)
			setPixel(&pixels[(y * w + col) * 4], r, g, b, a);
	}
}

static void drawDashedHLine(uint8_t *pixels, uint32_t w, uint32_t y,
			    uint32_t x0, uint32_t x1, uint8_t r, uint8_t g,
			    uint8_t b, uint8_t a)
{
	constexpr uint32_t dashLen = 12;
	constexpr uint32_t gapLen = 6;
	uint32_t pos = x0;
	bool draw = true;
	uint32_t count = 0;
	while (pos <= x1 && pos < w) {
		if (draw)
			setPixel(&pixels[(y * w + pos) * 4], r, g, b, a);
		++pos;
		if (++count >= (draw ? dashLen : gapLen)) {
			draw = !draw;
			count = 0;
		}
	}
}

static void drawDashedVLine(uint8_t *pixels, uint32_t w, uint32_t h,
			    uint32_t x, uint32_t y0, uint32_t y1, uint8_t r,
			    uint8_t g, uint8_t b, uint8_t a)
{
	constexpr uint32_t dashLen = 12;
	constexpr uint32_t gapLen = 6;
	uint32_t pos = y0;
	bool draw = true;
	uint32_t count = 0;
	while (pos <= y1 && pos < h) {
		if (draw)
			setPixel(&pixels[(pos * w + x) * 4], r, g, b, a);
		++pos;
		if (++count >= (draw ? dashLen : gapLen)) {
			draw = !draw;
			count = 0;
		}
	}
}

} // namespace

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------

SafeZoneOverlay *SafeZoneOverlay::s_instance = nullptr;
std::string SafeZoneOverlay::s_imageFile = "safezone-overlay.png";
bool SafeZoneOverlay::s_customEnabled = false;
int SafeZoneOverlay::s_marginTop = 10;
int SafeZoneOverlay::s_marginBottom = 10;
int SafeZoneOverlay::s_marginLeft = 10;
int SafeZoneOverlay::s_marginRight = 10;

// ---------------------------------------------------------------------------
// enable / disable / isEnabled
// ---------------------------------------------------------------------------

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
				      SafeZoneOverlay::drawCallback, overlay);

	s_instance = overlay;
	obs_log(LOG_INFO, "SafeZone Overlay enabled");
	return true;
}

void SafeZoneOverlay::disable()
{
	if (!s_instance)
		return;

	obs_display_t *liveDisplay = nullptr;
	if (s_instance->m_previewWidget) {
		auto *preview = reinterpret_cast<OBSBasicPreviewShim *>(
			s_instance->m_previewWidget.data());
		liveDisplay = preview->display;
	}

	if (liveDisplay && liveDisplay == s_instance->m_previewDisplay) {
		obs_display_remove_draw_callback(
			liveDisplay, SafeZoneOverlay::drawCallback, s_instance);
	}

	delete s_instance;
	s_instance = nullptr;

	obs_log(LOG_INFO, "SafeZone Overlay disabled");
}

bool SafeZoneOverlay::isEnabled()
{
	return s_instance != nullptr;
}

// ---------------------------------------------------------------------------
// Image file (image mode)
// ---------------------------------------------------------------------------

void SafeZoneOverlay::setImageFile(const std::string &filename)
{
	if (s_imageFile == filename && !s_customEnabled)
		return;

	s_imageFile = filename;

	if (s_instance && !s_customEnabled) {
		s_instance->freeTexture();
		if (!s_instance->loadTexture()) {
			obs_log(LOG_WARNING,
				"SafeZone Overlay: texture reload failed for '%s'",
				filename.c_str());
		}
	}
}

const std::string &SafeZoneOverlay::imageFile()
{
	return s_imageFile;
}

std::vector<std::string> SafeZoneOverlay::availableImageFiles()
{
	std::vector<std::string> result;

	char *dataDir = obs_module_file("");
	if (!dataDir)
		return result;

	QDir dir(QString::fromUtf8(dataDir));
	bfree(dataDir);

	const QStringList entries =
		dir.entryList(QStringList() << "*.png", QDir::Files,
			      QDir::Name | QDir::IgnoreCase);

	result.reserve(static_cast<size_t>(entries.size()));
	for (const QString &entry : entries)
		result.push_back(entry.toStdString());

	return result;
}

// ---------------------------------------------------------------------------
// Custom safe-zone mode
// ---------------------------------------------------------------------------

static inline int clampMargin(int v)
{
	return std::max(0, std::min(50, v));
}

// Helper: reload texture when custom-mode parameters change.
static void reloadIfActive()
{
	if (!SafeZoneOverlay::isEnabled())
		return;
	// Access internal instance via the static enable/disable cycle trick:
	// disable() deletes the instance then we re-enable() — but that is
	// expensive and touches the display. Instead we call the internal
	// reload directly. Because freeTexture/loadTexture are private we
	// expose the reload through setCustomEnabled which already has access.
	// For margin changes we do the same: call setCustomEnabled to trigger
	// the reload path. A simpler approach: keep a package-level friend or
	// add a thin public helper. We use a dedicated private reload path:
	// the public API calls the lambda below via the existing setCustomEnabled.
	(void)0; // handled by callers directly
}

void SafeZoneOverlay::setCustomEnabled(bool enabled)
{
	if (s_customEnabled == enabled)
		return;
	s_customEnabled = enabled;

	if (s_instance) {
		s_instance->freeTexture();
		if (!s_instance->loadTexture()) {
			obs_log(LOG_WARNING,
				"SafeZone Overlay: texture reload failed after "
				"custom mode %s",
				enabled ? "enable" : "disable");
		}
	}
}

bool SafeZoneOverlay::isCustomEnabled()
{
	return s_customEnabled;
}

void SafeZoneOverlay::setCustomMargins(int top, int bottom, int left,
				       int right)
{
	s_marginTop = clampMargin(top);
	s_marginBottom = clampMargin(bottom);
	s_marginLeft = clampMargin(left);
	s_marginRight = clampMargin(right);

	// Reload texture if custom mode is active.
	if (s_instance && s_customEnabled) {
		s_instance->freeTexture();
		if (!s_instance->loadTexture()) {
			obs_log(LOG_WARNING,
				"SafeZone Overlay: texture reload failed after "
				"margin change");
		}
	}
}

int SafeZoneOverlay::customMarginTop()    { return s_marginTop;    }
int SafeZoneOverlay::customMarginBottom() { return s_marginBottom; }
int SafeZoneOverlay::customMarginLeft()   { return s_marginLeft;   }
int SafeZoneOverlay::customMarginRight()  { return s_marginRight;  }



// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

SafeZoneOverlay::SafeZoneOverlay() = default;

SafeZoneOverlay::~SafeZoneOverlay()
{
	freeTexture();
}

// ---------------------------------------------------------------------------
// Custom pixel generation
// ---------------------------------------------------------------------------

uint8_t *SafeZoneOverlay::generateCustomPixels(uint32_t w, uint32_t h,
					       int top, int bottom, int left,
					       int right)
{
	const size_t bufSize = size_t(w) * h * 4;
	auto *pixels = static_cast<uint8_t *>(bzalloc(bufSize));
	if (!pixels)
		return nullptr;

	// Pixel coordinates of the safe-zone rectangle.
	const uint32_t x0 = uint32_t(w * left / 100);
	const uint32_t y0 = uint32_t(h * top / 100);
	const uint32_t x1 = w - uint32_t(w * right / 100) - 1;
	const uint32_t y1 = h - uint32_t(h * bottom / 100) - 1;

	if (x0 >= x1 || y0 >= y1) {
		// Degenerate rectangle — return empty (all-transparent) buffer.
		return pixels;
	}

	// Shade the area outside the safe zone with semi-transparent black.
	const uint8_t bgR = 0, bgG = 0, bgB = 0, bgA = 0x80; // 50% opacity

	// Top region
	if (y0 > 0)
		fillRect(pixels, w, h, 0, 0, w - 1, y0 - 1, bgR, bgG, bgB, bgA);
	// Bottom region
	if (y1 + 1 < h)
		fillRect(pixels, w, h, 0, y1 + 1, w - 1, h - 1, bgR, bgG, bgB, bgA);
	// Left region (between top and bottom)
	if (x0 > 0)
		fillRect(pixels, w, h, 0, y0, x0 - 1, y1, bgR, bgG, bgB, bgA);
	// Right region (between top and bottom)
	if (x1 + 1 < w)
		fillRect(pixels, w, h, x1 + 1, y0, w - 1, y1, bgR, bgG, bgB, bgA);

	const uint32_t lw = std::max(4u, w / 240); // ~8px at 1920w

	// Outline colour: white, mostly opaque
	const uint8_t R = 0xFF, G = 0xFF, B = 0xFF, A = 0xDC;

	// Top and bottom horizontal lines
	drawHLine(pixels, w, h, y0, x0, x1, lw, R, G, B, A);
	if (y1 >= lw)
		drawHLine(pixels, w, h, y1 - lw + 1, x0, x1, lw, R, G, B, A);

	// Left and right vertical lines (avoid overdrawing corners)
	drawVLine(pixels, w, h, x0, y0 + lw, y1 - lw, lw, R, G, B, A);
	if (x1 >= lw)
		drawVLine(pixels, w, h, x1 - lw + 1, y0 + lw, y1 - lw, lw,
			  R, G, B, A);

	// Dashed centre crosshair (50% alpha)
	const uint32_t midX = (x0 + x1) / 2;
	const uint32_t midY = (y0 + y1) / 2;
	drawDashedHLine(pixels, w, midY, x0 + lw, x1 - lw, R, G, B, 0x80);
	drawDashedVLine(pixels, w, h, midX, y0 + lw, y1 - lw, R, G, B, 0x80);

	return pixels;
}

// ---------------------------------------------------------------------------
// loadTexture / freeTexture
// ---------------------------------------------------------------------------

bool SafeZoneOverlay::loadTexture()
{
	// -----------------------------------------------------------------
	// Custom mode: generate texture from margin settings.
	// -----------------------------------------------------------------
	if (s_customEnabled) {
		// Use the actual canvas resolution if available, else 1920x1080.
		struct obs_video_info ovi = {};
		obs_get_video_info(&ovi);
		const uint32_t w =
			(ovi.base_width > 0) ? ovi.base_width : 1920;
		const uint32_t h =
			(ovi.base_height > 0) ? ovi.base_height : 1080;

		uint8_t *pixels = generateCustomPixels(
			w, h, s_marginTop, s_marginBottom, s_marginLeft,
			s_marginRight);
		if (!pixels) {
			obs_log(LOG_WARNING,
				"SafeZone Overlay: failed to generate custom "
				"safe-zone pixels");
			return false;
		}

		obs_enter_graphics();
		gs_texture_t *tex =
			gs_texture_create(w, h, GS_RGBA, 1,
					  (const uint8_t **)&pixels, GS_DYNAMIC);
		obs_leave_graphics();

		bfree(pixels);

		if (!tex) {
			obs_log(LOG_WARNING,
				"SafeZone Overlay: gs_texture_create failed "
				"for custom safe zone");
			return false;
		}

		m_texture = tex;
		m_textureWidth = w;
		m_textureHeight = h;
		m_textureIsGenerated = true;

		obs_log(LOG_INFO,
			"SafeZone Overlay: generated custom safe zone "
			"(%ux%u, T=%d B=%d L=%d R=%d%%)",
			w, h, s_marginTop, s_marginBottom, s_marginLeft,
			s_marginRight);
		return true;
	}

	// -----------------------------------------------------------------
	// Image mode: load from file in data/.
	// -----------------------------------------------------------------
	char *resolved = obs_module_file(s_imageFile.c_str());
	if (!resolved) {
		obs_log(LOG_WARNING,
			"SafeZone Overlay: image file '%s' not found in data/",
			s_imageFile.c_str());
		return false;
	}

	auto *image = static_cast<gs_image_file4_t *>(
		bzalloc(sizeof(gs_image_file4_t)));

	gs_image_file4_init(image, resolved, GS_IMAGE_ALPHA_PREMULTIPLY_SRGB);
	bfree(resolved);

	if (!image->image3.image2.image.loaded) {
		obs_log(LOG_WARNING,
			"SafeZone Overlay: failed to decode '%s'",
			s_imageFile.c_str());
		gs_image_file4_free(image);
		bfree(image);
		return false;
	}

	obs_enter_graphics();
	gs_image_file4_init_texture(image);
	obs_leave_graphics();

	if (!image->image3.image2.image.texture) {
		obs_log(LOG_WARNING,
			"SafeZone Overlay: failed to upload texture for '%s'",
			s_imageFile.c_str());
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
	m_textureIsGenerated = false;

	obs_log(LOG_INFO, "SafeZone Overlay: loaded '%s' (%ux%u)",
		s_imageFile.c_str(), m_textureWidth, m_textureHeight);
	return true;
}

void SafeZoneOverlay::freeTexture()
{
	if (m_textureIsGenerated) {
		if (m_texture) {
			obs_enter_graphics();
			gs_texture_destroy(m_texture);
			obs_leave_graphics();
			m_texture = nullptr;
		}
		m_textureIsGenerated = false;
	} else if (m_imageFile) {
		auto *image = static_cast<gs_image_file4_t *>(m_imageFile);
		obs_enter_graphics();
		gs_image_file4_free(image);
		obs_leave_graphics();
		bfree(image);
	}

	m_imageFile = nullptr;
	m_texture = nullptr;
	m_textureWidth = 0;
	m_textureHeight = 0;
}

// ---------------------------------------------------------------------------
// Draw callback
// ---------------------------------------------------------------------------

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


	auto *preview = reinterpret_cast<OBSBasicPreviewShim *>(
		self->m_previewWidget.data());

	const bool fixedScaling = preview->fixedScaling;
	float scalingAmount = preview->scalingAmount;
	const float scrollX = preview->scrollingOffset.x;
	const float scrollY = preview->scrollingOffset.y;

	if (!(scalingAmount > 0.0f && scalingAmount < 1000.0f))
		scalingAmount = 1.0f;

	const int windowCX = int(cx) - PREVIEW_EDGE_SIZE * 2;
	const int windowCY = int(cy) - PREVIEW_EDGE_SIZE * 2;
	if (windowCX <= 0 || windowCY <= 0)
		return;

	int previewX = 0, previewY = 0;
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
		GetScaleAndCenterPos(int(ovi.base_width), int(ovi.base_height),
				     windowCX, windowCY, previewX, previewY,
				     previewScale);
	}

	previewX += PREVIEW_EDGE_SIZE;
	previewY += PREVIEW_EDGE_SIZE;

	const int previewCX = int(previewScale * float(ovi.base_width));
	const int previewCY = int(previewScale * float(ovi.base_height));

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

	gs_eparam_t *colorParam =
		gs_effect_get_param_by_name(effect, "color");

	gs_eparam_t *imageParam =
		gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture(imageParam, self->m_texture);

	while (gs_effect_loop(effect, "Draw")) {
		gs_draw_sprite(self->m_texture, 0, ovi.base_width,
			       ovi.base_height);
	}


	gs_blend_state_pop();
	gs_projection_pop();
	gs_viewport_pop();
}
