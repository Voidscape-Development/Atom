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
#include "atom-preview.hpp"
#include "obs/atom-preset-store.hpp"
#include "obs/atom-source.hpp"
#include "atom-core/atom-modules.hpp"
#include "atom-core/atom-modulation.hpp"
#include "atom-core/atom-registry.hpp"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <obs-module.h>

namespace atom {
namespace ui {

namespace {

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

ParamSchema filterByGroup(const ParamSchema &schema, const std::string &group)
{
	ParamSchema filtered;
	for (const ParamSpec &spec : schema) {
		if (spec.group == group)
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

QWidget *wrapInScrollArea(QWidget *content)
{
	QScrollArea *area = new QScrollArea();
	area->setWidgetResizable(true);
	area->setFrameShape(QFrame::NoFrame);
	area->setWidget(content);
	return area;
}

QListWidgetItem *makeHeaderItem(const QString &text)
{
	QListWidgetItem *item = new QListWidgetItem(text);
	item->setFlags(Qt::NoItemFlags);
	QFont font = item->font();
	font.setBold(true);
	item->setFont(font);
	item->setForeground(QColor(150, 150, 160));
	return item;
}

} // namespace

AtomDesignerDialog::AtomDesignerDialog(obs_source_t *source, QWidget *parent) : QDialog(parent)
{
	registerBuiltinModules();

	weakSource_ = source ? obs_source_get_weak_source(source) : nullptr;
	config_ = configOfSource(source);
	if (config_.design.layers.empty())
		config_.design = AtomDesign::defaultDesign();
	currentLayerId_ = config_.design.layers.front().id;

	const char *name = source ? obs_source_get_name(source) : nullptr;
	setWindowTitle(QString("%1 - %2").arg(text("Atom.Designer.Title"), QString::fromUtf8(name ? name : "")));
	setWindowFlags(windowFlags() | Qt::Window);
	resize(1060, 720);

	// Left: categories and sections. Right: the active page. Bottom: live preview.
	sidebar_ = new QListWidget(this);
	sidebar_->setFixedWidth(210);
	sidebar_->setUniformItemSizes(false);
	sidebar_->setSpacing(1);

	title_ = new QLabel(this);
	QFont titleFont = title_->font();
	titleFont.setPointSizeF(titleFont.pointSizeF() * 1.35);
	titleFont.setBold(true);
	title_->setFont(titleFont);

	subtitle_ = new QLabel(this);
	subtitle_->setWordWrap(true);
	subtitle_->setStyleSheet("color: palette(mid);");

	stack_ = new QStackedWidget(this);

	QVBoxLayout *rightLayout = new QVBoxLayout();
	rightLayout->setContentsMargins(12, 8, 8, 8);
	rightLayout->addWidget(title_);
	rightLayout->addWidget(subtitle_);
	rightLayout->addSpacing(6);
	rightLayout->addWidget(stack_, 1);

	QWidget *rightPanel = new QWidget(this);
	rightPanel->setLayout(rightLayout);

	QHBoxLayout *topLayout = new QHBoxLayout();
	topLayout->setContentsMargins(0, 0, 0, 0);
	topLayout->addWidget(sidebar_);
	topLayout->addWidget(rightPanel, 1);

	QWidget *topPanel = new QWidget(this);
	topPanel->setLayout(topLayout);

	// Preview strip.
	preview_ = new AtomPreview(this);
	QPushButton *restart = new QPushButton(text("Atom.Designer.Restart"), this);
	QPushButton *pause = new QPushButton(text("Atom.Designer.Pause"), this);
	pause->setCheckable(true);
	liveApply_ = new QCheckBox(text("Atom.Designer.LiveApply"), this);
	liveApply_->setChecked(true);

	QVBoxLayout *previewSide = new QVBoxLayout();
	previewSide->addWidget(new QLabel(text("Atom.Designer.Preview"), this));
	previewSide->addWidget(restart);
	previewSide->addWidget(pause);
	previewSide->addWidget(liveApply_);
	previewSide->addStretch(1);

	QHBoxLayout *previewLayout = new QHBoxLayout();
	previewLayout->addWidget(preview_, 1);
	previewLayout->addLayout(previewSide);

	QGroupBox *previewBox = new QGroupBox(text("Atom.Designer.LivePreview"), this);
	previewBox->setLayout(previewLayout);

	QSplitter *splitter = new QSplitter(Qt::Vertical, this);
	splitter->addWidget(topPanel);
	splitter->addWidget(previewBox);
	splitter->setStretchFactor(0, 3);
	splitter->setStretchFactor(1, 2);

	QDialogButtonBox *buttons = new QDialogButtonBox(this);
	QPushButton *apply = buttons->addButton(text("Atom.Designer.Apply"), QDialogButtonBox::ApplyRole);
	QPushButton *revert = buttons->addButton(text("Atom.Designer.Revert"), QDialogButtonBox::ResetRole);
	buttons->addButton(QDialogButtonBox::Close);

	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	mainLayout->addWidget(splitter, 1);
	mainLayout->addWidget(buttons);

	// Pages, in sidebar order.
	addPage("presets", buildPresetPage());
	addPage("layers", buildLayerPage());
	addPage("color", buildLayerSectionPage("color", false, {}, {}));
	addPage("bloom", buildLayerSectionPage("bloom", false, {}, {}));
	addPage("size", buildLayerSectionPage("size", false, {}, {}));
	addPage("trail", buildLayerSectionPage("trail", true, registries::kTrail, "trail_style"));
	addPage("fade", buildLayerSectionPage("fade", true, registries::kFadePath, "fade_path"));
	addPage("sheet", buildLayerSectionPage("sheet", false, {}, {}));
	addPage("emission", buildEmissionPage());
	addPage("motion", buildPhysicsSectionPage("motion", false));
	addPage("life", buildPhysicsSectionPage("life", true));
	addPage("endpoint", buildPhysicsSectionPage("endpoint", false));
	addPage("offset", buildPhysicsSectionPage("offset", false));
	addPage("forces", buildForcesPage());
	addPage("render",
		buildBagPage(
			schemaOf(renderFields()), [this] { return toBag(config_.render, renderFields()); },
			[this](const ParamBag &bag, const QString &) { fromBag(config_.render, bag, renderFields()); },
			false));
	addPage("scene",
		buildBagPage(
			schemaOf(sceneFields()), [this] { return toBag(config_.scene, sceneFields()); },
			[this](const ParamBag &bag, const QString &) { fromBag(config_.scene, bag, sceneFields()); },
			false));
	addPage("audio",
		buildBagPage(
			schemaOf(audioFields()), [this] { return toBag(config_.audio, audioFields()); },
			[this](const ParamBag &bag, const QString &) { fromBag(config_.audio, bag, audioFields()); },
			true));
	addPage("modulation", buildModulationPage());

	buildSidebar();

	connect(sidebar_, &QListWidget::currentItemChanged, this, &AtomDesignerDialog::onSidebarChanged);
	connect(restart, &QPushButton::clicked, this, [this] {
		if (preview_)
			preview_->restart();
	});
	connect(pause, &QPushButton::toggled, this, [this, pause](bool checked) {
		if (preview_)
			preview_->setPlaying(!checked);
		pause->setText(checked ? text("Atom.Designer.Resume") : text("Atom.Designer.Pause"));
	});
	connect(apply, &QPushButton::clicked, this, [this] { applyToSource(); });
	connect(revert, &QPushButton::clicked, this, [this] { reloadFromSource(); });
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

	sidebar_->setCurrentRow(1);
	refreshPreview();
}

AtomDesignerDialog::~AtomDesignerDialog()
{
	if (weakSource_)
		obs_weak_source_release(weakSource_);
}

obs_source_t *AtomDesignerDialog::acquireSource() const
{
	return weakSource_ ? obs_weak_source_get_source(weakSource_) : nullptr;
}

void AtomDesignerDialog::addPage(const std::string &id, QWidget *widget)
{
	pageIndex_[id] = stack_->addWidget(widget);
}

void AtomDesignerDialog::buildSidebar()
{
	const auto addItem = [this](const QString &label, const std::string &pageId, const std::string &category) {
		QListWidgetItem *item = new QListWidgetItem(label);
		item->setData(Qt::UserRole, QString::fromStdString(pageId));
		item->setData(Qt::UserRole + 1, QString::fromStdString(category));
		sidebar_->addItem(item);
		return item;
	};

	sidebar_->addItem(makeHeaderItem(text("Atom.Designer.Section.Presets")));
	for (const auto &category : presetCategories())
		addItem(text(category.second), "presets", category.first);

	sidebar_->addItem(makeHeaderItem(text("Atom.Designer.Section.Design")));
	addItem(text("Atom.Designer.Page.Layers"), "layers", {});
	addItem(text("Atom.Designer.Page.Color"), "color", {});
	addItem(text("Atom.Designer.Page.Bloom"), "bloom", {});
	addItem(text("Atom.Designer.Page.Size"), "size", {});
	addItem(text("Atom.Designer.Page.Trail"), "trail", {});
	addItem(text("Atom.Designer.Page.Fade"), "fade", {});
	addItem(text("Atom.Designer.Page.Sheet"), "sheet", {});

	sidebar_->addItem(makeHeaderItem(text("Atom.Designer.Section.Physics")));
	addItem(text("Atom.Designer.Page.Emission"), "emission", {});
	addItem(text("Atom.Designer.Page.Motion"), "motion", {});
	addItem(text("Atom.Designer.Page.Lifetime"), "life", {});
	addItem(text("Atom.Designer.Page.Endpoint"), "endpoint", {});
	addItem(text("Atom.Designer.Page.Offset"), "offset", {});
	addItem(text("Atom.Designer.Page.Forces"), "forces", {});

	sidebar_->addItem(makeHeaderItem(text("Atom.Designer.Section.Reactivity")));
	addItem(text("Atom.Designer.Page.Render"), "render", {});
	addItem(text("Atom.Designer.Page.Scene"), "scene", {});
	addItem(text("Atom.Designer.Page.Audio"), "audio", {});
	addItem(text("Atom.Designer.Page.Modulation"), "modulation", {});
}

void AtomDesignerDialog::onSidebarChanged()
{
	QListWidgetItem *item = sidebar_->currentItem();
	if (!item)
		return;

	const std::string pageId = item->data(Qt::UserRole).toString().toStdString();
	const auto it = pageIndex_.find(pageId);
	if (it == pageIndex_.end())
		return;

	stack_->setCurrentIndex(it->second);
	title_->setText(item->text());
	subtitle_->setText(text("Atom.Designer.Hint." + pageId));

	if (pageId == "presets")
		refreshPresetGrid();
	else
		refreshAllPages();
}

// -------------------------------------------------------------------------------------------
// Pages
// -------------------------------------------------------------------------------------------

QWidget *AtomDesignerDialog::buildPresetPage()
{
	QWidget *page = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(page);

	presetGrid_ = new QListWidget(page);
	presetGrid_->setViewMode(QListView::IconMode);
	presetGrid_->setIconSize(QSize(150, 94));
	presetGrid_->setGridSize(QSize(168, 140));
	presetGrid_->setResizeMode(QListView::Adjust);
	presetGrid_->setMovement(QListView::Static);
	presetGrid_->setWordWrap(true);
	presetGrid_->setSpacing(6);

	QPushButton *apply = new QPushButton(text("Atom.Designer.ApplyPreset"), page);
	QPushButton *save = new QPushButton(text("Atom.Designer.SavePreset"), page);
	deletePreset_ = new QPushButton(text("Atom.Designer.DeletePreset"), page);
	deletePreset_->setEnabled(false);

	QHBoxLayout *buttons = new QHBoxLayout();
	buttons->addWidget(apply);
	buttons->addWidget(save);
	buttons->addWidget(deletePreset_);
	buttons->addStretch(1);

	layout->addWidget(presetGrid_, 1);
	layout->addLayout(buttons);

	connect(presetGrid_, &QListWidget::itemDoubleClicked, this, &AtomDesignerDialog::onPresetActivated);
	connect(presetGrid_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
		selectedPresetId_ = item ? item->data(Qt::UserRole).toString().toStdString() : std::string();
		const AtomPreset *preset = PresetStore::instance().find(selectedPresetId_);
		deletePreset_->setEnabled(preset && !preset->builtin);
	});
	connect(apply, &QPushButton::clicked, this, [this] {
		if (const AtomPreset *preset = PresetStore::instance().find(selectedPresetId_))
			applyPreset(*preset);
	});
	connect(save, &QPushButton::clicked, this, &AtomDesignerDialog::saveCurrentAsPreset);
	connect(deletePreset_, &QPushButton::clicked, this, &AtomDesignerDialog::deleteSelectedPreset);

	return page;
}

QWidget *AtomDesignerDialog::buildLayerPage()
{
	QWidget *page = new QWidget();
	QHBoxLayout *layout = new QHBoxLayout(page);

	layerList_ = new QListWidget(page);
	layerList_->setFixedWidth(200);

	QPushButton *add = new QPushButton(text("Atom.Designer.AddLayer"), page);
	QPushButton *duplicate = new QPushButton(text("Atom.Designer.DuplicateLayer"), page);
	QPushButton *remove = new QPushButton(text("Atom.Designer.RemoveLayer"), page);

	QVBoxLayout *listLayout = new QVBoxLayout();
	listLayout->addWidget(layerList_, 1);
	listLayout->addWidget(add);
	listLayout->addWidget(duplicate);
	listLayout->addWidget(remove);

	ParamEditor *editor = new ParamEditor(page);
	ParamEditor *spriteEditor = new ParamEditor(page);

	QWidget *content = new QWidget();
	QVBoxLayout *contentLayout = new QVBoxLayout(content);
	contentLayout->addWidget(editor);

	QGroupBox *spriteBox = new QGroupBox(text("Atom.Designer.SpriteOptions"), content);
	QVBoxLayout *spriteLayout = new QVBoxLayout(spriteBox);
	spriteLayout->addWidget(spriteEditor);
	contentLayout->addWidget(spriteBox);
	contentLayout->addStretch(1);

	layout->addLayout(listLayout);
	layout->addWidget(wrapInScrollArea(content), 1);

	const auto refresh = [this, editor, spriteEditor] {
		AtomLayer *layer = currentLayer();
		if (!layer)
			return;

		updating_ = true;
		const ParamSchema schema = filterByGroup(schemaOf(layerFields()), "layer");
		editor->setSchema(schema, bagSubset(toBag(*layer, layerFields()), schema));
		spriteEditor->setSchema(registrySchema(registries::kSprite, layer->spriteId), layer->spriteParams);
		updating_ = false;
	};

	connect(editor, &ParamEditor::valueChanged, this, [this, editor, spriteEditor](const QString &id) {
		AtomLayer *layer = currentLayer();
		if (!layer || updating_)
			return;

		fromBag(*layer, editor->values(), layerFields());
		if (id == "sprite") {
			layer->spriteParams.clear();
			layer->spriteParams.applyDefaults(registrySchema(registries::kSprite, layer->spriteId));
			spriteEditor->setSchema(registrySchema(registries::kSprite, layer->spriteId),
						layer->spriteParams);
		}
		refreshLayerList();
		configChanged();
	});
	connect(spriteEditor, &ParamEditor::valueChanged, this, [this, spriteEditor](const QString &) {
		AtomLayer *layer = currentLayer();
		if (!layer || updating_)
			return;
		layer->spriteParams = spriteEditor->values();
		configChanged();
	});

	connect(layerList_, &QListWidget::currentItemChanged, this, [this, refresh](QListWidgetItem *item) {
		if (!item || updating_)
			return;
		currentLayerId_ = item->data(Qt::UserRole).toString().toStdString();
		refreshAllPages();
		refresh();
	});

	connect(add, &QPushButton::clicked, this, [this, refresh] {
		AtomLayer layer;
		layer.id = makeLayerId();
		layer.name = "Atom " + std::to_string(config_.design.layers.size() + 1);
		config_.design.layers.push_back(layer);
		currentLayerId_ = layer.id;
		refreshLayerList();
		refresh();
		configChanged();
	});
	connect(duplicate, &QPushButton::clicked, this, [this, refresh] {
		AtomLayer *layer = currentLayer();
		if (!layer)
			return;
		AtomLayer copy = *layer;
		copy.id = makeLayerId();
		copy.name = layer->name + " copy";
		config_.design.layers.push_back(copy);
		currentLayerId_ = copy.id;
		refreshLayerList();
		refresh();
		configChanged();
	});
	connect(remove, &QPushButton::clicked, this, [this, refresh] {
		if (config_.design.layers.size() <= 1)
			return;
		for (size_t i = 0; i < config_.design.layers.size(); ++i) {
			if (config_.design.layers[i].id != currentLayerId_)
				continue;
			config_.design.layers.erase(config_.design.layers.begin() + static_cast<long>(i));
			break;
		}
		currentLayerId_ = config_.design.layers.front().id;
		refreshLayerList();
		refresh();
		configChanged();
	});

	// The layer list drives the editors, so both refresh together whenever the page is shown.
	layerPageRefresh_ = [this, refresh] {
		refreshLayerList();
		refresh();
	};

	return page;
}

QWidget *AtomDesignerDialog::buildLayerSectionPage(const std::string &group, bool withModuleParams,
						   const std::string &registryName, const std::string &moduleSelectorId)
{
	QWidget *content = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(content);

	ParamEditor *editor = new ParamEditor(content);
	layout->addWidget(editor);

	ParamEditor *moduleEditor = nullptr;
	if (withModuleParams) {
		QGroupBox *box = new QGroupBox(text("Atom.Designer.StyleOptions"), content);
		QVBoxLayout *boxLayout = new QVBoxLayout(box);
		moduleEditor = new ParamEditor(box);
		boxLayout->addWidget(moduleEditor);
		layout->addWidget(box);
	}
	layout->addStretch(1);

	const std::string selector = moduleSelectorId;

	const auto moduleIdFor = [registryName](const AtomLayer &layer) {
		if (registryName == registries::kTrail)
			return layer.trail.styleId;
		if (registryName == registries::kFadePath)
			return layer.fade.pathId;
		return std::string();
	};
	const auto moduleParamsFor = [registryName](AtomLayer &layer) -> ParamBag & {
		return registryName == registries::kTrail ? layer.trail.styleParams : layer.fade.pathParams;
	};

	const auto refresh = [this, editor, moduleEditor, group, registryName, moduleIdFor, moduleParamsFor] {
		AtomLayer *layer = currentLayer();
		if (!layer)
			return;

		updating_ = true;
		const ParamSchema schema = filterByGroup(schemaOf(layerFields()), group);
		editor->setSchema(schema, bagSubset(toBag(*layer, layerFields()), schema));
		if (moduleEditor) {
			const std::string moduleId = moduleIdFor(*layer);
			moduleEditor->setSchema(registrySchema(registryName, moduleId), moduleParamsFor(*layer));
		}
		updating_ = false;
	};

	connect(editor, &ParamEditor::valueChanged, this,
		[this, editor, moduleEditor, registryName, selector, moduleIdFor, moduleParamsFor](const QString &id) {
			AtomLayer *layer = currentLayer();
			if (!layer || updating_)
				return;

			fromBag(*layer, editor->values(), layerFields());

			if (moduleEditor && id.toStdString() == selector) {
				const std::string moduleId = moduleIdFor(*layer);
				ParamBag &params = moduleParamsFor(*layer);
				params.clear();
				params.applyDefaults(registrySchema(registryName, moduleId));
				moduleEditor->setSchema(registrySchema(registryName, moduleId), params);
			}
			configChanged();
		});

	if (moduleEditor) {
		connect(moduleEditor, &ParamEditor::valueChanged, this,
			[this, moduleEditor, moduleParamsFor](const QString &) {
				AtomLayer *layer = currentLayer();
				if (!layer || updating_)
					return;
				moduleParamsFor(*layer) = moduleEditor->values();
				configChanged();
			});
	}

	sectionRefreshers_.push_back(refresh);
	return wrapInScrollArea(content);
}

QWidget *AtomDesignerDialog::buildEmissionPage()
{
	QWidget *content = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(content);

	ParamEditor *editor = new ParamEditor(content);
	layout->addWidget(editor);

	QGroupBox *box = new QGroupBox(text("Atom.Designer.ShapeOptions"), content);
	QVBoxLayout *boxLayout = new QVBoxLayout(box);
	ParamEditor *shapeEditor = new ParamEditor(box);
	boxLayout->addWidget(shapeEditor);
	layout->addWidget(box);
	layout->addStretch(1);

	const auto refresh = [this, editor, shapeEditor] {
		updating_ = true;
		editor->setSchema(schemaOf(emissionFields()), toBag(config_.emission, emissionFields()));
		shapeEditor->setSchema(registrySchema(registries::kEmitterShape, config_.emission.shapeId),
				       config_.emission.shapeParams);
		updating_ = false;
	};

	connect(editor, &ParamEditor::valueChanged, this, [this, editor, shapeEditor](const QString &id) {
		if (updating_)
			return;
		fromBag(config_.emission, editor->values(), emissionFields());
		if (id == "shape") {
			config_.emission.shapeParams.clear();
			config_.emission.shapeParams.applyDefaults(
				registrySchema(registries::kEmitterShape, config_.emission.shapeId));
			shapeEditor->setSchema(registrySchema(registries::kEmitterShape, config_.emission.shapeId),
					       config_.emission.shapeParams);
		}
		configChanged();
	});
	connect(shapeEditor, &ParamEditor::valueChanged, this, [this, shapeEditor](const QString &) {
		if (updating_)
			return;
		config_.emission.shapeParams = shapeEditor->values();
		configChanged();
	});

	sectionRefreshers_.push_back(refresh);
	return wrapInScrollArea(content);
}

QWidget *AtomDesignerDialog::buildPhysicsSectionPage(const std::string &group, bool withFalloffParams)
{
	QWidget *content = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(content);

	ParamEditor *editor = new ParamEditor(content);
	editor->setSourceNames(videoSourceNames());
	layout->addWidget(editor);

	ParamEditor *falloffEditor = nullptr;
	if (withFalloffParams) {
		QGroupBox *box = new QGroupBox(text("Atom.Designer.FalloffOptions"), content);
		QVBoxLayout *boxLayout = new QVBoxLayout(box);
		falloffEditor = new ParamEditor(box);
		boxLayout->addWidget(falloffEditor);
		layout->addWidget(box);
	}
	layout->addStretch(1);

	const auto refresh = [this, editor, falloffEditor, group] {
		updating_ = true;
		const ParamSchema schema = filterByGroup(schemaOf(physicsFields()), group);
		editor->setSourceNames(videoSourceNames());
		editor->setSchema(schema, bagSubset(toBag(config_.physics, physicsFields()), schema));
		if (falloffEditor) {
			falloffEditor->setSchema(registrySchema(registries::kFalloff, config_.physics.falloffId),
						 config_.physics.falloffParams);
		}
		updating_ = false;
	};

	connect(editor, &ParamEditor::valueChanged, this, [this, editor, falloffEditor](const QString &id) {
		if (updating_)
			return;
		fromBag(config_.physics, editor->values(), physicsFields());
		if (falloffEditor && id == "falloff") {
			config_.physics.falloffParams.clear();
			config_.physics.falloffParams.applyDefaults(
				registrySchema(registries::kFalloff, config_.physics.falloffId));
			falloffEditor->setSchema(registrySchema(registries::kFalloff, config_.physics.falloffId),
						 config_.physics.falloffParams);
		}
		configChanged();
	});

	if (falloffEditor) {
		connect(falloffEditor, &ParamEditor::valueChanged, this, [this, falloffEditor](const QString &) {
			if (updating_)
				return;
			config_.physics.falloffParams = falloffEditor->values();
			configChanged();
		});
	}

	sectionRefreshers_.push_back(refresh);
	return wrapInScrollArea(content);
}

QWidget *AtomDesignerDialog::buildBagPage(const ParamSchema &schema, std::function<ParamBag()> read,
					  std::function<void(const ParamBag &, const QString &)> write,
					  bool needsSourceNames)
{
	QWidget *content = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(content);

	ParamEditor *editor = new ParamEditor(content);
	layout->addWidget(editor);
	layout->addStretch(1);

	const auto refresh = [this, editor, schema, read, needsSourceNames] {
		updating_ = true;
		if (needsSourceNames)
			editor->setSourceNames(audioSourceNames());
		editor->setSchema(schema, read());
		updating_ = false;
	};

	connect(editor, &ParamEditor::valueChanged, this, [this, editor, write](const QString &id) {
		if (updating_)
			return;
		write(editor->values(), id);
		configChanged();
	});

	sectionRefreshers_.push_back(refresh);
	return wrapInScrollArea(content);
}

QWidget *AtomDesignerDialog::buildModulationPage()
{
	QWidget *page = new QWidget();
	QHBoxLayout *layout = new QHBoxLayout(page);

	routesList_ = new QListWidget(page);
	routesList_->setFixedWidth(260);

	QPushButton *add = new QPushButton(text("Atom.Designer.AddRoute"), page);
	QPushButton *remove = new QPushButton(text("Atom.Designer.RemoveRoute"), page);

	QVBoxLayout *listLayout = new QVBoxLayout();
	listLayout->addWidget(routesList_, 1);
	listLayout->addWidget(add);
	listLayout->addWidget(remove);

	ParamEditor *editor = new ParamEditor(page);
	ParamEditor *modulatorEditor = new ParamEditor(page);

	QWidget *content = new QWidget();
	QVBoxLayout *contentLayout = new QVBoxLayout(content);
	contentLayout->addWidget(editor);

	QGroupBox *box = new QGroupBox(text("Atom.Designer.ModulatorOptions"), content);
	QVBoxLayout *boxLayout = new QVBoxLayout(box);
	boxLayout->addWidget(modulatorEditor);
	contentLayout->addWidget(box);
	contentLayout->addStretch(1);

	layout->addLayout(listLayout);
	layout->addWidget(wrapInScrollArea(content), 1);

	const auto selectedIndex = [this]() -> int {
		QListWidgetItem *item = routesList_->currentItem();
		return item ? item->data(Qt::UserRole).toInt() : -1;
	};

	const auto refreshEditors = [this, editor, modulatorEditor, selectedIndex] {
		const int index = selectedIndex();
		updating_ = true;
		if (index >= 0 && index < static_cast<int>(config_.modulation.size())) {
			const ModulationRoute &route = config_.modulation[static_cast<size_t>(index)];
			editor->setSchema(schemaOf(routeFields()), toBag(route, routeFields()));
			modulatorEditor->setSchema(registrySchema(registries::kModulator, route.modulatorId),
						   route.modulatorParams);
		} else {
			editor->setSchema({}, {});
			modulatorEditor->setSchema({}, {});
		}
		updating_ = false;
	};

	connect(routesList_, &QListWidget::currentItemChanged, this, [refreshEditors] { refreshEditors(); });
	connect(routesList_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
		if (updating_ || !item)
			return;
		const int index = item->data(Qt::UserRole).toInt();
		if (index < 0 || index >= static_cast<int>(config_.modulation.size()))
			return;
		config_.modulation[static_cast<size_t>(index)].enabled = item->checkState() == Qt::Checked;
		configChanged();
	});
	connect(editor, &ParamEditor::valueChanged, this,
		[this, editor, modulatorEditor, selectedIndex](const QString &id) {
			const int index = selectedIndex();
			if (updating_ || index < 0 || index >= static_cast<int>(config_.modulation.size()))
				return;

			ModulationRoute &route = config_.modulation[static_cast<size_t>(index)];
			fromBag(route, editor->values(), routeFields());

			if (id == "modulator") {
				route.modulatorParams.clear();
				route.modulatorParams.applyDefaults(
					registrySchema(registries::kModulator, route.modulatorId));
				modulatorEditor->setSchema(registrySchema(registries::kModulator, route.modulatorId),
							   route.modulatorParams);
			}
			refreshRoutesList();
			configChanged();
		});
	connect(modulatorEditor, &ParamEditor::valueChanged, this,
		[this, modulatorEditor, selectedIndex](const QString &) {
			const int index = selectedIndex();
			if (updating_ || index < 0 || index >= static_cast<int>(config_.modulation.size()))
				return;
			config_.modulation[static_cast<size_t>(index)].modulatorParams = modulatorEditor->values();
			configChanged();
		});

