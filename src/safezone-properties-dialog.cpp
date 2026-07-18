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

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>

namespace {

constexpr const char *kConfigSection = "SafeZoneOverlay";
constexpr const char *kConfigImageFileKey = "ImageFile";
constexpr const char *kConfigCustomEnabledKey = "CustomEnabled";
constexpr const char *kConfigMarginTopKey = "MarginTop";
constexpr const char *kConfigMarginBottomKey = "MarginBottom";
constexpr const char *kConfigMarginLeftKey = "MarginLeft";
constexpr const char *kConfigMarginRightKey = "MarginRight";

void saveSettings(const std::string &imageFile, bool customEnabled,
		  int mTop, int mBottom, int mLeft, int mRight)
{
	config_t *cfg = obs_frontend_get_user_config();
	if (!cfg)
		return;
	config_set_string(cfg, kConfigSection, kConfigImageFileKey,
			  imageFile.c_str());
	config_set_bool(cfg, kConfigSection, kConfigCustomEnabledKey,
			customEnabled);
	config_set_int(cfg, kConfigSection, kConfigMarginTopKey, mTop);
	config_set_int(cfg, kConfigSection, kConfigMarginBottomKey, mBottom);
	config_set_int(cfg, kConfigSection, kConfigMarginLeftKey, mLeft);
	config_set_int(cfg, kConfigSection, kConfigMarginRightKey, mRight);
	config_save_safe(cfg, "tmp", nullptr);
}

// Strip the extension, replace hyphens/underscores with spaces, title-case.
// e.g. "action-safe.png" -> "Action Safe"
QString friendlyName(const QString &filename)
{
	QString base = QFileInfo(filename).baseName();
	base.replace('-', ' ');
	base.replace('_', ' ');
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

QSpinBox *makeMarginSpin(QWidget *parent)
{
	auto *sb = new QSpinBox(parent);
	sb->setRange(0, 50);
	sb->setSuffix(QStringLiteral(" %"));
	sb->setFixedWidth(72);
	return sb;
}

} // namespace

SafeZonePropertiesDialog::SafeZonePropertiesDialog(QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(QStringLiteral("SafeZone Overlay — Properties"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	setMinimumWidth(400);

	// Snapshot current live values so Cancel can restore them.
	m_originalImageFile = SafeZoneOverlay::imageFile();
	m_originalCustomEnabled = SafeZoneOverlay::isCustomEnabled();
	m_originalMarginTop = SafeZoneOverlay::customMarginTop();
	m_originalMarginBottom = SafeZoneOverlay::customMarginBottom();
	m_originalMarginLeft = SafeZoneOverlay::customMarginLeft();
	m_originalMarginRight = SafeZoneOverlay::customMarginRight();

	// =========================================================
	// Image row
	// =========================================================
	auto *imageLabel = new QLabel(QStringLiteral("Safe Zone Image:"), this);

	m_imageCombo = new QComboBox(this);
	m_imageCombo->setToolTip(
		QStringLiteral("Select a safe-zone guide image from the plugin's data folder.\n"
			       "Drop any PNG into data/ and it will appear here."));

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
		m_imageCombo->addItem(
			QStringLiteral("(no images found in data/)"),
			QVariant(QString()));
		m_imageCombo->setEnabled(false);
	} else {
		m_imageCombo->setCurrentIndex(selectedIndex);
	}

	// =========================================================
	// Custom safe-zone checkbox
	// =========================================================
	m_customCheck = new QCheckBox(
		QStringLiteral("Custom Safe Zone"), this);
	m_customCheck->setChecked(m_originalCustomEnabled);
	m_customCheck->setToolTip(
		QStringLiteral("Draw a custom safe-zone rectangle using the\n"
			       "margins below instead of an image file."));

	// =========================================================
	// Custom margin group box
	// =========================================================
	m_customGroup = new QGroupBox(this);
	m_customGroup->setFlat(true);

	m_marginTop = makeMarginSpin(m_customGroup);
	m_marginTop->setValue(m_originalMarginTop);
	m_marginTop->setToolTip(QStringLiteral("Top margin (% of canvas height)"));

	m_marginBottom = makeMarginSpin(m_customGroup);
	m_marginBottom->setValue(m_originalMarginBottom);
	m_marginBottom->setToolTip(QStringLiteral("Bottom margin (% of canvas height)"));

	m_marginLeft = makeMarginSpin(m_customGroup);
	m_marginLeft->setValue(m_originalMarginLeft);
	m_marginLeft->setToolTip(QStringLiteral("Left margin (% of canvas width)"));

	m_marginRight = makeMarginSpin(m_customGroup);
	m_marginRight->setValue(m_originalMarginRight);
	m_marginRight->setToolTip(QStringLiteral("Right margin (% of canvas width)"));

	auto *marginForm = new QFormLayout(m_customGroup);
	marginForm->setLabelAlignment(Qt::AlignRight);
	marginForm->addRow(QStringLiteral("Top:"),    m_marginTop);
	marginForm->addRow(QStringLiteral("Bottom:"), m_marginBottom);
	marginForm->addRow(QStringLiteral("Left:"),   m_marginLeft);
	marginForm->addRow(QStringLiteral("Right:"),  m_marginRight);


	// =========================================================
	// Description
	// =========================================================
	auto *descLabel = new QLabel(
		QStringLiteral(
			"<small><i>The safe-zone guide is visible only on the "
			"OBS preview — never recorded or streamed.<br>"
			"Add PNG files to the plugin's <b>data/</b> folder to "
			"create new image presets.</i></small>"),
		this);
	descLabel->setWordWrap(true);

	// =========================================================
	// Button box
	// =========================================================
	auto *buttonBox = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	// =========================================================
	// Main layout
	// =========================================================
	auto *topForm = new QFormLayout();
	topForm->setLabelAlignment(Qt::AlignRight);
	topForm->addRow(imageLabel, m_imageCombo);
	topForm->addRow(m_customCheck, m_customGroup);


	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->addLayout(topForm);
	mainLayout->addSpacing(4);
	mainLayout->addWidget(descLabel);
	mainLayout->addSpacing(8);
	mainLayout->addWidget(buttonBox);

	// Apply initial enabled state.
	updateCustomGroupEnabled(m_originalCustomEnabled);

	// =========================================================
	// Connections
	// =========================================================
	connect(m_imageCombo,
		QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&SafeZonePropertiesDialog::onImageChanged);
	connect(m_customCheck, &QCheckBox::toggled, this,
		&SafeZonePropertiesDialog::onCustomToggled);

	// Any margin spinbox change → onMarginsChanged
	connect(m_marginTop,
		QOverload<int>::of(&QSpinBox::valueChanged), this,
		&SafeZonePropertiesDialog::onMarginsChanged);
	connect(m_marginBottom,
		QOverload<int>::of(&QSpinBox::valueChanged), this,
		&SafeZonePropertiesDialog::onMarginsChanged);
	connect(m_marginLeft,
		QOverload<int>::of(&QSpinBox::valueChanged), this,
		&SafeZonePropertiesDialog::onMarginsChanged);
	connect(m_marginRight,
		QOverload<int>::of(&QSpinBox::valueChanged), this,
		&SafeZonePropertiesDialog::onMarginsChanged);



	connect(buttonBox, &QDialogButtonBox::accepted, this,
		&SafeZonePropertiesDialog::onAccepted);
	connect(buttonBox, &QDialogButtonBox::rejected, this,
		&SafeZonePropertiesDialog::onRejected);
}

// ---------------------------------------------------------------------------
// Private helper
// ---------------------------------------------------------------------------

void SafeZonePropertiesDialog::updateCustomGroupEnabled(bool customActive)
{
	// When custom is active: margin controls on, image combo off.
	// When custom is inactive: image combo on, margin controls off.
	if (m_imageCombo)
		m_imageCombo->setEnabled(!customActive &&
					 m_imageCombo->count() > 0 &&
					 m_imageCombo->itemData(0).toString() !=
						 QString());
	if (m_customGroup)
		m_customGroup->setEnabled(customActive);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void SafeZonePropertiesDialog::onImageChanged(int index)
{
	if (!m_imageCombo)
		return;
	const QVariant data = m_imageCombo->itemData(index);
	if (!data.isValid() || data.toString().isEmpty())
		return;
	SafeZoneOverlay::setImageFile(data.toString().toStdString());
}

void SafeZonePropertiesDialog::onCustomToggled(bool checked)
{
	SafeZoneOverlay::setCustomEnabled(checked);
	updateCustomGroupEnabled(checked);

	// If switching to image mode and a valid image is selected, apply it.
	if (!checked && m_imageCombo && m_imageCombo->count() > 0) {
		const QVariant data =
			m_imageCombo->itemData(m_imageCombo->currentIndex());
		if (data.isValid() && !data.toString().isEmpty())
			SafeZoneOverlay::setImageFile(
				data.toString().toStdString());
	}
}

void SafeZonePropertiesDialog::onMarginsChanged()
{
	if (!m_marginTop)
		return;
	SafeZoneOverlay::setCustomMargins(
		m_marginTop->value(), m_marginBottom->value(),
		m_marginLeft->value(), m_marginRight->value());
}


void SafeZonePropertiesDialog::onAccepted()
{
	saveSettings(SafeZoneOverlay::imageFile(),
		     SafeZoneOverlay::isCustomEnabled(),
		     SafeZoneOverlay::customMarginTop(),
		     SafeZoneOverlay::customMarginBottom(),
		     SafeZoneOverlay::customMarginLeft(),
		     SafeZoneOverlay::customMarginRight());
	accept();
}

void SafeZonePropertiesDialog::onRejected()
{
	// Restore everything to the state at dialog open.
	SafeZoneOverlay::setCustomEnabled(m_originalCustomEnabled);
	SafeZoneOverlay::setCustomMargins(
		m_originalMarginTop, m_originalMarginBottom,
		m_originalMarginLeft, m_originalMarginRight);
	SafeZoneOverlay::setImageFile(m_originalImageFile);

	reject();
}
