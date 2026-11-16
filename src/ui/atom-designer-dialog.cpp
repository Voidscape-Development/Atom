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

#include "atom-designer-dialog.hpp"
#include "atom-param-editor.hpp"
#include "atom-preset-panel.hpp"
#include "atom-preview.hpp"
#include "atom-value-editors.hpp"

#include "obs/atom-preset-store.hpp"
#include "obs/atom-source.hpp"

#include "atom-core/atom-modules.hpp"
#include "atom-core/atom-modulation.hpp"
#include "atom-core/atom-registry.hpp"

#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <obs-module.h>

#include <cmath>
#include <initializer_list>
#include <memory>

namespace atom {
namespace ui {

namespace {

/// How long live edits are gathered before being written to the source.
constexpr int kLiveApplyIntervalMs = 40;
/// Edits closer together than this fold into a single undo step.
constexpr int kUndoMergeMs = 700;
constexpr size_t kUndoLimit = 100;
/// Points used to show a built-in fade path as an editable curve.
constexpr int kFadeSamples = 9;

// Roles on outline rows.
constexpr int kKindRole = Qt::UserRole;
constexpr int kLayerRole = Qt::UserRole + 1;
constexpr int kPartRole = Qt::UserRole + 2;
constexpr int kIndexRole = Qt::UserRole + 3;

/// Strong reference to a weakly-held source for the duration of a scope.
struct SourceHold {
	obs_source_t *source = nullptr;

	explicit SourceHold(obs_weak_source_t *weak) : source(weak ? obs_weak_source_get_source(weak) : nullptr) {}
	~SourceHold()
	{
		if (source)
			obs_source_release(source);
	}

