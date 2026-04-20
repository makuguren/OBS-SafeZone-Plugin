/*
SafeZone Overlay for OBS
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

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include "safezone-overlay-dock.hpp"
#include "safezone-overlay.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static void register_safezone_overlay_dock()
{
	auto *dock = new SafeZoneOverlayDock();
	dock->setWindowTitle(QStringLiteral("SafeZone Overlay"));

	const bool added = obs_frontend_add_dock_by_id(
		"obs-safezone-overlay-dock", "SafeZone Overlay", dock);

	if (!added) {
		obs_log(LOG_WARNING,
			"failed to register SafeZone Overlay dock");
		delete dock;
		return;
	}

	obs_log(LOG_INFO, "SafeZone Overlay dock registered");
}

static void frontend_event_cb(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		// Release the overlay's GPU texture here, while OBS's
		// graphics subsystem is still initialised. disable() is
		// self-protecting: by the time EXIT fires, OBSBasic::closeEvent
		// has already called ui->preview->DestroyDisplay(), so the
		// preview's obs_display_t is gone - disable() detects that
		// and skips the now-unsafe obs_display_remove_draw_callback.
		SafeZoneOverlay::disable();
		obs_frontend_remove_event_callback(frontend_event_cb, nullptr);
	}
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	obs_frontend_add_event_callback(frontend_event_cb, nullptr);
	return true;
}

// Runs from OBSBasic::OBSInit() *before* the main window calls
// restoreState() on its saved dock layout (and before
// OBS_FRONTEND_EVENT_FINISHED_LOADING fires). Registering here is what
// lets Qt's dock-layout restore pick up this dock by objectName and put
// it back where the user last had it - pinned, floating, tabbed, hidden,
// whatever. If we waited until FINISHED_LOADING, restoreState() would
// have already run without seeing our dock, the dock would be dropped
// from the layout, and the next closeEvent would save a layout that no
// longer contains it - which is why it used to vanish from the Docks
// menu after one restart.
void obs_module_post_load(void)
{
	register_safezone_overlay_dock();
}

void obs_module_unload(void)
{
	// Safety net: by this point OBS_FRONTEND_EVENT_EXIT has already run
	// and disable() is idempotent, but call it again in case the plugin
	// is being unloaded outside of a normal frontend shutdown.
	SafeZoneOverlay::disable();
	obs_log(LOG_INFO, "plugin unloaded");
}