	connect(add, &QPushButton::clicked, this, [this, refreshEditors] {
		ModulationRoute route;
		route.modulatorId = ModulatorRegistry::instance().defaultId();
		route.modulatorParams.applyDefaults(registrySchema(registries::kModulator, route.modulatorId));
		if (!modulationTargets().empty())
			route.target = modulationTargets().front().first;
		config_.modulation.push_back(std::move(route));

		refreshRoutesList();
		routesList_->setCurrentRow(routesList_->count() - 1);
		refreshEditors();
		configChanged();
	});
	connect(remove, &QPushButton::clicked, this, [this, selectedIndex, refreshEditors] {
		const int index = selectedIndex();
		if (index < 0 || index >= static_cast<int>(config_.modulation.size()))
			return;
		config_.modulation.erase(config_.modulation.begin() + static_cast<long>(index));
		refreshRoutesList();
		refreshEditors();
		configChanged();
	});

	modulationPageRefresh_ = [this, refreshEditors] {
		refreshRoutesList();
		refreshEditors();
	};

	return page;
}

QWidget *AtomDesignerDialog::buildForcesPage()
{
	QWidget *page = new QWidget();
	QHBoxLayout *layout = new QHBoxLayout(page);

	forcesList_ = new QListWidget(page);
	forcesList_->setFixedWidth(220);

	QComboBox *available = new QComboBox(page);
	for (const auto &item : registryEnumItems(registries::kBehavior))
		available->addItem(text(item.second), QString::fromStdString(item.first));

	QPushButton *add = new QPushButton(text("Atom.Designer.AddForce"), page);
	QPushButton *remove = new QPushButton(text("Atom.Designer.RemoveForce"), page);

	QVBoxLayout *listLayout = new QVBoxLayout();
	listLayout->addWidget(forcesList_, 1);
	listLayout->addWidget(available);
	listLayout->addWidget(add);
	listLayout->addWidget(remove);

	ParamEditor *editor = new ParamEditor(page);
	QWidget *content = new QWidget();
	QVBoxLayout *contentLayout = new QVBoxLayout(content);
	contentLayout->addWidget(editor);
	contentLayout->addStretch(1);

	layout->addLayout(listLayout);
	layout->addWidget(wrapInScrollArea(content), 1);

	const auto selectedIndex = [this]() -> int {
		QListWidgetItem *item = forcesList_->currentItem();
		return item ? item->data(Qt::UserRole).toInt() : -1;
	};

	const auto refreshEditor = [this, editor, selectedIndex] {
		const int index = selectedIndex();
		updating_ = true;
		if (index >= 0 && index < static_cast<int>(config_.physics.extras.size())) {
			const BehaviorInstance &instance = config_.physics.extras[static_cast<size_t>(index)];
			editor->setSchema(registrySchema(registries::kBehavior, instance.id), instance.params);
		} else {
			editor->setSchema({}, {});
		}
		updating_ = false;
	};

	connect(forcesList_, &QListWidget::currentItemChanged, this, [refreshEditor] { refreshEditor(); });
	connect(forcesList_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
		if (updating_ || !item)
			return;
		const int index = item->data(Qt::UserRole).toInt();
		if (index < 0 || index >= static_cast<int>(config_.physics.extras.size()))
			return;
		config_.physics.extras[static_cast<size_t>(index)].enabled = item->checkState() == Qt::Checked;
		configChanged();
	});
	connect(editor, &ParamEditor::valueChanged, this, [this, editor, selectedIndex](const QString &) {
		const int index = selectedIndex();
		if (updating_ || index < 0 || index >= static_cast<int>(config_.physics.extras.size()))
			return;
		config_.physics.extras[static_cast<size_t>(index)].params = editor->values();
		configChanged();
	});
	connect(add, &QPushButton::clicked, this, [this, available, refreshEditor] {
		BehaviorInstance instance;
		instance.id = available->currentData().toString().toStdString();
		if (instance.id.empty())
			return;
		instance.params.applyDefaults(registrySchema(registries::kBehavior, instance.id));
		config_.physics.extras.push_back(std::move(instance));
		refreshForcesList();
		forcesList_->setCurrentRow(forcesList_->count() - 1);
		refreshEditor();
		configChanged();
	});
	connect(remove, &QPushButton::clicked, this, [this, selectedIndex, refreshEditor] {
		const int index = selectedIndex();
		if (index < 0 || index >= static_cast<int>(config_.physics.extras.size()))
			return;
		config_.physics.extras.erase(config_.physics.extras.begin() + static_cast<long>(index));
		refreshForcesList();
		refreshEditor();
		configChanged();
	});

