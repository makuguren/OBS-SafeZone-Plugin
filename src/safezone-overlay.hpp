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
 */
class SafeZoneOverlay {
public:
	// Enable the overlay: load the image, find the main preview display,
	// and register the draw callback. Returns true on success. Safe to
	// call when already enabled.
	static bool enable();

	// Disable the overlay: unregister the callback and release the
	// texture. Safe to call when already disabled.
	static void disable();

	// True when the overlay is currently active.
	static bool isEnabled();

private:
	SafeZoneOverlay();
	~SafeZoneOverlay();

	SafeZoneOverlay(const SafeZoneOverlay &) = delete;
	SafeZoneOverlay &operator=(const SafeZoneOverlay &) = delete;

	bool loadTexture();
	void freeTexture();

	static void drawCallback(void *data, uint32_t cx, uint32_t cy);

	// Tracked via QPointer so that if Qt destroys the preview widget
	// before we get a chance to disable, our pointer auto-nulls and we
	// can safely skip touching the now-freed OBSBasicPreview shim.
	QPointer<QWidget> m_previewWidget;
	// Cached for identity comparison only - NEVER dereference this
	// directly at teardown. OBS destroys the preview's obs_display_t in
	// OBSBasic::closeEvent via ui->preview->DestroyDisplay() (which
	// sets OBSBasicPreview::display = nullptr and frees the struct)
	// BEFORE OBS_FRONTEND_EVENT_EXIT is dispatched, so by the time any
	// teardown path runs this cached pointer is already dangling.
	// disable() re-reads the live value from preview->display and only
	// calls obs_display_remove_draw_callback if it still matches.
	obs_display_t *m_previewDisplay = nullptr;
	void *m_imageFile = nullptr; // gs_image_file4_t, opaque to avoid extra include
	gs_texture_t *m_texture = nullptr;
	uint32_t m_textureWidth = 0;
	uint32_t m_textureHeight = 0;

	static SafeZoneOverlay *s_instance;
};
