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
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr const char *kConfigSection = "SafeZoneOverlay";
constexpr const char *kConfigImageFileKey = "ImageFile";
constexpr const char *kConfigCustomEnabledKey = "CustomEnabled";
constexpr const char *kConfigMarginTopKey = "MarginTop";
constexpr const char *kConfigMarginBottomKey = "MarginBottom";
constexpr const char *kConfigMarginLeftKey = "MarginLeft";
constexpr const char *kConfigMarginRightKey = "MarginRight";
constexpr const char *kConfigConstrainedSourcesKey = "ConstrainedSources";
constexpr const char *kConfigAutoClampKey = "AutoClampEnabled";

void saveSettings(const std::string &imageFile, bool customEnabled,
		  int mTop, int mBottom, int mLeft, int mRight,
		  const std::vector<std::string> &constrainedSources,
		  bool autoClamp)
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

	std::string sourcesJoined;
	for (size_t i = 0; i < constrainedSources.size(); ++i) {
		if (i > 0)
			sourcesJoined += ";";
		sourcesJoined += constrainedSources[i];
	}
	config_set_string(cfg, kConfigSection, kConfigConstrainedSourcesKey,
			  sourcesJoined.c_str());
	config_set_bool(cfg, kConfigSection, kConfigAutoClampKey, autoClamp);

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
	sb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	return sb;
}

static bool enumSceneItemNameCb(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *names = static_cast<std::vector<QString> *>(param);
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source)
		return true;

	const char *name = obs_source_get_name(source);
	if (!name)
		return true;

	QString qName = QString::fromUtf8(name);
	for (const auto &existing : *names) {
		if (existing == qName)
			return true;
	}
	names->push_back(qName);
	return true;
}

static std::vector<QString> getAvailableSceneSourceNames()
{
	std::vector<QString> names;
	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	if (!sceneSource)
		return names;

	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (scene) {
		obs_scene_enum_items(scene, enumSceneItemNameCb, &names);
	}
	obs_source_release(sceneSource);
	return names;
}

} // namespace