	forcesPageRefresh_ = [this, refreshEditor] {
		refreshForcesList();
		refreshEditor();
	};

	return page;
}

// -------------------------------------------------------------------------------------------
// State
// -------------------------------------------------------------------------------------------

AtomLayer *AtomDesignerDialog::currentLayer()
{
	if (AtomLayer *layer = config_.design.findLayer(currentLayerId_))
		return layer;
	if (config_.design.layers.empty())
		return nullptr;

	currentLayerId_ = config_.design.layers.front().id;
	return &config_.design.layers.front();
}

void AtomDesignerDialog::refreshLayerList()
{
	if (!layerList_)
		return;

	updating_ = true;
	layerList_->clear();
	for (const AtomLayer &layer : config_.design.layers) {
		QListWidgetItem *item = new QListWidgetItem(QString::fromStdString(layer.name));
		item->setData(Qt::UserRole, QString::fromStdString(layer.id));
		if (!layer.enabled)
			item->setForeground(QColor(130, 130, 140));
		layerList_->addItem(item);
		if (layer.id == currentLayerId_)
			layerList_->setCurrentItem(item);
	}
	updating_ = false;
}

void AtomDesignerDialog::refreshForcesList()
{
	if (!forcesList_)
		return;

	updating_ = true;
	const int previous = forcesList_->currentRow();
	forcesList_->clear();
	for (size_t i = 0; i < config_.physics.extras.size(); ++i) {
		const BehaviorInstance &instance = config_.physics.extras[i];
		const ModuleInfo *info = BehaviorRegistry::instance().info(instance.id);
		QListWidgetItem *item =
			new QListWidgetItem(info ? text(info->label) : QString::fromStdString(instance.id));
		item->setData(Qt::UserRole, static_cast<int>(i));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(instance.enabled ? Qt::Checked : Qt::Unchecked);
		forcesList_->addItem(item);
	}
	if (previous >= 0 && previous < forcesList_->count())
		forcesList_->setCurrentRow(previous);
	updating_ = false;
}

void AtomDesignerDialog::refreshRoutesList()
{
	if (!routesList_)
		return;

	updating_ = true;
	const int previous = routesList_->currentRow();
	routesList_->clear();

	for (size_t i = 0; i < config_.modulation.size(); ++i) {
		const ModulationRoute &route = config_.modulation[i];
		const ModuleInfo *info = ModulatorRegistry::instance().info(route.modulatorId);

		// "Audio -> Emission Rate" reads better in a list than an id ever will.
		QString targetLabel = QString::fromStdString(route.target);
		for (const auto &target : modulationTargets()) {
			if (target.first == route.target) {
				targetLabel = text(target.second);
				break;
			}
		}

		QListWidgetItem *item = new QListWidgetItem(
			QString("%1 %2 %3")
				.arg(info ? text(info->label) : QString::fromStdString(route.modulatorId),
				     QString(QChar(0x2192)), targetLabel));
		item->setData(Qt::UserRole, static_cast<int>(i));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(route.enabled ? Qt::Checked : Qt::Unchecked);
		routesList_->addItem(item);
	}

	if (previous >= 0 && previous < routesList_->count())
		routesList_->setCurrentRow(previous);
	updating_ = false;
}

void AtomDesignerDialog::refreshPresetGrid()
{
	if (!presetGrid_)
		return;

	QListWidgetItem *sidebarItem = sidebar_ ? sidebar_->currentItem() : nullptr;
	const QString category = sidebarItem ? sidebarItem->data(Qt::UserRole + 1).toString() : QString("all");

	presetGrid_->clear();
	for (const AtomPreset &preset : PresetStore::instance().presets()) {
		if (category != "all" && !category.isEmpty() && QString::fromStdString(preset.category) != category)
			continue;

		QListWidgetItem *item = new QListWidgetItem(QString::fromStdString(preset.name));
		item->setData(Qt::UserRole, QString::fromStdString(preset.id));
		item->setToolTip(preset.description.empty() ? QString::fromStdString(preset.name)
							    : text(preset.description));
		item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);

		// Thumbnails are simulated, not stored, so a preset always previews what it will do.
		// Simulating one is not free, so keep them around for the lifetime of the window.
		const QString key = QString::fromStdString(preset.id);
		const auto cached = thumbnails_.find(key);
		if (cached != thumbnails_.end()) {
			item->setIcon(cached.value());
		} else {
			EmitterConfig thumbConfig = preset.config;
			thumbConfig.emission.width = 300;
			thumbConfig.emission.height = 188;
			thumbConfig.emission.prewarm = true;

			const QPixmap thumbnail =
				QPixmap::fromImage(renderPresetThumbnail(thumbConfig, QSize(150, 94)));
			thumbnails_.insert(key, thumbnail);
			item->setIcon(thumbnail);
		}

		presetGrid_->addItem(item);
	}
}