	explicit operator bool() const { return source != nullptr; }
};

QString text(const std::string &key)
{
	return QString::fromUtf8(obs_module_text(key.c_str()));
}

/// The parts of one kind of atom, in outline order, as {id, locale key}.
const std::vector<std::pair<std::string, std::string>> &atomParts()
{
	static const std::vector<std::pair<std::string, std::string>> parts = {
		{"shape", "Atom.Designer.Part.Shape"}, {"size", "Atom.Designer.Part.Size"},
		{"glow", "Atom.Designer.Part.Glow"},   {"trail", "Atom.Designer.Part.Trail"},
		{"life", "Atom.Designer.Part.Life"},   {"vary", "Atom.Designer.Part.Variation"},
	};
	return parts;
}

ParamSchema byGroup(const ParamSchema &schema, const std::string &group)
{
	ParamSchema filtered;
	for (const ParamSpec &spec : schema) {
		if (spec.group == group)
			filtered.push_back(spec);
	}
	return filtered;
}

bool listed(const std::string &id, std::initializer_list<const char *> ids)
{
	for (const char *candidate : ids) {
		if (id == candidate)
			return true;
	}
	return false;
}

ParamSchema withIds(const ParamSchema &schema, std::initializer_list<const char *> ids)
{
	ParamSchema filtered;
	for (const ParamSpec &spec : schema) {
		if (listed(spec.id, ids))
			filtered.push_back(spec);
	}
	return filtered;
}

ParamSchema withoutIds(const ParamSchema &schema, std::initializer_list<const char *> ids)
{
	ParamSchema filtered;
	for (const ParamSpec &spec : schema) {
		if (!listed(spec.id, ids))
			filtered.push_back(spec);
	}
	return filtered;
}

ParamBag bagSubset(const ParamBag &bag, const ParamSchema &schema)
{
	ParamBag subset;
	for (const ParamSpec &spec : schema) {
		if (const Value *value = bag.find(spec.id))
			subset.set(spec.id, *value);
	}
	return subset;
}

ParamBag withDefaults(ParamBag bag, const ParamSchema &schema)
{
	bag.applyDefaults(schema);
	return bag;
}

void clearLayout(QLayout *layout)
{
	while (QLayoutItem *item = layout->takeAt(0)) {
		// Sections are often rebuilt from inside one of their own signals, so the widgets are
		// only scheduled for deletion here.
		if (QWidget *widget = item->widget()) {
			widget->hide();
			widget->deleteLater();
		}
		delete item;
	}
}

/// A colour that stands for a kind of atom in the outline and the inspector header.
QColor layerChipColor(const AtomLayer &layer)
{
	Color color = layer.color.modeId == "solid" ? layer.color.color : layer.color.gradient.sample(0.3f);
	color.a = 1.0f;
	return toQColor(color);
}

QPixmap chipPixmap(const QColor &color, int size, bool round)
{
	QPixmap pixmap(size, size);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(QPen(QColor(0, 0, 0, 90), 1.0));
	painter.setBrush(color);
	const QRectF rect(0.5, 0.5, size - 1.0, size - 1.0);
	if (round)
		painter.drawEllipse(rect);
	else
		painter.drawRoundedRect(rect, 3.0, 3.0);
	return pixmap;
}

/// Display name of the setting a link drives, e.g. "Emission Rate".
QString targetLabel(const std::string &target)
{
	for (const auto &entry : modulationTargets()) {
		if (entry.first == target)
			return text(entry.second);
	}
	return QString::fromStdString(target);
}

QString linkLabel(const ModulationRoute &route)
{
	const ModuleInfo *info = ModulatorRegistry::instance().info(route.modulatorId);
	return text("Atom.Designer.Link.Label")
		.arg(info ? text(info->label) : QString::fromStdString(route.modulatorId), targetLabel(route.target));
}

/// Current value of a modulation target in `config`, and whether the target exists.
bool targetValue(const EmitterConfig &config, const std::string &target, double &value)
{
	const size_t dot = target.find('.');
	if (dot == std::string::npos)
		return false;
	const std::string table = target.substr(0, dot);
	const std::string id = target.substr(dot + 1);

	const auto read = [&](const ParamBag &bag) {
		const Value *found = bag.find(id);
		if (!found)
			return false;
		value = detail::asDouble(*found);
		return true;
	};
	if (table == "emission")
		return read(toBag(config.emission, emissionFields()));
	if (table == "physics")
		return read(toBag(config.physics, physicsFields()));
	return read(toBag(config.render, renderFields()));
}

/// Range of a modulation target's own slider, used to pick a sensible first amount for a link.
bool targetRange(const std::string &target, double &minimum, double &maximum)
{
	const size_t dot = target.find('.');
	if (dot == std::string::npos)
		return false;
	const std::string table = target.substr(0, dot);
	const std::string id = target.substr(dot + 1);
	const ParamSchema schema = table == "emission"  ? schemaOf(emissionFields())
				   : table == "physics" ? schemaOf(physicsFields())
							: schemaOf(renderFields());
	const ParamSpec *spec = findSpec(schema, id);
	if (!spec)
		return false;
	minimum = spec->min;
	maximum = spec->max;
	return true;
}

QFrame *makeCard(QWidget *parent)
{
	QFrame *card = new QFrame(parent);
	card->setFrameShape(QFrame::StyledPanel);
	return card;
}

QLabel *makeCaption(const QString &caption, QWidget *parent)
{
	QLabel *label = new QLabel(caption, parent);
	label->setWordWrap(true);
	label->setEnabled(false);
	return label;
}

} // namespace

AtomDesignerDialog::AtomDesignerDialog(obs_source_t *source, QWidget *parent) : QDialog(parent)
{
	registerBuiltinModules();

	weakSource_ = source ? obs_source_get_weak_source(source) : nullptr;
	config_ = configOfSource(source);
	if (config_.design.layers.empty())
		config_.design = AtomDesign::defaultDesign();
	opened_ = config_;
	committed_ = config_;
	selection_.kind = Kind::Atom;
	selection_.layerId = config_.design.layers.front().id;

	setWindowFlags(windowFlags() | Qt::Window);
	resize(1320, 860);

	// Writing to the source serializes the whole configuration and makes the source rebuild
	// itself from it. Dragging a slider fires dozens of edits a second, so those are folded into
	// one write per interval; the preview, which is cheap to update, still follows every edit.
	applyTimer_ = new QTimer(this);
	applyTimer_->setSingleShot(true);
	applyTimer_->setInterval(kLiveApplyIntervalMs);
	connect(applyTimer_, &QTimer::timeout, this, [this] { applyToSource(); });

	checkpointTimer_ = new QTimer(this);
	checkpointTimer_->setSingleShot(true);
	checkpointTimer_->setInterval(kUndoMergeMs);
	connect(checkpointTimer_, &QTimer::timeout, this, [this] { checkpoint(); });

	QSplitter *columns = new QSplitter(Qt::Horizontal, this);
	columns->setChildrenCollapsible(false);
	columns->addWidget(buildOutlinePane());
	columns->addWidget(buildCenterPane());
	columns->addWidget(buildInspectorPane());
	columns->setStretchFactor(0, 0);
	columns->setStretchFactor(1, 1);
	columns->setStretchFactor(2, 0);
	columns->setSizes({270, 630, 420});

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(buildTopBar());
	layout->addWidget(columns, 1);

	// Created last so it stacks above the columns it opens over.
	presetPanel_ = new PresetPanel(this);

	QShortcut *undoKey = new QShortcut(QKeySequence::Undo, this);
	connect(undoKey, &QShortcut::activated, this, [this] { undo(); });
	QShortcut *redoKey = new QShortcut(QKeySequence::Redo, this);
	connect(redoKey, &QShortcut::activated, this, [this] { redo(); });

	refreshEverything();
}

AtomDesignerDialog::~AtomDesignerDialog()
{
	// An edit made just before closing may still be waiting on the timer.
	if (applyTimer_ && applyTimer_->isActive())
		applyToSource();

	if (weakSource_)
		obs_weak_source_release(weakSource_);
}

obs_source_t *AtomDesignerDialog::acquireSource() const
{
	return weakSource_ ? obs_weak_source_get_source(weakSource_) : nullptr;
}

void AtomDesignerDialog::resizeEvent(QResizeEvent *event)
{
	QDialog::resizeEvent(event);
	if (presetPanel_ && presetPanel_->isVisible())
		placePresetPanel();
}

void AtomDesignerDialog::keyPressEvent(QKeyEvent *event)
{
	// Escape backs out of the preset pop-up; it never closes the designer, where a stray press
	// would otherwise throw the window away mid-edit.
	if (event->key() == Qt::Key_Escape) {
		if (presetPanel_ && presetPanel_->isVisible())
			presetPanel_->cancel();
		event->accept();
		return;
	}
	// Return belongs to whichever field has focus, not to a default button.
	if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
		event->accept();
		return;
	}
	QDialog::keyPressEvent(event);
}

// ---------------------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------------------

QWidget *AtomDesignerDialog::buildTopBar()
{
	QWidget *bar = new QWidget(this);

	title_ = new QLabel(bar);
	QFont titleFont = title_->font();
	titleFont.setPointSizeF(titleFont.pointSizeF() * 1.25);
	titleFont.setBold(true);
	title_->setFont(titleFont);

	subtitle_ = new QLabel(bar);
	subtitle_->setEnabled(false);

	QVBoxLayout *names = new QVBoxLayout();
	names->setSpacing(0);
	names->addWidget(title_);
	names->addWidget(subtitle_);

	undoButton_ = new QPushButton(text("Atom.Designer.Undo"), bar);
	undoButton_->setObjectName("atomUndo");
	redoButton_ = new QPushButton(text("Atom.Designer.Redo"), bar);
	redoButton_->setObjectName("atomRedo");

	liveButton_ = new QPushButton(text("Atom.Designer.Live"), bar);
	liveButton_->setCheckable(true);
	liveButton_->setChecked(true);
	liveButton_->setToolTip(text("Atom.Designer.Live.Tip"));

	QPushButton *save = new QPushButton(text("Atom.Designer.SavePreset"), bar);
	QPushButton *done = new QPushButton(text("Atom.Designer.Done"), bar);
	done->setAutoDefault(false);

	QHBoxLayout *layout = new QHBoxLayout(bar);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addLayout(names, 1);
	layout->addWidget(undoButton_);
	layout->addWidget(redoButton_);
	layout->addWidget(liveButton_);
	layout->addWidget(save);
	layout->addWidget(done);

	for (QPushButton *button : {undoButton_, redoButton_, liveButton_, save, done})
		button->setAutoDefault(false);

	connect(undoButton_, &QPushButton::clicked, this, [this] { undo(); });
	connect(redoButton_, &QPushButton::clicked, this, [this] { redo(); });
	connect(liveButton_, &QPushButton::toggled, this, [this](bool live) {
		// Turning it back on sends whatever was edited while it was off.
		if (live)
			applyToSource();
	});
	connect(save, &QPushButton::clicked, this, [this] { saveCurrentAsPreset(); });
	connect(done, &QPushButton::clicked, this, [this] {
		applyToSource();
		close();
	});

	return bar;
}

QWidget *AtomDesignerDialog::buildOutlinePane()
{
	QWidget *pane = new QWidget(this);

	presetsButton_ = new QPushButton(text("Atom.Designer.Presets.Button"), pane);
	presetsButton_->setObjectName("atomPresetsButton");
	presetsButton_->setCheckable(true);
	presetsButton_->setAutoDefault(false);
	presetsButton_->setToolTip(text("Atom.Designer.Presets.Button.Tip"));
	presetsButton_->setMinimumHeight(presetsButton_->sizeHint().height() + 8);

	outline_ = new QTreeWidget(pane);
	outline_->setObjectName("atomOutline");
	outline_->setHeaderHidden(true);
	outline_->setColumnCount(2);
	outline_->setRootIsDecorated(false);
	outline_->setIndentation(14);
	outline_->setUniformRowHeights(false);
	outline_->setExpandsOnDoubleClick(false);
	outline_->setSelectionBehavior(QAbstractItemView::SelectRows);
	outline_->header()->setStretchLastSection(false);
	outline_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	outline_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

	QVBoxLayout *layout = new QVBoxLayout(pane);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(presetsButton_);
	layout->addWidget(outline_, 1);

	connect(presetsButton_, &QPushButton::clicked, this, [this] { togglePresets(); });
	connect(outline_, &QTreeWidget::currentItemChanged, this, [this] { onOutlineSelectionChanged(); });
	connect(outline_, &QTreeWidget::itemClicked, this,
		[this](QTreeWidgetItem *item) { onOutlineItemClicked(item); });
	connect(outline_, &QTreeWidget::itemChanged, this,
		[this](QTreeWidgetItem *item) { onOutlineItemChanged(item); });

	return pane;
}

QWidget *AtomDesignerDialog::buildCenterPane()
{
	QWidget *pane = new QWidget(this);

	preview_ = new AtomPreview(pane);
	preview_->setObjectName("atomPreview");
	preview_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	previewHint_ = new QLabel(pane);
	previewHint_->setWordWrap(true);

	QPushButton *pause = new QPushButton(text("Atom.Designer.Pause"), pane);
	pause->setCheckable(true);
	QPushButton *restart = new QPushButton(text("Atom.Designer.Restart"), pane);
	QPushButton *burst = new QPushButton(text("Atom.Designer.Burst"), pane);
	burst->setToolTip(text("Atom.Designer.Burst.Tip"));
	QCheckBox *checker = new QCheckBox(text("Atom.Designer.Checker"), pane);
	checker->setToolTip(text("Atom.Designer.Checker.Tip"));

	for (QPushButton *button : {pause, restart, burst})
		button->setAutoDefault(false);

	QHBoxLayout *transport = new QHBoxLayout();
	transport->addWidget(pause);
	transport->addWidget(restart);
	transport->addWidget(burst);
	transport->addStretch(1);
	transport->addWidget(checker);

	QVBoxLayout *layout = new QVBoxLayout(pane);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(previewHint_);
	layout->addWidget(preview_, 1);
	layout->addLayout(transport);
	layout->addWidget(buildLifeTracks());

	connect(pause, &QPushButton::toggled, this, [this, pause](bool paused) {
		if (preview_)
			preview_->setPlaying(!paused);
		pause->setText(text(paused ? "Atom.Designer.Resume" : "Atom.Designer.Pause"));
	});
	connect(restart, &QPushButton::clicked, this, [this] {
		if (preview_)
			preview_->restart();
	});
	connect(burst, &QPushButton::clicked, this, [this] {
		if (preview_)
			preview_->burst(std::max(1, config_.emission.burstCount));
	});
	connect(checker, &QCheckBox::toggled, this, [this](bool on) {
		if (preview_)
			preview_->setChecker(on);
	});
	connect(preview_.data(), &AtomPreview::dragged, this,
		[this](const QPointF &position) { onPreviewDragged(position); });

	return pane;
}

QWidget *AtomDesignerDialog::buildLifeTracks()
{
	QFrame *panel = makeCard(this);
	panel->setObjectName("atomLifeTracks");

	lifeStack_ = new QStackedWidget(panel);

	// Page 0: the tracks of the selected kind of atom.
	QWidget *tracks = new QWidget(lifeStack_);

	lifeTitle_ = new QLabel(tracks);
	QFont bold = lifeTitle_->font();
	bold.setBold(true);
	lifeTitle_->setFont(bold);

	lifeGradient_ = new GradientEditor(tracks);
	lifeGradient_->setToolTip(text("Atom.Designer.GradientHint"));
	lifeColor_ = new ColorButton(tracks);
	lifeColorStack_ = new QStackedWidget(tracks);
	lifeColorStack_->addWidget(lifeGradient_);
	lifeColorStack_->addWidget(lifeColor_);
	lifeColorStack_->setFixedHeight(46);

	lifeSize_ = new CurveEditor(tracks);
	lifeSize_->setValueRange(0.0f, 3.0f);
	lifeSize_->setToolTip(text("Atom.Designer.CurveHint"));
	lifeSize_->setFixedHeight(72);

	lifeFade_ = new CurveEditor(tracks);
	lifeFade_->setValueRange(0.0f, 1.0f);
	lifeFade_->setToolTip(text("Atom.Designer.CurveHint"));
	lifeFade_->setFixedHeight(72);

	QLabel *start = new QLabel(text("Atom.Designer.Life.Start"), tracks);
	start->setEnabled(false);
	lifeEnd_ = new QLabel(tracks);
	lifeEnd_->setEnabled(false);
	lifeEnd_->setAlignment(Qt::AlignRight);
	QHBoxLayout *axis = new QHBoxLayout();
	axis->addWidget(start);
	axis->addStretch(1);
	axis->addWidget(lifeEnd_);

	QGridLayout *grid = new QGridLayout(tracks);
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setColumnStretch(1, 1);
	grid->addWidget(lifeTitle_, 0, 0, 1, 2);
	grid->addWidget(makeCaption(text("Atom.Designer.Life.Caption"), tracks), 1, 0, 1, 2);
	grid->addWidget(new QLabel(text("Atom.Designer.Life.Color"), tracks), 2, 0);
	grid->addWidget(lifeColorStack_, 2, 1);
	grid->addWidget(new QLabel(text("Atom.Designer.Life.Size"), tracks), 3, 0);
	grid->addWidget(lifeSize_, 3, 1);
	grid->addWidget(new QLabel(text("Atom.Designer.Life.Opacity"), tracks), 4, 0);
	grid->addWidget(lifeFade_, 4, 1);
	grid->addLayout(axis, 5, 1);

	// Page 1: nothing per-atom is selected.
	lifeHint_ = new QLabel(lifeStack_);
	lifeHint_->setWordWrap(true);
	lifeHint_->setAlignment(Qt::AlignCenter);
	lifeHint_->setEnabled(false);

	lifeStack_->addWidget(tracks);
	lifeStack_->addWidget(lifeHint_);

	QVBoxLayout *layout = new QVBoxLayout(panel);
	layout->addWidget(lifeStack_);

	connect(lifeGradient_, &GradientEditor::gradientChanged, this, [this] {
		AtomLayer *layer = currentLayer();
		if (!layer || updating_)
			return;
		layer->color.gradient = lifeGradient_->gradient();
		configChanged();
	});
	connect(lifeColor_, &ColorButton::colorChanged, this, [this] {
		AtomLayer *layer = currentLayer();
		if (!layer || updating_)
			return;
		layer->color.color = lifeColor_->color();
		scheduleOutlineRebuild();
		scheduleInspectorRebuild();
		configChanged();
	});
	connect(lifeSize_, &CurveEditor::curveChanged, this, [this] {
		AtomLayer *layer = currentLayer();
		if (!layer || updating_)
			return;
		layer->size.overLife = lifeSize_->curve();
		configChanged();
	});
	connect(lifeFade_, &CurveEditor::curveChanged, this, [this] {
		AtomLayer *layer = currentLayer();
		if (!layer || updating_)
			return;
		// A built-in fade is shown as a curve; reshaping it turns it into the layer's own curve.
		if (layer->fade.pathId != "custom") {
			layer->fade.pathId = "custom";
			scheduleInspectorRebuild();
		}
		layer->fade.customCurve = lifeFade_->curve();
		configChanged();
	});

	return panel;
}

QWidget *AtomDesignerDialog::buildInspectorPane()
{
	QWidget *pane = new QWidget(this);

	// "You are editing": what the settings below belong to, and how far they reach.
	QFrame *header = makeCard(pane);
	header->setObjectName("atomInspectorHeader");

	QLabel *eyebrow = new QLabel(text("Atom.Designer.Editing"), header);
	eyebrow->setEnabled(false);

	headerChip_ = new QLabel(header);
	headerChip_->setFixedSize(16, 16);
	headerTitle_ = new QLabel(header);
	QFont titleFont = headerTitle_->font();
	titleFont.setPointSizeF(titleFont.pointSizeF() * 1.3);
	titleFont.setBold(true);
	headerTitle_->setFont(titleFont);
	headerTitle_->setWordWrap(true);

	QHBoxLayout *titleRow = new QHBoxLayout();
	titleRow->addWidget(headerChip_);
	titleRow->addWidget(headerTitle_, 1);

	headerScope_ = new QLabel(header);
	headerScope_->setWordWrap(true);

	headerActions_ = new QWidget(header);
	QHBoxLayout *actions = new QHBoxLayout(headerActions_);
	actions->setContentsMargins(0, 0, 0, 0);

	QVBoxLayout *headerLayout = new QVBoxLayout(header);
	headerLayout->addWidget(eyebrow);
	headerLayout->addLayout(titleRow);
	headerLayout->addWidget(headerScope_);
	headerLayout->addWidget(headerActions_);

	QWidget *content = new QWidget();
	inspectorLayout_ = new QVBoxLayout(content);
	inspectorLayout_->setContentsMargins(0, 0, 0, 0);
	sectionTarget_ = inspectorLayout_;

	inspectorScroll_ = new QScrollArea(pane);
	inspectorScroll_->setWidgetResizable(true);
	inspectorScroll_->setFrameShape(QFrame::NoFrame);
	inspectorScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	inspectorScroll_->setWidget(content);

	QVBoxLayout *layout = new QVBoxLayout(pane);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(header);
	layout->addWidget(inspectorScroll_, 1);

	return pane;
}

// ---------------------------------------------------------------------------------------------
// Outline
// ---------------------------------------------------------------------------------------------

void AtomDesignerDialog::rebuildOutline()
{
	updating_ = true;
	outline_->clear();

	QFont headerFont = outline_->font();
	headerFont.setBold(true);
	headerFont.setPointSizeF(headerFont.pointSizeF() * 0.88);
	headerFont.setCapitalization(QFont::AllUppercase);

	QTreeWidgetItem *toSelect = nullptr;

	const auto addHeader = [&](const char *titleKey, const char *hintKey) {
		QTreeWidgetItem *header = new QTreeWidgetItem(outline_);
		header->setText(0, text(titleKey));
		header->setToolTip(0, text(hintKey));
		header->setFont(0, headerFont);
		header->setFlags(Qt::ItemIsEnabled);
		header->setFirstColumnSpanned(true);
		header->setData(0, kKindRole, static_cast<int>(Kind::None));
		header->setExpanded(true);
		return header;
	};
	const auto addRow = [&](QTreeWidgetItem *parent, const QString &label, Kind kind,
				const std::string &layerId = {}, const std::string &part = {}, int index = -1) {
		QTreeWidgetItem *row = new QTreeWidgetItem(parent);
		row->setText(0, label);
		row->setData(0, kKindRole, static_cast<int>(kind));
		row->setData(0, kLayerRole, QString::fromStdString(layerId));
		row->setData(0, kPartRole, QString::fromStdString(part));
		row->setData(0, kIndexRole, index);
		row->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
		if (kind == selection_.kind && layerId == selection_.layerId && part == selection_.part &&
		    index == selection_.index)
			toSelect = row;
		return row;
	};
	const auto addAction = [&](QTreeWidgetItem *parent, const char *labelKey, Kind kind) {
		QTreeWidgetItem *row = addRow(parent, QString("+  ") + text(labelKey), kind);
		QFont font = outline_->font();
		font.setItalic(true);
		row->setFont(0, font);
		row->setFirstColumnSpanned(true);
		return row;
	};
	const auto setChecked = [](QTreeWidgetItem *row, bool checked) {
		row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
		row->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
	};

	// Emitter.
	QTreeWidgetItem *emitter = addHeader("Atom.Designer.Group.Emitter", "Atom.Designer.Group.Emitter.Hint");
	{
		QTreeWidgetItem *row = addRow(emitter, text("Atom.Designer.Row.Emitter"), Kind::Emitter);
		const ModuleInfo *shape = EmitterShapeRegistry::instance().info(config_.emission.shapeId);
		row->setText(1, shape ? text(shape->label) : QString());
	}

	// Atoms: each kind, with everything that belongs to it underneath.
	QTreeWidgetItem *atoms = addHeader("Atom.Designer.Group.Atoms", "Atom.Designer.Group.Atoms.Hint");
	float totalWeight = 0.0f;
	for (const AtomLayer &layer : config_.design.layers)
		totalWeight += layer.enabled ? std::max(0.0f, layer.weight) : 0.0f;
	for (const AtomLayer &layer : config_.design.layers) {
		QTreeWidgetItem *row = addRow(atoms, QString::fromStdString(layer.name), Kind::Atom, layer.id);
		row->setIcon(0, chipPixmap(layerChipColor(layer), 12, true));
		setChecked(row, layer.enabled);
		if (layer.enabled && totalWeight > 0.0f) {
			const int share = static_cast<int>(std::max(0.0f, layer.weight) / totalWeight * 100.0f + 0.5f);
			row->setText(1, QString("%1%").arg(share));
		}
		for (const auto &part : atomParts()) {
			QTreeWidgetItem *child = addRow(row, text(part.second), Kind::Atom, layer.id, part.first);
			if (part.first == "trail") {
				const ModuleInfo *style = TrailRegistry::instance().info(layer.trail.styleId);
				child->setText(1, layer.trail.enabled ? (style ? text(style->label) : QString())
								      : text("Atom.Designer.Off"));
			}
		}
		// Only the kind being edited shows its parts; the rest stay one line each.
		row->setExpanded(selection_.kind == Kind::Atom && selection_.layerId == layer.id);
	}
	addAction(atoms, "Atom.Designer.Add.Atom", Kind::AddAtom);

	// Motion: shared by every kind of atom.
	QTreeWidgetItem *motion = addHeader("Atom.Designer.Group.Motion", "Atom.Designer.Group.Motion.Hint");
	addRow(motion, text("Atom.Designer.Row.Launch"), Kind::Launch);
	{
		QTreeWidgetItem *row = addRow(motion, text("Atom.Designer.Row.Destination"), Kind::Destination);
		const ModuleInfo *mode = EndpointRegistry::instance().info(config_.physics.endpoint.modeId);
		row->setText(1, mode ? text(mode->label) : QString());
	}
	for (size_t i = 0; i < config_.physics.extras.size(); ++i) {
		const BehaviorInstance &instance = config_.physics.extras[i];
		const ModuleInfo *info = BehaviorRegistry::instance().info(instance.id);
		QTreeWidgetItem *row = addRow(motion, info ? text(info->label) : QString::fromStdString(instance.id),
					      Kind::Force, {}, {}, static_cast<int>(i));
		setChecked(row, instance.enabled);
	}
	addAction(motion, "Atom.Designer.Add.Force", Kind::AddForce);

	// Reacts to: audio and waves driving settings.
	QTreeWidgetItem *reacts = addHeader("Atom.Designer.Group.Reacts", "Atom.Designer.Group.Reacts.Hint");
	{
		QTreeWidgetItem *row = addRow(reacts, text("Atom.Designer.Row.Audio"), Kind::Audio);
		row->setText(1, config_.audio.sourceName.empty() ? text("Atom.Designer.None")
								 : QString::fromStdString(config_.audio.sourceName));
	}
	addRow(reacts, text("Atom.Designer.Row.Scene"), Kind::Scene);
	for (size_t i = 0; i < config_.modulation.size(); ++i) {
		QTreeWidgetItem *row =
			addRow(reacts, linkLabel(config_.modulation[i]), Kind::Link, {}, {}, static_cast<int>(i));
		row->setFirstColumnSpanned(true);
		setChecked(row, config_.modulation[i].enabled);
	}
	addAction(reacts, "Atom.Designer.Add.Link", Kind::AddLink);

	// Output.
	QTreeWidgetItem *output = addHeader("Atom.Designer.Group.Output", "Atom.Designer.Group.Output.Hint");
	addRow(output, text("Atom.Designer.Row.Output"), Kind::Output);

	if (!toSelect) {
		// The selection no longer exists (its layer or force was removed): fall back to the
		// first kind of atom, which always does.
		selection_ = Selection{};
		selection_.layerId = config_.design.layers.front().id;
		for (int i = 0; i < atoms->childCount(); ++i) {
			if (selectionOf(atoms->child(i)).layerId == selection_.layerId) {
				toSelect = atoms->child(i);
				toSelect->setExpanded(true);
				break;
			}
		}
	}
	if (toSelect)
		outline_->setCurrentItem(toSelect);

	updating_ = false;
}

AtomDesignerDialog::Selection AtomDesignerDialog::selectionOf(QTreeWidgetItem *item) const
{
	Selection selection;
	selection.kind = Kind::None;
	if (!item)
		return selection;
	selection.kind = static_cast<Kind>(item->data(0, kKindRole).toInt());
	selection.layerId = item->data(0, kLayerRole).toString().toStdString();
	selection.part = item->data(0, kPartRole).toString().toStdString();
	selection.index = item->data(0, kIndexRole).toInt();
	return selection;
}

void AtomDesignerDialog::select(const Selection &selection)
{
	selection_ = selection;
	rebuildOutline();
	rebuildInspector();
	refreshLifeTracks();
	refreshPreviewDecor();
}

void AtomDesignerDialog::onOutlineSelectionChanged()
{
	if (updating_)
		return;

	const Selection picked = selectionOf(outline_->currentItem());
	// Headers and the "+ Add" rows are not things to edit; clicking them leaves the selection be.
	if (picked.kind == Kind::None || picked.kind == Kind::AddAtom || picked.kind == Kind::AddForce ||
	    picked.kind == Kind::AddLink)
		return;

	const bool layerChanged = picked.layerId != selection_.layerId || picked.kind != selection_.kind;
	selection_ = picked;

	if (layerChanged) {
		// Open the parts of the kind of atom being edited and fold the others away.
		for (int i = 0; i < outline_->topLevelItemCount(); ++i) {
			QTreeWidgetItem *group = outline_->topLevelItem(i);
			for (int j = 0; j < group->childCount(); ++j) {
				QTreeWidgetItem *row = group->child(j);
				if (row->childCount() == 0)
					continue;
				row->setExpanded(selection_.kind == Kind::Atom &&
						 selectionOf(row).layerId == selection_.layerId);
			}
		}
	}

	rebuildInspector();
	refreshLifeTracks();
	refreshPreviewDecor();
}

void AtomDesignerDialog::onOutlineItemClicked(QTreeWidgetItem *item)
{
	switch (selectionOf(item).kind) {
	case Kind::AddAtom:
		addAtom();
		break;
	case Kind::AddForce:
		addForce(item);
		break;
	case Kind::AddLink:
		addLink();
		break;
	default:
		break;
	}
}

void AtomDesignerDialog::onOutlineItemChanged(QTreeWidgetItem *item)
{
	if (updating_ || !item)
		return;

	// The only thing a user can change on a row is its tick box.
	const Selection row = selectionOf(item);
	const bool enabled = item->checkState(0) == Qt::Checked;
	if (row.kind == Kind::Atom && row.part.empty()) {
		AtomLayer *layer = config_.design.findLayer(row.layerId);
		if (!layer || layer->enabled == enabled)
			return;
		layer->enabled = enabled;
	} else if (row.kind == Kind::Force && row.index >= 0 &&
		   row.index < static_cast<int>(config_.physics.extras.size())) {
		BehaviorInstance &instance = config_.physics.extras[static_cast<size_t>(row.index)];
		if (instance.enabled == enabled)
			return;
		instance.enabled = enabled;
	} else if (row.kind == Kind::Link && row.index >= 0 &&
		   row.index < static_cast<int>(config_.modulation.size())) {
		ModulationRoute &route = config_.modulation[static_cast<size_t>(row.index)];
		if (route.enabled == enabled)
			return;
		route.enabled = enabled;
	} else {
		return;
	}

	scheduleOutlineRebuild();
	scheduleInspectorRebuild();
	configChanged();
}

void AtomDesignerDialog::scheduleOutlineRebuild()
{
	if (outlineRebuildPending_)
		return;
	outlineRebuildPending_ = true;
	QTimer::singleShot(0, this, [this] {
		outlineRebuildPending_ = false;
		rebuildOutline();
	});
}

void AtomDesignerDialog::scheduleInspectorRebuild()
{
	// Rebuilding destroys the editors, and the request usually comes from one of them, so it
	// waits until that signal has finished.
	if (inspectorRebuildPending_)
		return;
	inspectorRebuildPending_ = true;
	QTimer::singleShot(0, this, [this] {
		inspectorRebuildPending_ = false;
		rebuildInspector();
		if (popupRebuild_)
			popupRebuild_();
	});
}

// ---------------------------------------------------------------------------------------------
// Inspector
// ---------------------------------------------------------------------------------------------

ParamEditor *AtomDesignerDialog::addSection(const QString &title, const ParamSchema &schema, const ParamBag &values,
					    SectionWriter write, const char *linkTable, const QStringList &sourceNames)
{
	if (schema.empty())
		return nullptr;

	QGroupBox *box = new QGroupBox(title);
	ParamEditor *editor = new ParamEditor(box);
	if (!sourceNames.isEmpty())
		editor->setSourceNames(sourceNames);

	const std::string table = linkTable ? linkTable : "";
	if (linkTable) {
		// Numeric emitter-wide settings can be driven by audio or a wave.
		QStringList linkable;
		QStringList linked;
		for (const ParamSpec &spec : schema) {
			if (spec.type != ParamType::Float && spec.type != ParamType::Int)
				continue;
			linkable.append(QString::fromStdString(spec.id));
			for (const ModulationRoute &route : config_.modulation) {
				if (route.target == table + "." + spec.id)
					linked.append(QString::fromStdString(spec.id));
			}
		}
		editor->setLinks(linkable, linked);
	}

	editor->setSchema(schema, values);

	QVBoxLayout *boxLayout = new QVBoxLayout(box);
	boxLayout->addWidget(editor);
	sectionTarget_->addWidget(box);

	connect(editor, &ParamEditor::valueChanged, this, [this, editor, write](const QString &id) {
		if (updating_)
			return;
		write(editor->values(), id.toStdString());
		configChanged();
	});
	connect(editor, &ParamEditor::linkRequested, this,
		[this, table](const QString &id) { requestLink(table + "." + id.toStdString()); });

	return editor;
}

void AtomDesignerDialog::addHeaderAction(const QString &label, std::function<void()> action)
{
	QPushButton *button = new QPushButton(label, headerActions_);
	button->setAutoDefault(false);
	headerActions_->layout()->addWidget(button);
	// Queued: the action usually rebuilds the header, button included.
	connect(button, &QPushButton::clicked, this, [action] { action(); }, Qt::QueuedConnection);
}

void AtomDesignerDialog::rebuildInspector()
{
	updating_ = true;
	clearLayout(inspectorLayout_);
	clearLayout(headerActions_->layout());
	sectionTarget_ = inspectorLayout_;

	switch (selection_.kind) {
	case Kind::Atom:
		if (AtomLayer *layer = currentLayer())
			buildAtomSections(*layer);
		break;
	case Kind::Emitter:
		buildEmitterSections();
		break;
	case Kind::Launch:
		buildLaunchSections();
		break;
	case Kind::Destination: {
		const ParamSchema schema = byGroup(schemaOf(physicsFields()), "endpoint");
		addSection(
			text("Atom.Designer.Row.Destination"), schema,
			bagSubset(toBag(config_.physics, physicsFields()), schema),
			[this](const ParamBag &bag, const std::string &id) {
				fromBag(config_.physics, bag, physicsFields());
				if (id == "endpoint_mode") {
					scheduleOutlineRebuild();
					refreshPreviewDecor();
				}
			},
			"physics", videoSourceNames());
		break;
	}
	case Kind::Force:
		buildForceSections();
		break;
	case Kind::Audio:
		addSection(
			text("Atom.Designer.Row.Audio"), schemaOf(audioFields()), toBag(config_.audio, audioFields()),
			[this](const ParamBag &bag, const std::string &id) {
				fromBag(config_.audio, bag, audioFields());
				if (id == "audio_source")
					scheduleOutlineRebuild();
			},
			nullptr, audioSourceNames());
		break;
	case Kind::Scene:
		addSection(text("Atom.Designer.Row.Scene"), schemaOf(sceneFields()),
			   toBag(config_.scene, sceneFields()), [this](const ParamBag &bag, const std::string &) {
				   fromBag(config_.scene, bag, sceneFields());
			   });
		break;
	case Kind::Link:
		addHeaderAction(text("Atom.Designer.Remove.Link"), [this] { removeLink(); });
		buildLinkSections(selection_.index, true);
		break;
	case Kind::Output: {
		const ParamSchema schema = schemaOf(renderFields());
		const ParamBag values = toBag(config_.render, renderFields());
		const SectionWriter write = [this](const ParamBag &bag, const std::string &) {
			fromBag(config_.render, bag, renderFields());
		};
		addSection(text("Atom.Designer.Section.GlowPass"), byGroup(schema, "bloom"),
			   bagSubset(values, byGroup(schema, "bloom")), write, "render");
		addSection(text("Atom.Designer.Section.Trims"), byGroup(schema, "global"),
			   bagSubset(values, byGroup(schema, "global")), write, "render");
		break;
	}
	default:
		break;
	}

	inspectorLayout_->addStretch(1);
	static_cast<QHBoxLayout *>(headerActions_->layout())->addStretch(1);
	updating_ = false;

	refreshHeader();
}

void AtomDesignerDialog::buildAtomSections(AtomLayer &layer)
{
	const ParamSchema all = schemaOf(layerFields());
	const ParamBag values = toBag(layer, layerFields());
	const std::string &part = selection_.part;
	const auto want = [&part](const char *id) {
		return part.empty() || part == id;
	};
	const auto section = [&](const std::string &titleKey, const ParamSchema &schema, const SectionWriter &write) {
		addSection(text(titleKey), schema, bagSubset(values, schema), write);
	};

	// Writes a value from one of the generated sections, then deals with the settings that
	// change what else is on screen.
	const SectionWriter writeLayer = [this](const ParamBag &bag, const std::string &id) {
		AtomLayer *target = currentLayer();
		if (!target)
			return;
		fromBag(*target, bag, layerFields());

		if (id == "sprite") {
			target->spriteParams.clear();
			target->spriteParams.applyDefaults(registrySchema(registries::kSprite, target->spriteId));
			scheduleInspectorRebuild();
		} else if (id == "trail_style") {
			target->trail.styleParams.clear();
			target->trail.styleParams.applyDefaults(
				registrySchema(registries::kTrail, target->trail.styleId));
			scheduleInspectorRebuild();
			scheduleOutlineRebuild();
		} else if (id == "trail_enabled") {
			scheduleInspectorRebuild();
			scheduleOutlineRebuild();
		} else if (id == "fade_path") {
			target->fade.pathParams.clear();
			target->fade.pathParams.applyDefaults(
				registrySchema(registries::kFadePath, target->fade.pathId));
			scheduleInspectorRebuild();
			refreshLifeTracks();
		} else if (id == "color_mode" || id == "color_color") {
			refreshLifeTracks();
			scheduleOutlineRebuild();
		} else if (id == "name" || id == "weight") {
			scheduleOutlineRebuild();
			refreshHeader();
		}
	};

	addHeaderAction(text("Atom.Designer.Duplicate.Atom"), [this] { duplicateAtom(); });
	if (config_.design.layers.size() > 1)
		addHeaderAction(text("Atom.Designer.Remove.Atom"), [this] { removeAtom(); });

	if (want("shape")) {
		section("Atom.Designer.Part.Shape", withoutIds(byGroup(all, "layer"), {"enabled"}), writeLayer);
		addSection(text("Atom.Designer.SpriteOptions"), registrySchema(registries::kSprite, layer.spriteId),
			   withDefaults(layer.spriteParams, registrySchema(registries::kSprite, layer.spriteId)),
			   [this](const ParamBag &bag, const std::string &) {
				   if (AtomLayer *target = currentLayer())
					   target->spriteParams = bag;
			   });
		section("Atom.Designer.Page.Sheet", byGroup(all, "sheet"), writeLayer);
	}
	if (want("size"))
		section("Atom.Designer.Part.Size", withoutIds(byGroup(all, "size"), {"size_over_life"}), writeLayer);
	if (want("glow"))
		section("Atom.Designer.Part.Glow", byGroup(all, "bloom"), writeLayer);
	if (want("trail")) {
		section("Atom.Designer.Part.Trail", byGroup(all, "trail"), writeLayer);
		if (layer.trail.enabled) {
			const ParamSchema style = registrySchema(registries::kTrail, layer.trail.styleId);
			addSection(text("Atom.Designer.StyleOptions"), style,
				   withDefaults(layer.trail.styleParams, style),
				   [this](const ParamBag &bag, const std::string &) {
					   if (AtomLayer *target = currentLayer())
						   target->trail.styleParams = bag;
				   });
		}
	}
	if (want("life")) {
		// The gradient and the curves themselves are edited on the life tracks under the preview.
		section("Atom.Designer.Life.Color", withoutIds(byGroup(all, "color"), {"color_gradient"}), writeLayer);
		section("Atom.Designer.Life.Opacity", withoutIds(byGroup(all, "fade"), {"fade_curve"}), writeLayer);
		if (layer.fade.pathId != "custom") {
			const ParamSchema path = registrySchema(registries::kFadePath, layer.fade.pathId);
			addSection(text("Atom.Designer.StyleOptions"), path, withDefaults(layer.fade.pathParams, path),
				   [this](const ParamBag &bag, const std::string &) {
					   if (AtomLayer *target = currentLayer()) {
						   target->fade.pathParams = bag;
						   refreshLifeTracks();
					   }
				   });
		}
	}
	if (want("vary"))
		section("Atom.Designer.Part.Variation", byGroup(all, "offset"), writeLayer);
}

void AtomDesignerDialog::buildEmitterSections()
{
	const ParamSchema all = schemaOf(emissionFields());
	const ParamBag values = toBag(config_.emission, emissionFields());

	const SectionWriter write = [this](const ParamBag &bag, const std::string &id) {
		fromBag(config_.emission, bag, emissionFields());
		if (id == "shape") {
			config_.emission.shapeParams.clear();
			config_.emission.shapeParams.applyDefaults(
				registrySchema(registries::kEmitterShape, config_.emission.shapeId));
			scheduleInspectorRebuild();
			scheduleOutlineRebuild();
		}
	};

	const ParamSchema location = byGroup(all, "location");
	addSection(text("Atom.Designer.Section.Where"), location, bagSubset(values, location), write);

	const ParamSchema shape = registrySchema(registries::kEmitterShape, config_.emission.shapeId);
	addSection(text("Atom.Designer.ShapeOptions"), shape, withDefaults(config_.emission.shapeParams, shape),
		   [this](const ParamBag &bag, const std::string &) { config_.emission.shapeParams = bag; });

	const ParamSchema rate = byGroup(all, "rate");
	addSection(text("Atom.Designer.Section.HowMany"), rate, bagSubset(values, rate), write, "emission");

	const ParamSchema size = byGroup(all, "size");
	addSection(text("Atom.Designer.Section.Canvas"), size, bagSubset(values, size), write);
}

void AtomDesignerDialog::buildLaunchSections()
{
	const ParamSchema all = schemaOf(physicsFields());
	const ParamBag values = toBag(config_.physics, physicsFields());

	const SectionWriter write = [this](const ParamBag &bag, const std::string &id) {
		fromBag(config_.physics, bag, physicsFields());
		if (id == "falloff") {
			config_.physics.falloffParams.clear();
			config_.physics.falloffParams.applyDefaults(
				registrySchema(registries::kFalloff, config_.physics.falloffId));
			scheduleInspectorRebuild();
		} else if (id == "lifetime") {
			refreshLifeTracks();
		}
	};

	const ParamSchema launch = withIds(all, {"speed", "emit_angle", "emit_spread"});
	addSection(text("Atom.Designer.Section.Launch"), launch, bagSubset(values, launch), write, "physics");

	const ParamSchema alive = withIds(all, {"gravity", "gravity_angle", "drag", "turbulence", "turbulence_scale",
						"turbulence_speed", "spin", "align_to_velocity"});
	addSection(text("Atom.Designer.Section.Alive"), alive, bagSubset(values, alive), write, "physics");

	const ParamSchema life = byGroup(all, "life");
	addSection(text("Atom.Designer.Page.Lifetime"), life, bagSubset(values, life), write, "physics");

	const ParamSchema falloff = registrySchema(registries::kFalloff, config_.physics.falloffId);
	addSection(text("Atom.Designer.FalloffOptions"), falloff, withDefaults(config_.physics.falloffParams, falloff),
		   [this](const ParamBag &bag, const std::string &) { config_.physics.falloffParams = bag; });
}

void AtomDesignerDialog::buildForceSections()
{
	const int index = selection_.index;
	if (index < 0 || index >= static_cast<int>(config_.physics.extras.size()))
		return;

	addHeaderAction(text("Atom.Designer.Remove.Force"), [this] { removeForce(); });

	const BehaviorInstance &instance = config_.physics.extras[static_cast<size_t>(index)];
	const ModuleInfo *info = BehaviorRegistry::instance().info(instance.id);
	const ParamSchema schema = registrySchema(registries::kBehavior, instance.id);
	addSection(info ? text(info->label) : QString::fromStdString(instance.id), schema,
		   withDefaults(instance.params, schema), [this, index](const ParamBag &bag, const std::string &) {
			   if (index < static_cast<int>(config_.physics.extras.size()))
				   config_.physics.extras[static_cast<size_t>(index)].params = bag;
		   });
}

void AtomDesignerDialog::buildLinkSections(int index, bool withTarget)
{
	if (index < 0 || index >= static_cast<int>(config_.modulation.size()))
		return;
	const ModulationRoute &route = config_.modulation[static_cast<size_t>(index)];

	// The outline already carries the on/off tick; a pop-up opened from a setting already knows
	// what it drives.
	ParamSchema schema = withoutIds(schemaOf(routeFields()), {"enabled"});
	if (!withTarget)
		schema = withoutIds(schema, {"target"});

	addSection(text("Atom.Designer.Section.LinkEffect"), schema, bagSubset(toBag(route, routeFields()), schema),
		   [this, index](const ParamBag &bag, const std::string &id) {
			   if (index >= static_cast<int>(config_.modulation.size()))
				   return;
			   ModulationRoute &target = config_.modulation[static_cast<size_t>(index)];
			   fromBag(target, bag, routeFields());
			   if (id == "modulator") {
				   target.modulatorParams.clear();
				   target.modulatorParams.applyDefaults(
					   registrySchema(registries::kModulator, target.modulatorId));
				   scheduleInspectorRebuild();
			   }
			   if (id == "modulator" || id == "target")
				   scheduleOutlineRebuild();
		   });

	const ParamSchema input = registrySchema(registries::kModulator, route.modulatorId);
	addSection(text("Atom.Designer.Section.LinkInput"), input, withDefaults(route.modulatorParams, input),
		   [this, index](const ParamBag &bag, const std::string &) {
			   if (index < static_cast<int>(config_.modulation.size()))
				   config_.modulation[static_cast<size_t>(index)].modulatorParams = bag;
		   });

	// Audio links are silent until the emitter is told what to listen to, so offer that here
	// rather than making it a separate trip.
	if (route.modulatorId == "audio") {
		const ParamSchema source = withIds(schemaOf(audioFields()), {"audio_source"});
		addSection(
			text("Atom.Designer.Row.Audio"), source, bagSubset(toBag(config_.audio, audioFields()), source),
			[this](const ParamBag &bag, const std::string &) {
				fromBag(config_.audio, bag, audioFields());
				scheduleOutlineRebuild();
			},
			nullptr, audioSourceNames());
	}
}

void AtomDesignerDialog::refreshHeader()
{
	QString title;
	QString scope;
	QColor chip = palette().color(QPalette::Mid);
	bool round = false;

	switch (selection_.kind) {
	case Kind::Atom:
		if (const AtomLayer *layer = currentLayer()) {
			title = QString::fromStdString(layer->name);
			for (const auto &part : atomParts()) {
				if (part.first == selection_.part)
					title += QString(" %1 %2").arg(QString(QChar(0x203A)), text(part.second));
			}
			float total = 0.0f;
			for (const AtomLayer &other : config_.design.layers)
				total += other.enabled ? std::max(0.0f, other.weight) : 0.0f;
			if (!layer->enabled) {
				scope = text("Atom.Designer.Scope.AtomOff").arg(QString::fromStdString(layer->name));
			} else if (config_.design.layers.size() == 1 || total <= 0.0f) {
				scope = text("Atom.Designer.Scope.AtomOnly").arg(QString::fromStdString(layer->name));
			} else {
				const int share =
					static_cast<int>(std::max(0.0f, layer->weight) / total * 100.0f + 0.5f);
				scope = text("Atom.Designer.Scope.Atom")
						.arg(QString::fromStdString(layer->name))
						.arg(share);
			}
			chip = layerChipColor(*layer);
			round = true;
		}
		break;
	case Kind::Emitter:
		title = text("Atom.Designer.Group.Emitter");
		scope = text("Atom.Designer.Scope.Emitter");
		break;
	case Kind::Launch:
		title = text("Atom.Designer.Row.Launch");
		scope = text("Atom.Designer.Scope.Launch");
		break;
	case Kind::Destination:
		title = text("Atom.Designer.Row.Destination");
		scope = text("Atom.Designer.Scope.Destination");
		break;
	case Kind::Force:
		if (selection_.index >= 0 && selection_.index < static_cast<int>(config_.physics.extras.size())) {
			const std::string &id = config_.physics.extras[static_cast<size_t>(selection_.index)].id;
			const ModuleInfo *info = BehaviorRegistry::instance().info(id);
			title = info ? text(info->label) : QString::fromStdString(id);
		}
		scope = text("Atom.Designer.Scope.Force");
		break;
	case Kind::Audio:
		title = text("Atom.Designer.Row.Audio");
		scope = text("Atom.Designer.Scope.Audio");
		break;
	case Kind::Scene:
		title = text("Atom.Designer.Row.Scene");
		scope = text("Atom.Designer.Scope.Scene");
		break;
	case Kind::Link:
		if (selection_.index >= 0 && selection_.index < static_cast<int>(config_.modulation.size()))
			title = linkLabel(config_.modulation[static_cast<size_t>(selection_.index)]);
		scope = text("Atom.Designer.Scope.Link");
		chip = palette().color(QPalette::Highlight);
		break;
	case Kind::Output:
		title = text("Atom.Designer.Row.Output");
		scope = text("Atom.Designer.Scope.Output");
		break;
	default:
		break;
	}

	headerTitle_->setText(title);
	headerScope_->setText(scope);
	headerChip_->setPixmap(chipPixmap(chip, 16, round));
}

// ---------------------------------------------------------------------------------------------
// Life tracks
// ---------------------------------------------------------------------------------------------

void AtomDesignerDialog::refreshLifeTracks()
{
	const AtomLayer *layer = selection_.kind == Kind::Atom ? currentLayer() : nullptr;
	if (!layer) {
		// With nothing per-atom selected the panel shrinks to a one-line pointer, and the
		// preview gets the room.
		lifeHint_->setText(text("Atom.Designer.Life.None"));
		lifeStack_->setCurrentIndex(1);
		lifeStack_->setMaximumHeight(lifeHint_->sizeHint().height() + 16);
		return;
	}

	updating_ = true;
	lifeStack_->setMaximumHeight(QWIDGETSIZE_MAX);
	lifeStack_->setCurrentIndex(0);
	lifeTitle_->setText(text("Atom.Designer.Life.Title").arg(QString::fromStdString(layer->name)));
	lifeEnd_->setText(text("Atom.Designer.Life.End").arg(static_cast<double>(config_.physics.lifetime), 0, 'g', 3));

	const bool solid = layer->color.modeId == "solid";
	lifeColorStack_->setCurrentIndex(solid ? 1 : 0);
	lifeGradient_->setGradient(layer->color.gradient);
	lifeColor_->setColor(layer->color.color);
	lifeSize_->setCurve(layer->size.overLife);

	if (layer->fade.pathId == "custom") {
		lifeFade_->setCurve(layer->fade.customCurve);
	} else {
		// Built-in fades are functions, not curves. Sample one so it can be seen on the same
		// axis as everything else, and reshaped into the layer's own curve by dragging it.
		Curve sampled;
		std::unique_ptr<FadePath> path = FadePathRegistry::instance().create(layer->fade.pathId);
		if (path) {
			path->configure(withDefaults(layer->fade.pathParams,
						     registrySchema(registries::kFadePath, layer->fade.pathId)));
			const Atom probe;
			for (int i = 0; i < kFadeSamples; ++i) {
				const float t = static_cast<float>(i) / static_cast<float>(kFadeSamples - 1);
				sampled.points.push_back({t, saturate(path->alpha(t, probe))});
			}
		} else {
			sampled = Curve::linear(1.0f, 0.0f);
		}
		lifeFade_->setCurve(sampled);
	}
	updating_ = false;
}

// ---------------------------------------------------------------------------------------------
// Structure edits
// ---------------------------------------------------------------------------------------------

void AtomDesignerDialog::addAtom()
{
	AtomLayer layer;
	layer.id = makeLayerId();
	layer.name = text("Atom.Designer.NewAtomName").arg(config_.design.layers.size() + 1).toStdString();
	config_.design.layers.push_back(layer);

	Selection selection;
	selection.kind = Kind::Atom;
	selection.layerId = layer.id;
	configChanged();
	select(selection);
}

void AtomDesignerDialog::duplicateAtom()
{
	const AtomLayer *layer = currentLayer();
	if (!layer)
		return;
	AtomLayer copy = *layer;
	copy.id = makeLayerId();
	copy.name = text("Atom.Designer.CopyName").arg(QString::fromStdString(layer->name)).toStdString();
	config_.design.layers.push_back(copy);

	Selection selection;
	selection.kind = Kind::Atom;
	selection.layerId = copy.id;
	configChanged();
	select(selection);
}

void AtomDesignerDialog::removeAtom()
{
	const int index = currentLayerIndex();
	if (index < 0 || config_.design.layers.size() <= 1)
		return;
	config_.design.layers.erase(config_.design.layers.begin() + index);

	Selection selection;
	selection.kind = Kind::Atom;
	selection.layerId = config_.design.layers[static_cast<size_t>(std::max(0, index - 1))].id;
	configChanged();
	select(selection);
}

void AtomDesignerDialog::addForce(QTreeWidgetItem *anchor)
{
	QMenu menu(this);
	for (const auto &entry : registryEnumItems(registries::kBehavior))
		menu.addAction(text(entry.second))->setData(QString::fromStdString(entry.first));

	const QRect rect = anchor ? outline_->visualItemRect(anchor) : QRect();
	QAction *picked = menu.exec(outline_->viewport()->mapToGlobal(rect.bottomLeft()));
	if (!picked)
		return;

	BehaviorInstance instance;
	instance.id = picked->data().toString().toStdString();
	instance.params.applyDefaults(registrySchema(registries::kBehavior, instance.id));
	config_.physics.extras.push_back(std::move(instance));

	Selection selection;
	selection.kind = Kind::Force;
	selection.index = static_cast<int>(config_.physics.extras.size()) - 1;
	configChanged();
	select(selection);
}

void AtomDesignerDialog::removeForce()
{
	const int index = selection_.index;
	if (selection_.kind != Kind::Force || index < 0 || index >= static_cast<int>(config_.physics.extras.size()))
		return;
	config_.physics.extras.erase(config_.physics.extras.begin() + index);

	Selection selection;
	selection.kind = Kind::Launch;
	configChanged();
	select(selection);
}

void AtomDesignerDialog::addLink()
{
	ModulationRoute route;
	route.modulatorId = ModulatorRegistry::instance().defaultId();
	route.modulatorParams.applyDefaults(registrySchema(registries::kModulator, route.modulatorId));
	if (!modulationTargets().empty())
		route.target = modulationTargets().front().first;
	config_.modulation.push_back(std::move(route));

	Selection selection;
	selection.kind = Kind::Link;
	selection.index = static_cast<int>(config_.modulation.size()) - 1;
	configChanged();
	select(selection);
}

void AtomDesignerDialog::removeLink()
{
	const int index = selection_.index;
	if (selection_.kind != Kind::Link || index < 0 || index >= static_cast<int>(config_.modulation.size()))
		return;
	config_.modulation.erase(config_.modulation.begin() + index);

	Selection selection;
	selection.kind = Kind::Audio;
	configChanged();
	select(selection);
}

void AtomDesignerDialog::requestLink(const std::string &target)
{
	int index = -1;
	for (size_t i = 0; i < config_.modulation.size(); ++i) {
		if (config_.modulation[i].target == target) {
			index = static_cast<int>(i);
			break;
		}
	}

	if (index < 0) {
		ModulationRoute route;
		route.modulatorId = ModulatorRegistry::instance().defaultId();
		route.modulatorParams.applyDefaults(registrySchema(registries::kModulator, route.modulatorId));
		route.target = target;
		route.modeId = "add";
		// Start by doubling the value at full input, which is visible in any units. A setting
		// that sits at zero has nothing to double, so it gets a tenth of its slider instead.
		double base = 0.0;
		double minimum = 0.0;
		double maximum = 1.0;
		if (targetValue(config_, target, base) && std::abs(base) > 1e-6)
			route.amount = static_cast<float>(base);
		else if (targetRange(target, minimum, maximum))
			route.amount = static_cast<float>((maximum - minimum) * 0.1);
		config_.modulation.push_back(std::move(route));
		index = static_cast<int>(config_.modulation.size()) - 1;
		configChanged();
	}

	editLink(index);
}

void AtomDesignerDialog::editLink(int index)
{
	if (index < 0 || index >= static_cast<int>(config_.modulation.size()))
		return;
	const std::string target = config_.modulation[static_cast<size_t>(index)].target;
	const QString name = targetLabel(target);

	QDialog dialog(this);
	dialog.setObjectName("atomLinkDialog");
	dialog.setWindowTitle(text("Atom.Designer.Link.Title").arg(name));
	dialog.setMinimumWidth(440);

	QLabel *heading = new QLabel(text("Atom.Designer.Link.Title").arg(name), &dialog);
	QFont headingFont = heading->font();
	headingFont.setBold(true);
	headingFont.setPointSizeF(headingFont.pointSizeF() * 1.15);
	heading->setFont(headingFont);

	QWidget *body = new QWidget(&dialog);
	QVBoxLayout *bodyLayout = new QVBoxLayout(body);
	bodyLayout->setContentsMargins(0, 0, 0, 0);

	QLabel *range = new QLabel(&dialog);
	range->setWordWrap(true);

	QPushButton *stop = new QPushButton(text("Atom.Designer.Link.Stop").arg(name), &dialog);
	QPushButton *done = new QPushButton(text("Atom.Designer.Done"), &dialog);
	stop->setAutoDefault(false);
	QHBoxLayout *buttons = new QHBoxLayout();
	buttons->addWidget(stop);
	buttons->addStretch(1);
	buttons->addWidget(done);

	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	layout->addWidget(heading);
	layout->addWidget(makeCaption(text("Atom.Designer.Link.Caption"), &dialog));
	layout->addWidget(body);
	layout->addWidget(range);
	layout->addLayout(buttons);

	// Says, in the setting's own units, how far the link will move it.
	popupChanged_ = [this, index, target, name, range] {
		double base = 0.0;
		if (index >= static_cast<int>(config_.modulation.size()) || !targetValue(config_, target, base)) {
			range->clear();
			return;
		}
		const ModulationRoute &route = config_.modulation[static_cast<size_t>(index)];
		double peak = base + route.amount;
		if (route.modeId == "multiply")
			peak = base * route.amount;
		else if (route.modeId == "replace")
			peak = route.amount;
		range->setText(text("Atom.Designer.Link.Range")
				       .arg(name)
				       .arg(std::min(base, peak), 0, 'g', 4)
				       .arg(std::max(base, peak), 0, 'g', 4));
	};
	popupRebuild_ = [this, index, bodyLayout] {
		const bool wasUpdating = updating_;
		updating_ = true;
		clearLayout(bodyLayout);
		sectionTarget_ = bodyLayout;
		buildLinkSections(index, false);
		sectionTarget_ = inspectorLayout_;
		updating_ = wasUpdating;
	};
	popupRebuild_();
	popupChanged_();

	bool removed = false;
	connect(stop, &QPushButton::clicked, &dialog, [&] {
		removed = true;
		dialog.accept();
	});
	connect(done, &QPushButton::clicked, &dialog, &QDialog::accept);
	dialog.exec();

	popupRebuild_ = nullptr;
	popupChanged_ = nullptr;

	if (removed && index < static_cast<int>(config_.modulation.size())) {
		config_.modulation.erase(config_.modulation.begin() + index);
		if (selection_.kind == Kind::Link)
			selection_ = Selection{};
		configChanged();
	}

	// The setting's link button and the outline's list of links both need to catch up.
	rebuildOutline();
	rebuildInspector();
}

// ---------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------

AtomLayer *AtomDesignerDialog::currentLayer()
{
	if (selection_.kind != Kind::Atom)
		return nullptr;
	return config_.design.findLayer(selection_.layerId);
}

int AtomDesignerDialog::currentLayerIndex()
{
	if (selection_.kind != Kind::Atom)
		return -1;
	for (size_t i = 0; i < config_.design.layers.size(); ++i) {
		if (config_.design.layers[i].id == selection_.layerId)
			return static_cast<int>(i);
	}
	return -1;
}

void AtomDesignerDialog::refreshEverything()
{
	rebuildOutline();
	rebuildInspector();
	refreshLifeTracks();
	refreshPreview();
	refreshPreviewDecor();
	refreshTitles();
}

void AtomDesignerDialog::refreshTitles()
{
	SourceHold hold(weakSource_);
	const char *sourceName = hold ? obs_source_get_name(hold.source) : nullptr;
	const QString name = QString::fromUtf8(sourceName ? sourceName : "");

	setWindowTitle(QString("%1 - %2").arg(text("Atom.Designer.Title"), name));
	title_->setText(name.isEmpty() ? text("Atom.Designer.Title") : name);

	// A design keeps the name of the preset it came from; any edit since then is worth saying.
	const QString design = QString::fromStdString(config_.design.name);
	subtitle_->setText(text(config_.design.presetId.empty() ? "Atom.Designer.Subtitle.Edited"
								: "Atom.Designer.Subtitle.Preset")
				   .arg(design));

	undoButton_->setEnabled(!undo_.empty() || checkpointTimer_->isActive());
	redoButton_->setEnabled(!redo_.empty());
}

void AtomDesignerDialog::refreshPreview()
{
	if (!preview_)
		return;

	EmitterConfig previewConfig = config_;
	previewConfig.emission.randomSeed = true;
	preview_->setConfig(previewConfig);
}

void AtomDesignerDialog::refreshPreviewDecor()
{
	if (!preview_)
		return;

	AtomPreview::Overlay overlay = AtomPreview::Overlay::None;
	const char *hint = "Atom.Designer.Stage.Default";
	int highlight = -1;

	switch (selection_.kind) {
	case Kind::Atom:
		// With a single kind of atom there is nothing to tell apart.
		if (config_.design.layers.size() > 1) {
			highlight = currentLayerIndex();
			hint = "Atom.Designer.Stage.Atom";
		}
		break;
	case Kind::Emitter:
		overlay = AtomPreview::Overlay::Shape;
		hint = "Atom.Designer.Stage.Emitter";
		break;
	case Kind::Launch:
		overlay = AtomPreview::Overlay::Launch;
		hint = config_.emission.directionModeId == "angle" ? "Atom.Designer.Stage.Launch"
								   : "Atom.Designer.Stage.LaunchOther";
		break;
	case Kind::Destination:
		overlay = AtomPreview::Overlay::Target;
		hint = config_.physics.endpoint.modeId == "point" ? "Atom.Designer.Stage.Destination"
								  : "Atom.Designer.Stage.Default";
		break;
	default:
		break;
	}

	preview_->setOverlay(overlay);
	preview_->setHighlightLayer(highlight);

	QString message = text(hint);
	if (selection_.kind == Kind::Atom && highlight >= 0) {
		if (const AtomLayer *layer = currentLayer())
			message = message.arg(QString::fromStdString(layer->name));
	}
	previewHint_->setText(message);
}

void AtomDesignerDialog::onPreviewDragged(const QPointF &position)
{
	if (!preview_)
		return;

	if (selection_.kind == Kind::Launch) {
		if (config_.emission.directionModeId != "angle")
			return;
		// Aim from the middle of the emitter shape towards the pointer.
		const QPointF delta = position - preview_->shapeCenter();
		if (delta.manhattanLength() < 2.0)
			return;
		const float angle = rad2deg(std::atan2(static_cast<float>(delta.y()), static_cast<float>(delta.x())));
		config_.physics.emitAngle = std::round(angle);
	} else if (selection_.kind == Kind::Destination) {
		if (config_.physics.endpoint.modeId != "point")
			return;
		const float width = std::max(1.0f, static_cast<float>(config_.emission.width));
		const float height = std::max(1.0f, static_cast<float>(config_.emission.height));
		config_.physics.endpoint.point = Vec2{saturate(static_cast<float>(position.x()) / width),
						      saturate(static_cast<float>(position.y()) / height)};
	} else {
		return;
	}

	// The sliders show the same value the drag just changed.
	scheduleInspectorRebuild();
	configChanged();
}

void AtomDesignerDialog::configChanged()
{
	// The first edit after a pause opens a new undo step; the ones that follow closely join it.
	if (!checkpointTimer_->isActive()) {
		undo_.push_back(committed_);
		if (undo_.size() > kUndoLimit)
			undo_.erase(undo_.begin());
		redo_.clear();
	}
	checkpointTimer_->start();

	config_.design.presetId.clear();
	refreshPreview();
	refreshTitles();
	if (popupChanged_)
		popupChanged_();

	// Start only when idle: restarting on every edit would hold the write back for as long as
	// a slider keeps moving.
	if (liveButton_->isChecked() && !applyTimer_->isActive())
		applyTimer_->start();
}

void AtomDesignerDialog::applyToSource()
{
	applyTimer_->stop();

	SourceHold hold(weakSource_);
	if (!hold)
		return;
	applyConfigToSource(hold.source, config_);
}

void AtomDesignerDialog::checkpoint()
{
	checkpointTimer_->stop();
	committed_ = config_;
}

void AtomDesignerDialog::restore(const EmitterConfig &config)
{
	config_ = config;
	if (config_.design.layers.empty())
		config_.design = AtomDesign::defaultDesign();
	committed_ = config_;
	checkpointTimer_->stop();

	refreshEverything();
	if (liveButton_->isChecked())
		applyToSource();
}

void AtomDesignerDialog::undo()
{
	// An edit still inside its merge window is a step of its own as far as the user can tell.
	if (checkpointTimer_->isActive())
		checkpointTimer_->stop();
	else if (undo_.empty())
		return;
	if (undo_.empty())
		return;

	const EmitterConfig previous = undo_.back();
	undo_.pop_back();
	redo_.push_back(config_);
	restore(previous);
}

void AtomDesignerDialog::redo()
{
	if (redo_.empty())
		return;
	checkpoint();

	const EmitterConfig next = redo_.back();
	redo_.pop_back();
	undo_.push_back(config_);
	restore(next);
}

void AtomDesignerDialog::reloadFromSource()
{
	SourceHold hold(weakSource_);
	if (!hold)
		return;

	// Reloading discards unapplied edits, including one still waiting to be written.
	applyTimer_->stop();
	undo_.clear();
	redo_.clear();
	opened_ = configOfSource(hold.source);
	restore(opened_);
}

// ---------------------------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------------------------

void AtomDesignerDialog::placePresetPanel()
{
	// Under its button, over the outline, and only as far into the preview as it needs.
	const QPoint anchor = presetsButton_->mapTo(this, QPoint(0, presetsButton_->height() + 4));
	const int width = std::min(560, this->width() - anchor.x() - 12);
	const int height = std::min(520, this->height() - anchor.y() - 12);
	presetPanel_->setGeometry(anchor.x(), anchor.y(), std::max(280, width), std::max(240, height));
}

void AtomDesignerDialog::togglePresets()
{
	if (presetPanel_->isVisible()) {
		presetPanel_->cancel();
		return;
	}

	PresetPanel::Callbacks callbacks;
	callbacks.preview = [this](const EmitterConfig *candidate) {
		if (!preview_)
			return;
		if (!candidate) {
			refreshPreview();
			refreshPreviewDecor();
			return;
		}
		// Presets describe a look and its motion, not a canvas size: keep whatever the source is.
		EmitterConfig shown = *candidate;
		shown.emission.width = config_.emission.width;
		shown.emission.height = config_.emission.height;
		shown.emission.randomSeed = true;
		preview_->setOverlay(AtomPreview::Overlay::None);
		preview_->setHighlightLayer(-1);
		preview_->setConfig(shown);
		preview_->restart();
		previewHint_->setText(text("Atom.Designer.Stage.Trying"));
	};
	callbacks.use = [this](const EmitterConfig &chosen, const std::string &presetId) {
		presetsButton_->setChecked(false);
		usePreset(chosen, presetId);
	};
	callbacks.cancelled = [this] {
		presetsButton_->setChecked(false);
		refreshPreview();
		refreshPreviewDecor();
	};

	presetsButton_->setChecked(true);
	placePresetPanel();
	presetPanel_->open(config_, opened_, std::move(callbacks));
}

void AtomDesignerDialog::usePreset(const EmitterConfig &chosen, const std::string &presetId)
{
	// One undo step takes the whole replacement back.
	checkpoint();
	undo_.push_back(config_);
	if (undo_.size() > kUndoLimit)
		undo_.erase(undo_.begin());
	redo_.clear();

	const uint32_t width = config_.emission.width;
	const uint32_t height = config_.emission.height;
	EmitterConfig next = chosen;
	next.emission.width = width;
	next.emission.height = height;
	// Reverting brings back whatever preset the opened effect was based on, if any.
	if (!presetId.empty())
		next.design.presetId = presetId;

	selection_ = Selection{};
	restore(next);
	if (preview_)
		preview_->restart();
}

void AtomDesignerDialog::saveCurrentAsPreset()
{
	bool ok = false;
	const QString name = QInputDialog::getText(this, text("Atom.Designer.SavePreset"),
						   text("Atom.Designer.PresetName"), QLineEdit::Normal,
						   QString::fromStdString(config_.design.name), &ok);
	if (!ok || name.trimmed().isEmpty())
		return;

	AtomPreset preset;
	preset.name = name.trimmed().toStdString();
	preset.id = "user_" + preset.name;
	preset.category = "user";
	preset.config = config_;
	preset.config.design.name = preset.name;
	preset.config.design.presetId = preset.id;

	std::string error;
	if (!PresetStore::instance().save(preset, &error)) {
		QMessageBox::warning(this, text("Atom.Designer.SavePreset"), QString::fromStdString(error));
		return;
	}

	// The effect being edited now is that preset, by name and by content.
	config_.design.name = preset.name;
	config_.design.presetId = preset.id;
	committed_ = config_;
	refreshTitles();
}

QStringList AtomDesignerDialog::audioSourceNames() const
{
	QStringList names;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			QStringList *list = static_cast<QStringList *>(param);
			if ((obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) == 0)
				return true;
			const char *name = obs_source_get_name(source);
			if (name && *name)
				list->append(QString::fromUtf8(name));
			return true;
		},
		&names);
	names.sort(Qt::CaseInsensitive);
	return names;
}

QStringList AtomDesignerDialog::videoSourceNames() const
{
	QStringList names;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			QStringList *list = static_cast<QStringList *>(param);
			if ((obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) == 0)
				return true;
			const char *name = obs_source_get_name(source);
			if (name && *name)
				list->append(QString::fromUtf8(name));
			return true;
		},
		&names);
	names.sort(Qt::CaseInsensitive);
	return names;
}

} // namespace ui
} // namespace atom
