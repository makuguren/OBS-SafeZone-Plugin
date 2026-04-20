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

#pragma once

#include <obs-frontend-api.h>

#include <QWidget>

class QPushButton;
class QTimer;

class SafeZoneOverlayDock : public QWidget {
	Q_OBJECT

public:
	explicit SafeZoneOverlayDock(QWidget *parent = nullptr);
	~SafeZoneOverlayDock() override;

private slots:
	void onToggle(bool checked);
	// Invoked once OBS has finished loading. The preview's
	// obs_display_t is created lazily from Qt events (visibleChanged /
	// paintEvent / resizeEvent) and may not exist yet at this point, so
	// this slot retries via m_restoreTimer until enable() succeeds or
	// we time out.
	void tryRestoreOverlay();

private:
	static void frontendEventCb(enum obs_frontend_event event, void *data);

	void setButtonEnabledText(bool enabled);

	QPushButton *m_toggleButton = nullptr;
	// Poll timer used to retry enabling the overlay on startup while the
	// preview display is still being constructed by Qt.
	QTimer *m_restoreTimer = nullptr;
	int m_restoreAttempts = 0;
};