void AtomDesignerDialog::refreshAllPages()
{
	for (const std::function<void()> &refresh : sectionRefreshers_)
		refresh();
	if (layerPageRefresh_)
		layerPageRefresh_();
	if (forcesPageRefresh_)
		forcesPageRefresh_();
	if (modulationPageRefresh_)
		modulationPageRefresh_();
}

void AtomDesignerDialog::refreshPreview()
{
	if (!preview_)
		return;

	EmitterConfig previewConfig = config_;
	previewConfig.emission.randomSeed = true;
	preview_->setConfig(previewConfig);
}

void AtomDesignerDialog::configChanged()
{
	config_.design.presetId.clear();
	refreshPreview();

	if (liveApply_ && liveApply_->isChecked())
		applyToSource();
}

void AtomDesignerDialog::applyToSource()
{
	SourceHold hold(weakSource_);
	if (!hold)
		return;

	applyConfigToSource(hold.source, config_);
}

void AtomDesignerDialog::applyPreset(const AtomPreset &preset)
{
	// Presets describe a look and its motion, not a canvas size: keep whatever the source is.
	const uint32_t width = config_.emission.width;
	const uint32_t height = config_.emission.height;

	config_ = preset.config;
	config_.emission.width = width;
	config_.emission.height = height;
	config_.design.presetId = preset.id;

	if (config_.design.layers.empty())
		config_.design = AtomDesign::defaultDesign();
	currentLayerId_ = config_.design.layers.front().id;

	refreshAllPages();
	refreshPreview();
	if (liveApply_ && liveApply_->isChecked())
		applyToSource();
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

	std::string error;
	if (!PresetStore::instance().save(preset, &error)) {
		QMessageBox::warning(this, text("Atom.Designer.SavePreset"), QString::fromStdString(error));
		return;
	}

	config_.design.name = preset.name;
	refreshPresetGrid();
}

void AtomDesignerDialog::deleteSelectedPreset()
{
	const AtomPreset *preset = PresetStore::instance().find(selectedPresetId_);
	if (!preset || preset->builtin)
		return;

	const QMessageBox::StandardButton answer = QMessageBox::question(
		this, text("Atom.Designer.DeletePreset"),
		text("Atom.Designer.DeletePresetConfirm").arg(QString::fromStdString(preset->name)));
	if (answer != QMessageBox::Yes)
		return;

	PresetStore::instance().remove(selectedPresetId_);
	refreshPresetGrid();
}

void AtomDesignerDialog::onPresetActivated(QListWidgetItem *item)
{
	if (!item)
		return;
	if (const AtomPreset *preset = PresetStore::instance().find(item->data(Qt::UserRole).toString().toStdString()))
		applyPreset(*preset);
}

void AtomDesignerDialog::reloadFromSource()
{
	SourceHold hold(weakSource_);
	if (!hold)
		return;

	config_ = configOfSource(hold.source);
	if (config_.design.layers.empty())
		config_.design = AtomDesign::defaultDesign();
	currentLayerId_ = config_.design.layers.front().id;

	refreshAllPages();
	refreshPreview();
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
