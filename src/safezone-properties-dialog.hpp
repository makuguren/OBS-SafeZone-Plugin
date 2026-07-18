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

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QSpinBox;

/*
 * Modal settings dialog for the SafeZone Overlay plugin.
 *
 * Exposes:
 *   - Safe Zone image selector — auto-populated from *.png files in data/
 *   - Custom Safe Zone checkbox + four margin spinboxes (Top/Bottom/Left/Right)
 *
 * When the "Custom Safe Zone" checkbox is checked the image dropdown is
 * disabled and the margin controls become active, and vice-versa.
 * Changes are applied live; OK persists, Cancel restores.
 */
class SafeZonePropertiesDialog : public QDialog {
	Q_OBJECT

public:
	explicit SafeZonePropertiesDialog(QWidget *parent = nullptr);

private slots:
	void onImageChanged(int index);
	void onCustomToggled(bool checked);
	void onMarginsChanged();
	void onAccepted();
	void onRejected();

private:
	// Image mode controls
	QComboBox *m_imageCombo = nullptr;

	// Custom mode controls
	QCheckBox *m_customCheck = nullptr;
	QGroupBox *m_customGroup = nullptr;
	QSpinBox *m_marginTop = nullptr;
	QSpinBox *m_marginBottom = nullptr;
	QSpinBox *m_marginLeft = nullptr;
	QSpinBox *m_marginRight = nullptr;


	// Snapshots at dialog open so we can restore on Cancel.
	std::string m_originalImageFile;
	bool m_originalCustomEnabled = false;
	int m_originalMarginTop = 10;
	int m_originalMarginBottom = 10;
	int m_originalMarginLeft = 10;
	int m_originalMarginRight = 10;

	void updateCustomGroupEnabled(bool customActive);
};
