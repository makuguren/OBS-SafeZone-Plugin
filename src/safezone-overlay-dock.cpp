/*
SafeZone Overlay for OBS - Dock
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

#include "safezone-overlay-dock.hpp"
#include "safezone-overlay.hpp"

#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <plugin-support.h>

#include <QMetaObject>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// OBS stores per-user settings in the global user config (the same file
// that holds dock layout, confirm-on-exit, etc.). We keep our "was the
// overlay enabled at last shutdown?" flag under a dedicated section so
// we don't collide with anything OBS owns.
constexpr const char *kConfigSection = "SafeZoneOverlay";
constexpr const char *kConfigEnabledKey = "Enabled";

// The preview's obs_display_t is created lazily (see
// OBSQTDisplay::CreateDisplay, triggered from visibleChanged / paintEvent /
// resizeEvent), so it can legitimately be null for the first few hundred
// milliseconds after OBS_FRONTEND_EVENT_FINISHED_LOADING fires. Poll for
// up to ~10 s before giving up on auto-restore.
constexpr int kRestorePollIntervalMs = 100;
constexpr int kRestoreMaxAttempts = 100;

bool loadSavedEnabled()
{
	config_t *cfg = obs_frontend_get_user_config();
	if (!cfg)
		return false;
	return config_get_bool(cfg, kConfigSection, kConfigEnabledKey);
}

void saveEnabled(bool enabled)
{
	config_t *cfg = obs_frontend_get_user_config();
	if (!cfg)
		return;
	config_set_bool(cfg, kConfigSection, kConfigEnabledKey, enabled);
	// OBS flushes user config on shutdown; an explicit save here makes
	// the setting survive an unclean exit (e.g. a crash) as well.
	config_save_safe(cfg, "tmp", nullptr);
}

} // namespace

SafeZoneOverlayDock::SafeZoneOverlayDock(QWidget *parent) : QWidget(parent)
{
	setObjectName("SafeZoneOverlayDock");

	// The dock is built inside obs_module_post_load(), which runs before
	// the preview's obs_display_t exists. We can't actually call
	// SafeZoneOverlay::enable() yet - that has to wait until after
	// OBS_FRONTEND_EVENT_FINISHED_LOADING AND until Qt has had a chance
	// to expose/paint the preview (which is what triggers
	// OBSQTDisplay::CreateDisplay). For now we only reflect the saved
	// state in the button and defer the real enable() until
	// tryRestoreOverlay() succeeds.
	const bool savedEnabled = loadSavedEnabled();

	m_toggleButton = new QPushButton(this);
	m_toggleButton->setCheckable(true);
	m_toggleButton->setChecked(savedEnabled);
	setButtonEnabledText(savedEnabled);
	m_toggleButton->setToolTip(QStringLiteral(
		"Overlay a safe-zone guide on the preview. "
		"The overlay is rendered only on the preview and never "
		"appears in recordings, streams, or projectors."));

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(8);
	layout->addWidget(m_toggleButton);
	layout->addStretch(1);

	connect(m_toggleButton, &QPushButton::toggled, this,
		&SafeZoneOverlayDock::onToggle);

	// Polling timer used to retry auto-enabling on startup while the
	// preview's obs_display_t is still being constructed. Kicked off by
	// frontendEventCb() when FINISHED_LOADING fires.
	m_restoreTimer = new QTimer(this);
	m_restoreTimer->setInterval(kRestorePollIntervalMs);
	connect(m_restoreTimer, &QTimer::timeout, this,
		&SafeZoneOverlayDock::tryRestoreOverlay);

	obs_frontend_add_event_callback(
		&SafeZoneOverlayDock::frontendEventCb, this);
}

SafeZoneOverlayDock::~SafeZoneOverlayDock()
{
	obs_frontend_remove_event_callback(
		&SafeZoneOverlayDock::frontendEventCb, this);
	SafeZoneOverlay::disable();
}

void SafeZoneOverlayDock::frontendEventCb(enum obs_frontend_event event,
					  void *data)
{
	if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING)
		return;

	auto *self = static_cast<SafeZoneOverlayDock *>(data);
	// Marshal onto the Qt main thread via a queued invocation so we
	// don't call into the overlay from inside OBS's event dispatcher.
	QMetaObject::invokeMethod(self, "tryRestoreOverlay",
				  Qt::QueuedConnection);
}

void SafeZoneOverlayDock::tryRestoreOverlay()
{
	// Nothing to restore if the user didn't have the overlay enabled
	// at last shutdown, or if it's already running.
	if (!m_toggleButton || !m_toggleButton->isChecked()) {
		if (m_restoreTimer && m_restoreTimer->isActive())
			m_restoreTimer->stop();
		return;
	}
	if (SafeZoneOverlay::isEnabled()) {
		if (m_restoreTimer && m_restoreTimer->isActive())
			m_restoreTimer->stop();
		return;
	}

	if (SafeZoneOverlay::enable()) {
		if (m_restoreTimer && m_restoreTimer->isActive())
			m_restoreTimer->stop();
		m_restoreAttempts = 0;
		return;
	}

	// enable() failed. The usual reason during startup is that the
	// preview's obs_display_t has not been created yet (Qt hasn't
	// exposed/painted the preview widget). Keep polling until it shows
	// up, or give up after kRestoreMaxAttempts.
	if (++m_restoreAttempts >= kRestoreMaxAttempts) {
		if (m_restoreTimer && m_restoreTimer->isActive())
			m_restoreTimer->stop();
		obs_log(LOG_WARNING,
			"SafeZone Overlay: auto-restore gave up after %d "
			"attempts; preview display never became ready",
			m_restoreAttempts);
		m_restoreAttempts = 0;
		// IMPORTANT: do NOT write Enabled=false here. The user
		// explicitly turned the overlay on, so we leave the saved
		// state intact and simply leave the button checked - toggling
		// it off and on again, or restarting OBS, will retry.
		return;
	}

	if (m_restoreTimer && !m_restoreTimer->isActive())
		m_restoreTimer->start();
}

void SafeZoneOverlayDock::setButtonEnabledText(bool enabled)
{
	m_toggleButton->setText(
		enabled ? QStringLiteral("Disable SafeZone Overlay")
			: QStringLiteral("Enable SafeZone Overlay"));
}

void SafeZoneOverlayDock::onToggle(bool checked)
{
	if (checked) {
		if (SafeZoneOverlay::enable()) {
			setButtonEnabledText(true);
			saveEnabled(true);
		} else {
			m_toggleButton->blockSignals(true);
			m_toggleButton->setChecked(false);
			m_toggleButton->blockSignals(false);
			setButtonEnabledText(false);
			saveEnabled(false);
		}
	} else {
		SafeZoneOverlay::disable();
		setButtonEnabledText(false);
		saveEnabled(false);
	}
}
