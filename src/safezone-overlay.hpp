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

#pragma once

#include <obs.h>

#include <QPointer>
#include <QWidget>

#include <string>
#include <vector>

struct gs_texture;
struct gs_image_file4;
typedef struct gs_texture gs_texture_t;

/*
 * Preview-only SafeZone overlay rendered via the OBS graphics pipeline.
 *
 * This mirrors the mechanism OBS itself uses for its built-in "Draw Safe
 * Areas (EBU R 95)" feature: it registers an obs_display_add_draw_callback
 * on the main preview's obs_display_t. The callback draws a textured quad
 * in canvas coordinates, so it automatically tracks:
 *   - preview widget size (letterbox/pillarbox),
 *   - base canvas resolution changes,
 *   - preview zoom (Ctrl+Scroll) and pan (Spacebar), by reading the live
 *     OBSBasicPreview scaling state.
 *
 * Because the drawing happens on the preview display only (not on any
 * source or on the main program output), it never appears in recordings,
 * streams, projectors, or virtual camera, and it never touches Studio Mode.
 *
 * Two mutually-exclusive modes:
 *   - Image mode  : displays a user-supplied PNG from the data/ folder.
 *   - Custom mode : draws a programmatic outline rectangle whose margins
 *                   (top / bottom / left / right, expressed as integer
 *                   percentages 0–50) are set by the user.
 */
class SafeZoneOverlay {
public:
	// Enable the overlay: load/generate the texture, find the main preview
	// display, and register the draw callback. Returns true on success.
	// Safe to call when already enabled.
	static bool enable();

	// Disable the overlay: unregister the callback and release the texture.
	// Safe to call when already disabled.
	static void disable();

	// True when the overlay is currently active.
	static bool isEnabled();

	// ---------------------------------------------------------------------------
	// Image file mode — selects a PNG from the plugin's data/ folder.
	// Ignored when custom mode is active.
	// ---------------------------------------------------------------------------
	static void setImageFile(const std::string &filename);
	static const std::string &imageFile();

	// Enumerate PNG files available in the plugin data/ directory.
	// Returns bare filenames, e.g. {"action-safe.png", "safezone-overlay.png"}.
	static std::vector<std::string> availableImageFiles();

	// ---------------------------------------------------------------------------
	// Custom safe-zone mode — draws a configurable outline rectangle.
	// Margins are integer percentages of the canvas dimension (0–50).
	// When enabled, the image dropdown is ignored.
	// ---------------------------------------------------------------------------
	static void setCustomEnabled(bool enabled);
	static bool isCustomEnabled();

	// Sets one or more margins; each value is clamped to [0, 50].
	// If the overlay is active the texture is regenerated immediately.
	static void setCustomMargins(int top, int bottom, int left, int right);
	static int customMarginTop();
	static int customMarginBottom();
	static int customMarginLeft();
	static int customMarginRight();



private:
	SafeZoneOverlay();
	~SafeZoneOverlay();

	SafeZoneOverlay(const SafeZoneOverlay &) = delete;
	SafeZoneOverlay &operator=(const SafeZoneOverlay &) = delete;

	// Loads (image mode) or generates (custom mode) the GPU texture.
	bool loadTexture();
	void freeTexture();

	// Generates a transparent RGBA pixel buffer with an outline rectangle
	// at the configured margins. Caller must bfree() the result.
	static uint8_t *generateCustomPixels(uint32_t w, uint32_t h, int top,
					     int bottom, int left, int right);

	static void drawCallback(void *data, uint32_t cx, uint32_t cy);

	// Tracked via QPointer so that if Qt destroys the preview widget
	// before we get a chance to disable, our pointer auto-nulls and we
	// can safely skip touching the now-freed OBSBasicPreview shim.
	QPointer<QWidget> m_previewWidget;
	// Cached for identity comparison only — never dereference at teardown.
	obs_display_t *m_previewDisplay = nullptr;

	// m_imageFile is non-null when the texture came from gs_image_file4
	// (image mode). m_textureIsGenerated is true when the texture was
	// created directly with gs_texture_create (custom mode).
	void *m_imageFile = nullptr;
	gs_texture_t *m_texture = nullptr;
	uint32_t m_textureWidth = 0;
	uint32_t m_textureHeight = 0;
	bool m_textureIsGenerated = false;

	static SafeZoneOverlay *s_instance;

	// --- Global state (persisted across enable/disable cycles) ---
	static std::string s_imageFile; // bare filename, image mode

	// Custom mode
	static bool s_customEnabled;
	static int s_marginTop;    // % of canvas height, [0, 50]
	static int s_marginBottom; // % of canvas height, [0, 50]
	static int s_marginLeft;   // % of canvas width,  [0, 50]
	static int s_marginRight;  // % of canvas width,  [0, 50]
};
