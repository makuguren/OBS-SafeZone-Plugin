/*
SafeZone Overlay for OBS - Properties Dialog
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

#include <QDialog>

#include <string>

class QComboBox;
class QSlider;
class QLabel;
class QSpinBox;

/*
 * Modal settings dialog for the SafeZone Overlay plugin.
 *
 * Exposes:
 *   - Safe Zone image selector — auto-populated from *.png files in data/
 *   - Opacity slider (0–100 %)
 *
 * Changes are applied live so the user can preview them. OK persists to OBS
 * user config; Cancel restores the original values.
 */
class SafeZonePropertiesDialog : public QDialog {
	Q_OBJECT

public:
	explicit SafeZonePropertiesDialog(QWidget *parent = nullptr);

private slots:
	void onImageChanged(int index);
	void onOpacitySliderChanged(int value);
	void onOpacitySpinChanged(int value);
	void onAccepted();
	void onRejected();

private:
	QComboBox *m_imageCombo = nullptr;
	QSlider *m_opacitySlider = nullptr;
	QSpinBox *m_opacitySpin = nullptr;

	// Snapshots at dialog open so we can restore on Cancel.
	std::string m_originalImageFile;
	float m_originalOpacity = 1.0f;
};