SafeZonePropertiesDialog::SafeZonePropertiesDialog(QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(QStringLiteral("SafeZone Overlay — Properties"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	setMinimumWidth(440);

	// Snapshot current live values so Cancel can restore them.
	m_originalImageFile = SafeZoneOverlay::imageFile();
	m_originalCustomEnabled = SafeZoneOverlay::isCustomEnabled();
	m_originalMarginTop = SafeZoneOverlay::customMarginTop();
	m_originalMarginBottom = SafeZoneOverlay::customMarginBottom();
	m_originalMarginLeft = SafeZoneOverlay::customMarginLeft();
	m_originalMarginRight = SafeZoneOverlay::customMarginRight();
	m_originalConstrainedSources = SafeZoneOverlay::constrainedSources();
	m_originalAutoClampEnabled = SafeZoneOverlay::isAutoClampEnabled();

	// =========================================================
	// Image row
	// =========================================================
	auto *imageLabel = new QLabel(QStringLiteral("Safe Zone Image:"), this);

	m_imageCombo = new QComboBox(this);
	m_imageCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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
	// Custom safe-zone group box
	// =========================================================
	m_customGroup = new QGroupBox(QStringLiteral("Custom Safe Zone"), this);

	m_customCheck = new QCheckBox(QStringLiteral("Enable Custom Safe Zone"), m_customGroup);
	m_customCheck->setChecked(m_originalCustomEnabled);
	m_customCheck->setToolTip(
		QStringLiteral("Draw a custom safe-zone rectangle using the\n"
			       "margins below instead of an image file."));

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

	auto *marginForm = new QFormLayout();
	marginForm->setLabelAlignment(Qt::AlignRight);
	marginForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	marginForm->setContentsMargins(0, 0, 0, 0);
	marginForm->setVerticalSpacing(4);
	marginForm->addRow(QStringLiteral("Top:"),    m_marginTop);
	marginForm->addRow(QStringLiteral("Bottom:"), m_marginBottom);
	marginForm->addRow(QStringLiteral("Left:"),   m_marginLeft);
	marginForm->addRow(QStringLiteral("Right:"),  m_marginRight);

	auto *customLayout = new QVBoxLayout(m_customGroup);
	customLayout->setContentsMargins(8, 8, 8, 8);
	customLayout->setSpacing(6);
	customLayout->addWidget(m_customCheck);
	customLayout->addLayout(marginForm);

	// =========================================================
	// Source Constraint Group Box
	// =========================================================
	m_constraintGroup = new QGroupBox(QStringLiteral("Safe Zone Source Constraints"), this);
	auto *constraintLayout = new QVBoxLayout(m_constraintGroup);

	auto *constraintInfo = new QLabel(
		QStringLiteral("<small>Select sources below that must stay inside the safe zone:</small>"),
		m_constraintGroup);
	constraintInfo->setWordWrap(true);

	m_sourcesRowsLayout = new QVBoxLayout();
	m_sourcesRowsLayout->setSpacing(4);

	populateInitialSourceRows();

	m_addSourceButton = new QPushButton(QStringLiteral("+ Add Source Constraint"), m_constraintGroup);
	m_addSourceButton->setToolTip(QStringLiteral("Add another source dropdown to constrain."));

	auto *btnRow = new QHBoxLayout();
	m_snapButton = new QPushButton(QStringLiteral("Snap Selected Sources Now"), m_constraintGroup);
	m_snapButton->setToolTip(QStringLiteral("Instantly shift selected sources to remain inside the safe zone."));
	btnRow->addWidget(m_snapButton);

	m_autoClampCheck = new QCheckBox(QStringLiteral("Automatically clamp selected sources"), m_constraintGroup);
	m_autoClampCheck->setChecked(m_originalAutoClampEnabled);
	m_autoClampCheck->setToolTip(QStringLiteral("Keep selected sources strictly inside the safe zone continuously."));

	constraintLayout->addWidget(m_autoClampCheck);
	constraintLayout->addWidget(constraintInfo);
	constraintLayout->addLayout(m_sourcesRowsLayout);
	constraintLayout->addWidget(m_addSourceButton);
	constraintLayout->addLayout(btnRow);

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
	// Scrollable Content & Main layout
	// =========================================================
	auto *imageForm = new QFormLayout();
	imageForm->setLabelAlignment(Qt::AlignRight);
	imageForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	imageForm->setContentsMargins(0, 0, 0, 0);
	imageForm->setVerticalSpacing(6);
	imageForm->addRow(imageLabel, m_imageCombo);

	auto *contentWidget = new QWidget(this);
	auto *contentLayout = new QVBoxLayout(contentWidget);
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setSpacing(8);
	contentLayout->addLayout(imageForm);
	contentLayout->addWidget(m_customGroup);
	contentLayout->addWidget(m_constraintGroup);
	contentLayout->addWidget(descLabel);
	contentLayout->addStretch(1);

	auto *scrollArea = new QScrollArea(this);
	scrollArea->setWidgetResizable(true);
	scrollArea->setFrameShape(QFrame::NoFrame);
	scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scrollArea->setWidget(contentWidget);

	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(8, 8, 8, 8);
	mainLayout->setSpacing(8);
	mainLayout->addWidget(scrollArea, 1);
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

	connect(m_addSourceButton, &QPushButton::clicked, this,
		&SafeZonePropertiesDialog::onAddSourceRowClicked);
	connect(m_snapButton, &QPushButton::clicked, this,
		&SafeZonePropertiesDialog::onSnapSourcesClicked);
	connect(m_autoClampCheck, &QCheckBox::toggled, this,
		&SafeZonePropertiesDialog::onAutoClampToggled);

	connect(buttonBox, &QDialogButtonBox::accepted, this,
		&SafeZonePropertiesDialog::onAccepted);
	connect(buttonBox, &QDialogButtonBox::rejected, this,
		&SafeZonePropertiesDialog::onRejected);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void SafeZonePropertiesDialog::updateCustomGroupEnabled(bool customActive)
{
	if (m_imageCombo)
		m_imageCombo->setEnabled(!customActive &&
					 m_imageCombo->count() > 0 &&
					 m_imageCombo->itemData(0).toString() !=
						 QString());

	if (m_marginTop)    m_marginTop->setEnabled(customActive);
	if (m_marginBottom) m_marginBottom->setEnabled(customActive);
	if (m_marginLeft)   m_marginLeft->setEnabled(customActive);
	if (m_marginRight)  m_marginRight->setEnabled(customActive);
}

void SafeZonePropertiesDialog::addSourceRow(const QString &selectedName)
{
	auto *rowWidget = new QWidget(m_constraintGroup);
	auto *rowLayout = new QHBoxLayout(rowWidget);
	rowLayout->setContentsMargins(0, 0, 0, 0);
	rowLayout->setSpacing(4);

	auto *combo = new QComboBox(rowWidget);
	combo->addItem(QStringLiteral("(Select a source...)"), QVariant(QString()));

	const auto sceneSources = getAvailableSceneSourceNames();
	int selectIdx = 0;
	for (int i = 0; i < static_cast<int>(sceneSources.size()); ++i) {
		combo->addItem(sceneSources[i], QVariant(sceneSources[i]));
		if (!selectedName.isEmpty() && sceneSources[i] == selectedName) {
			selectIdx = i + 1; // +1 because of placeholder
		}
	}
	combo->setCurrentIndex(selectIdx);

	auto *removeBtn = new QToolButton(rowWidget);
	removeBtn->setText(QStringLiteral("✕"));
	removeBtn->setFixedSize(26, 26);
	removeBtn->setToolTip(QStringLiteral("Remove this source constraint"));

	rowLayout->addWidget(combo, 1);
	rowLayout->addWidget(removeBtn);

	m_sourcesRowsLayout->addWidget(rowWidget);

	SourceRow sr;
	sr.rowWidget = rowWidget;
	sr.combo = combo;
	sr.removeBtn = removeBtn;
	m_sourceRows.push_back(sr);

	connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&SafeZonePropertiesDialog::onSourceComboChanged);
	connect(removeBtn, &QToolButton::clicked, this,
		&SafeZonePropertiesDialog::onRemoveSourceRowClicked);
}

void SafeZonePropertiesDialog::removeSourceRow(QWidget *rowWidget)
{
	if (!rowWidget)
		return;

	for (auto it = m_sourceRows.begin(); it != m_sourceRows.end(); ++it) {
		if (it->rowWidget == rowWidget) {
			m_sourcesRowsLayout->removeWidget(rowWidget);
			rowWidget->deleteLater();
			m_sourceRows.erase(it);
			break;
		}
	}
}

void SafeZonePropertiesDialog::populateInitialSourceRows()
{
	for (const auto &row : m_sourceRows) {
		if (row.rowWidget) {
			m_sourcesRowsLayout->removeWidget(row.rowWidget);
			delete row.rowWidget;
		}
	}
	m_sourceRows.clear();

	if (m_originalConstrainedSources.empty()) {
		addSourceRow();
	} else {
		for (const auto &src : m_originalConstrainedSources) {
			addSourceRow(QString::fromUtf8(src.c_str()));
		}
	}
}

std::vector<std::string> SafeZonePropertiesDialog::getSelectedSourcesFromList() const
{
	std::vector<std::string> res;
	for (const auto &row : m_sourceRows) {
		if (!row.combo)
			continue;
		const QString text = row.combo->currentText();
		if (row.combo->currentIndex() > 0 && !text.isEmpty()) {
			res.push_back(text.toUtf8().constData());
		}
	}
	return res;
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
	if (m_customCheck && m_customCheck->isChecked()) {
		m_customCheck->setChecked(false);
	}
}

void SafeZonePropertiesDialog::onCustomToggled(bool checked)
{
	SafeZoneOverlay::setCustomEnabled(checked);
	updateCustomGroupEnabled(checked);

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

void SafeZonePropertiesDialog::onAddSourceRowClicked()
{
	addSourceRow();
	onSourceComboChanged();
}

void SafeZonePropertiesDialog::onRemoveSourceRowClicked()
{
	auto *btn = qobject_cast<QToolButton *>(sender());
	if (!btn)
		return;

	for (const auto &row : m_sourceRows) {
		if (row.removeBtn == btn) {
			removeSourceRow(row.rowWidget);
			break;
		}
	}

	onSourceComboChanged();
}

void SafeZonePropertiesDialog::onSnapSourcesClicked()
{
	onSourceComboChanged();
	SafeZoneOverlay::snapConstrainedSourcesNow();
}

void SafeZonePropertiesDialog::onSourceComboChanged()
{
	SafeZoneOverlay::setConstrainedSources(getSelectedSourcesFromList());
}

void SafeZonePropertiesDialog::onAutoClampToggled(bool checked)
{
	onSourceComboChanged();
	SafeZoneOverlay::setAutoClampEnabled(checked);
}

void SafeZonePropertiesDialog::onAccepted()
{
	auto selectedSources = getSelectedSourcesFromList();
	SafeZoneOverlay::setConstrainedSources(selectedSources);
	SafeZoneOverlay::setAutoClampEnabled(m_autoClampCheck ? m_autoClampCheck->isChecked() : false);

	saveSettings(SafeZoneOverlay::imageFile(),
		     SafeZoneOverlay::isCustomEnabled(),
		     SafeZoneOverlay::customMarginTop(),
		     SafeZoneOverlay::customMarginBottom(),
		     SafeZoneOverlay::customMarginLeft(),
		     SafeZoneOverlay::customMarginRight(),
		     selectedSources,
		     SafeZoneOverlay::isAutoClampEnabled());
	accept();
}

void SafeZonePropertiesDialog::onRejected()
{
	SafeZoneOverlay::setCustomEnabled(m_originalCustomEnabled);
	SafeZoneOverlay::setCustomMargins(
		m_originalMarginTop, m_originalMarginBottom,
		m_originalMarginLeft, m_originalMarginRight);
	SafeZoneOverlay::setImageFile(m_originalImageFile);
	SafeZoneOverlay::setConstrainedSources(m_originalConstrainedSources);
	SafeZoneOverlay::setAutoClampEnabled(m_originalAutoClampEnabled);

	reject();
}
