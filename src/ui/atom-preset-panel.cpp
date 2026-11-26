/*
Atom - Particle system plugin for OBS Studio
Copyright (C) 2026 Voidscape Development

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

#include "atom-preset-panel.hpp"
#include "atom-preview.hpp"

#include "obs/atom-preset-store.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <obs-module.h>

namespace atom {
namespace ui {

namespace {

const char *const kCurrentId = "@current";
const char *const kOpenedId = "@opened";
const QSize kThumbnailSize(150, 94);

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

} // namespace

PresetPanel::PresetPanel(QWidget *parent) : QFrame(parent)
{
	setObjectName("atomPresetPanel");
	setFrameShape(QFrame::StyledPanel);
	setFrameShadow(QFrame::Raised);
	// OBS themes style every widget, which overrides palette fills and left this overlay see-through,
	// so the background is painted by a stylesheet of its own.
	setAttribute(Qt::WA_StyledBackground, true);
	setStyleSheet("#atomPresetPanel { background-color: palette(window); border: 1px solid palette(mid);"
		      " border-radius: 6px; }");

	QLabel *title = new QLabel(text("Atom.Designer.Presets.Title"), this);
	QFont titleFont = title->font();
	titleFont.setBold(true);
	titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
	title->setFont(titleFont);

	QLabel *hint = new QLabel(text("Atom.Designer.Presets.Hint"), this);
	hint->setWordWrap(true);
	hint->setEnabled(false);

	category_ = new QComboBox(this);
	for (const auto &entry : presetCategories())
		category_->addItem(text(entry.second.c_str()), QString::fromStdString(entry.first));

	list_ = new QListWidget(this);
	list_->setObjectName("atomPresetList");
	list_->setViewMode(QListView::IconMode);
	list_->setIconSize(kThumbnailSize);
	list_->setGridSize(QSize(kThumbnailSize.width() + 18, kThumbnailSize.height() + 44));
	list_->setResizeMode(QListView::Adjust);
	list_->setMovement(QListView::Static);
	list_->setWordWrap(true);
	list_->setSpacing(4);

	status_ = new QLabel(this);
	status_->setWordWrap(true);

	delete_ = new QPushButton(text("Atom.Designer.DeletePreset"), this);
	QPushButton *cancel = new QPushButton(text("Atom.Designer.Cancel"), this);
	use_ = new QPushButton(text("Atom.Designer.Presets.Use"), this);
	use_->setObjectName("atomPresetUse");
	use_->setDefault(true);

	QHBoxLayout *filter = new QHBoxLayout();
	filter->addWidget(new QLabel(text("Atom.Designer.Presets.Show"), this));
	filter->addWidget(category_, 1);

	QHBoxLayout *buttons = new QHBoxLayout();
	buttons->addWidget(delete_);
	buttons->addStretch(1);
	buttons->addWidget(cancel);
	buttons->addWidget(use_);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(title);
	layout->addWidget(hint);
	layout->addLayout(filter);
	layout->addWidget(list_, 1);
	layout->addWidget(status_);
	layout->addLayout(buttons);

	// Thumbnails are simulated, not stored, so a preset always previews what it will do. One is
	// rendered per pass of the event loop: the panel opens at once and fills in as it goes.
	thumbnailTimer_ = new QTimer(this);
	thumbnailTimer_->setInterval(0);
	connect(thumbnailTimer_, &QTimer::timeout, this, [this] { renderNextThumbnail(); });

	connect(category_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { rebuildList(); });
	connect(list_, &QListWidget::currentItemChanged, this, [this] { onSelectionChanged(); });
	connect(list_, &QListWidget::itemDoubleClicked, this, [this] { useSelection(); });
	connect(use_, &QPushButton::clicked, this, [this] { useSelection(); });
	connect(cancel, &QPushButton::clicked, this, [this] { this->cancel(); });
	connect(delete_, &QPushButton::clicked, this, [this] { deleteSelection(); });

	hide();
}

void PresetPanel::open(const EmitterConfig &current, const EmitterConfig &opened, Callbacks callbacks)
{
	current_ = current;
	opened_ = opened;
	callbacks_ = std::move(callbacks);

	// The two "this emitter" entries change between openings; presets do not.
	thumbnails_.remove(kCurrentId);
	thumbnails_.remove(kOpenedId);

	rebuildList();
	show();
	raise();
	list_->setFocus();
}

void PresetPanel::cancel()
{
	if (!isVisible())
		return;
	thumbnailTimer_->stop();
	hide();
	if (callbacks_.cancelled)
		callbacks_.cancelled();
}

const EmitterConfig *PresetPanel::configFor(const QString &id) const
{
	if (id == kCurrentId)
		return nullptr;
	if (id == kOpenedId)
		return &opened_;
	const AtomPreset *preset = PresetStore::instance().find(id.toStdString());
	return preset ? &preset->config : nullptr;
}

void PresetPanel::rebuildList()
{
	const QString previous = list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString()
						      : QString(kCurrentId);
	const QString category = category_->currentData().toString();

	list_->blockSignals(true);
	list_->clear();
	pending_.clear();

	const auto add = [this, &previous](const QString &id, const QString &label, const QString &tip) {
		QListWidgetItem *item = new QListWidgetItem(label);
		item->setData(Qt::UserRole, id);
		item->setToolTip(tip);
		item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
		const auto cached = thumbnails_.find(id);
		if (cached != thumbnails_.end()) {
			item->setIcon(cached.value());
		} else {
			QPixmap blank(kThumbnailSize);
			blank.fill(QColor(24, 24, 28));
			item->setIcon(blank);
			pending_.append(id);
		}
		list_->addItem(item);
		if (id == previous)
			list_->setCurrentItem(item);
	};

	add(kCurrentId, text("Atom.Designer.Presets.Current"), text("Atom.Designer.Presets.Current.Tip"));
	add(kOpenedId, text("Atom.Designer.Presets.Opened"), text("Atom.Designer.Presets.Opened.Tip"));

	for (const AtomPreset &preset : PresetStore::instance().presets()) {
		if (category != "all" && !category.isEmpty() && QString::fromStdString(preset.category) != category)
			continue;
		add(QString::fromStdString(preset.id), QString::fromStdString(preset.name),
		    preset.description.empty() ? QString::fromStdString(preset.name)
					       : text(preset.description.c_str()));
	}

	if (!list_->currentItem())
		list_->setCurrentRow(0);
	list_->blockSignals(false);

	onSelectionChanged();
	if (!pending_.isEmpty())
		thumbnailTimer_->start();
}

void PresetPanel::renderNextThumbnail()
{
	if (pending_.isEmpty()) {
		thumbnailTimer_->stop();
		return;
	}

	const QString id = pending_.takeFirst();
	const EmitterConfig *source = id == kCurrentId ? &current_ : configFor(id);
	if (!source)
		return;

	EmitterConfig config = *source;
	config.emission.width = static_cast<uint32_t>(kThumbnailSize.width() * 2);
	config.emission.height = static_cast<uint32_t>(kThumbnailSize.height() * 2);
	config.emission.prewarm = true;
	const QPixmap thumbnail = QPixmap::fromImage(renderPresetThumbnail(config, kThumbnailSize));
	thumbnails_.insert(id, thumbnail);

	for (int i = 0; i < list_->count(); ++i) {
		if (list_->item(i)->data(Qt::UserRole).toString() == id)
			list_->item(i)->setIcon(thumbnail);
	}
}

void PresetPanel::onSelectionChanged()
{
	QListWidgetItem *item = list_->currentItem();
	const QString id = item ? item->data(Qt::UserRole).toString() : QString(kCurrentId);
	const bool isCurrent = id == kCurrentId;

	const AtomPreset *preset = PresetStore::instance().find(id.toStdString());
	delete_->setEnabled(preset && !preset->builtin);
	use_->setEnabled(!isCurrent);

	if (isCurrent)
		status_->setText(text("Atom.Designer.Presets.Status.Current"));
	else if (id == kOpenedId)
		status_->setText(text("Atom.Designer.Presets.Status.Opened"));
	else
		status_->setText(text("Atom.Designer.Presets.Status.Preset").arg(item ? item->text() : QString()));

	if (isVisible() && callbacks_.preview)
		callbacks_.preview(configFor(id));
}

void PresetPanel::useSelection()
{
	QListWidgetItem *item = list_->currentItem();
	if (!item)
		return;
	const QString id = item->data(Qt::UserRole).toString();
	const EmitterConfig *config = configFor(id);
	if (!config) {
		cancel();
		return;
	}

	// Copy first: applying the choice can reload the preset store.
	const EmitterConfig chosen = *config;
	const std::string presetId = id == kOpenedId ? std::string() : id.toStdString();
	thumbnailTimer_->stop();
	hide();
	if (callbacks_.use)
		callbacks_.use(chosen, presetId);
}

void PresetPanel::deleteSelection()
{
	QListWidgetItem *item = list_->currentItem();
	if (!item)
		return;
	const std::string id = item->data(Qt::UserRole).toString().toStdString();
	const AtomPreset *preset = PresetStore::instance().find(id);
	if (!preset || preset->builtin)
		return;

	const QMessageBox::StandardButton answer = QMessageBox::question(
		this, text("Atom.Designer.DeletePreset"),
		text("Atom.Designer.DeletePresetConfirm").arg(QString::fromStdString(preset->name)));
	if (answer != QMessageBox::Yes)
		return;

	PresetStore::instance().remove(id);
	thumbnails_.remove(QString::fromStdString(id));
	list_->setCurrentRow(0);
	rebuildList();
}

} // namespace ui
} // namespace atom
