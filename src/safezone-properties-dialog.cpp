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

#include "safezone-properties-dialog.hpp"
#include "safezone-overlay.hpp"

#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>

namespace {

constexpr const char *kConfigSection = "SafeZoneOverlay";
constexpr const char *kConfigImageFileKey = "ImageFile";
constexpr const char *kConfigOpacityKey = "Opacity";

void saveSettings(const std::string &imageFile, float opacity)
{
	config_t *cfg = obs_frontend_get_user_config();
	if (!cfg)
		return;
	config_set_string(cfg, kConfigSection, kConfigImageFileKey,
			  imageFile.c_str());
	config_set_double(cfg, kConfigSection, kConfigOpacityKey,
			  double(opacity));
	config_save_safe(cfg, "tmp", nullptr);
}

// Strip the extension and replace hyphens/underscores with spaces, then
// title-case the result for a friendlier dropdown label.
// e.g. "action-safe.png" -> "Action Safe"
//      "ebu_r95.png"      -> "Ebu R95"
QString friendlyName(const QString &filename)
{
	QString base = QFileInfo(filename).baseName();
	base.replace('-', ' ');
	base.replace('_', ' ');

	// Title-case: capitalise first letter of each word.
	bool capitalise = true;
	for (int i = 0; i < base.size(); ++i) {
		if (base[i] == ' ') {
			capitalise = true;
		} else if (capitalise) {
			base[i] = base[i].toUpper();
			capitalise = false;
		}
	}
	return base;
}

} // namespace

SafeZonePropertiesDialog::SafeZonePropertiesDialog(QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(QStringLiteral("SafeZone Overlay — Properties"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	setMinimumWidth(360);

	// Snapshot current live values so Cancel can restore them.
	m_originalImageFile = SafeZoneOverlay::imageFile();
	m_originalOpacity = SafeZoneOverlay::opacity();
	const int initialPct = int(m_originalOpacity * 100.0f + 0.5f);

	// ---- Image / preset row ----
	auto *imageLabel = new QLabel(QStringLiteral("Safe Zone:"), this);

	m_imageCombo = new QComboBox(this);
	m_imageCombo->setToolTip(
		QStringLiteral("Select a safe-zone guide image from the plugin's data folder.\n"
			       "Drop any PNG into the data/ directory and it will appear here."));

	// Populate from whatever PNGs are currently in data/.
	const auto files = SafeZoneOverlay::availableImageFiles();
	int selectedIndex = 0;
	for (int i = 0; i < static_cast<int>(files.size()); ++i) {
		const QString filename = QString::fromStdString(files[i]);
		m_imageCombo->addItem(friendlyName(filename),
				      QVariant(filename));
		if (files[i] == m_originalImageFile)
			selectedIndex = i;
	}

	if (m_imageCombo->count() == 0) {
		// No PNGs found — show a placeholder so the dialog still opens.
		m_imageCombo->addItem(
			QStringLiteral("(no images found in data/)"),
			QVariant(QString()));
		m_imageCombo->setEnabled(false);
	} else {
		m_imageCombo->setCurrentIndex(selectedIndex);
	}

	// ---- Opacity row ----
	auto *opacityLabel = new QLabel(QStringLiteral("Opacity:"), this);
	opacityLabel->setToolTip(
		QStringLiteral("Adjust how transparent the safe-zone guide appears.\n"
			       "100 % = fully opaque, 0 % = invisible."));

	m_opacitySlider = new QSlider(Qt::Horizontal, this);
	m_opacitySlider->setRange(0, 100);
	m_opacitySlider->setValue(initialPct);
	m_opacitySlider->setTickPosition(QSlider::TicksBelow);
	m_opacitySlider->setTickInterval(10);

	m_opacitySpin = new QSpinBox(this);
	m_opacitySpin->setRange(0, 100);
	m_opacitySpin->setValue(initialPct);
	m_opacitySpin->setSuffix(QStringLiteral(" %"));
	m_opacitySpin->setFixedWidth(72);

	auto *opacityRow = new QHBoxLayout();
	opacityRow->addWidget(m_opacitySlider);
	opacityRow->addWidget(m_opacitySpin);

	// ---- Description ----
	auto *descLabel = new QLabel(
		QStringLiteral(
			"<small><i>The safe-zone guide is visible only on the "
			"OBS preview. It is never recorded or streamed.<br>"
			"Add PNG files to the plugin's <b>data/</b> folder to "
			"create new presets.</i></small>"),
		this);
	descLabel->setWordWrap(true);

	// ---- Button box ----
	auto *buttonBox = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	// ---- Main layout ----
	auto *form = new QFormLayout();
	form->setLabelAlignment(Qt::AlignRight);
	form->addRow(imageLabel, m_imageCombo);
	form->addRow(opacityLabel, opacityRow);

	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->addLayout(form);
	mainLayout->addSpacing(4);
	mainLayout->addWidget(descLabel);
	mainLayout->addSpacing(8);
	mainLayout->addWidget(buttonBox);

	// ---- Connections ----
	connect(m_imageCombo,
		QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&SafeZonePropertiesDialog::onImageChanged);
	connect(m_opacitySlider, &QSlider::valueChanged, this,
		&SafeZonePropertiesDialog::onOpacitySliderChanged);
	connect(m_opacitySpin, QOverload<int>::of(&QSpinBox::valueChanged),
		this, &SafeZonePropertiesDialog::onOpacitySpinChanged);

	connect(buttonBox, &QDialogButtonBox::accepted, this,
		&SafeZonePropertiesDialog::onAccepted);
	connect(buttonBox, &QDialogButtonBox::rejected, this,
		&SafeZonePropertiesDialog::onRejected);
}

void SafeZonePropertiesDialog::onImageChanged(int index)
{
	if (!m_imageCombo)
		return;
	const QVariant data = m_imageCombo->itemData(index);
	if (!data.isValid() || data.toString().isEmpty())
		return;
	// Apply live so the user can see the overlay change immediately.
	SafeZoneOverlay::setImageFile(data.toString().toStdString());
}

void SafeZonePropertiesDialog::onOpacitySliderChanged(int value)
{
	m_opacitySpin->blockSignals(true);
	m_opacitySpin->setValue(value);
	m_opacitySpin->blockSignals(false);

	SafeZoneOverlay::setOpacity(float(value) / 100.0f);
}

void SafeZonePropertiesDialog::onOpacitySpinChanged(int value)
{
	m_opacitySlider->blockSignals(true);
	m_opacitySlider->setValue(value);
	m_opacitySlider->blockSignals(false);

	SafeZoneOverlay::setOpacity(float(value) / 100.0f);
}

void SafeZonePropertiesDialog::onAccepted()
{
	saveSettings(SafeZoneOverlay::imageFile(), SafeZoneOverlay::opacity());
	accept();
}

void SafeZonePropertiesDialog::onRejected()
{
	// Restore the image and opacity to what they were when the dialog opened.
	SafeZoneOverlay::setImageFile(m_originalImageFile);
	SafeZoneOverlay::setOpacity(m_originalOpacity);
	reject();
}
