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

#pragma once

#include "atom-core/atom-presets.hpp"

#include <obs.h>

#include <QDialog>
#include <QPointer>
#include <QPointF>
#include <QStringList>

#include <functional>
#include <string>
#include <vector>

class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace atom {
namespace ui {

class AtomPreview;
class ColorButton;
class CurveEditor;
class GradientEditor;
class ParamEditor;
class PresetPanel;

/// The Atom Designer.
///
/// Three columns answer three questions. The outline on the left shows what the effect is made of
/// and is the only place anything is selected. The preview in the middle shows the result, with
/// the selection drawn into it, and under it the colour, size and opacity of the selected atom
/// over its life. The inspector on the right holds the settings of the selection and always says
/// what they apply to: one kind of atom, or the whole emitter.
///
/// Every settings section is generated from a ParamSchema, so a newly registered module shows up
/// here without any UI work.
class AtomDesignerDialog : public QDialog {
	Q_OBJECT

public:
	AtomDesignerDialog(obs_source_t *source, QWidget *parent);
	~AtomDesignerDialog() override;

	/// Returns a strong reference to the edited source, or nullptr once it is gone.
	/// The caller owns the reference and must release it.
	obs_source_t *acquireSource() const;

	/// Reloads the working copy from the source, discarding unapplied edits.
	void reloadFromSource();

protected:
	void resizeEvent(QResizeEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	/// What an outline row stands for.
	enum class Kind {
		None,
		Emitter,
		Atom,
		Launch,
		Destination,
		Force,
		Audio,
		Scene,
		Link,
		Output,
		/// Rows that do something when clicked rather than select something.
		AddAtom,
		AddForce,
		AddLink,
	};

	struct Selection {
		Kind kind = Kind::Atom;
		/// For Kind::Atom: which layer, and which part of it ("" for all of it).
		std::string layerId;
		std::string part;
		/// For Kind::Force and Kind::Link: position in the config's list.
		int index = -1;
	};

	using SectionWriter = std::function<void(const ParamBag &values, const std::string &id)>;

	QWidget *buildTopBar();
	QWidget *buildOutlinePane();
	QWidget *buildCenterPane();
	QWidget *buildInspectorPane();
	QWidget *buildLifeTracks();

	// Outline.
	void rebuildOutline();
	void onOutlineSelectionChanged();
	void onOutlineItemClicked(QTreeWidgetItem *item);
	void onOutlineItemChanged(QTreeWidgetItem *item);
	Selection selectionOf(QTreeWidgetItem *item) const;
	void select(const Selection &selection);

	// Inspector.
	void rebuildInspector();
	void buildAtomSections(AtomLayer &layer);
	void buildEmitterSections();
	void buildLaunchSections();
	void buildForceSections();
	void buildLinkSections(int index, bool withTarget);
	ParamEditor *addSection(const QString &title, const ParamSchema &schema, const ParamBag &values,
				SectionWriter write, const char *linkTable = nullptr,
				const QStringList &sourceNames = QStringList());
	void addHeaderAction(const QString &label, std::function<void()> action);
	void scheduleInspectorRebuild();
	void scheduleOutlineRebuild();

	// Life tracks.
	void refreshLifeTracks();

	// Structure edits.
	void addAtom();
	void duplicateAtom();
	void removeAtom();
	void addForce(QTreeWidgetItem *anchor);
	void removeForce();
	void addLink();
	void removeLink();
	/// Opens the editor for whatever drives `target`, creating a link first if nothing does.
	void requestLink(const std::string &target);
	void editLink(int index);

	// State.
	AtomLayer *currentLayer();
	int currentLayerIndex();
	void refreshEverything();
	void refreshPreview();
	void refreshPreviewDecor();
	void refreshHeader();
	void refreshTitles();
	void configChanged();
	void applyToSource();
	void checkpoint();
	void undo();
	void redo();
	void restore(const EmitterConfig &config);
	void onPreviewDragged(const QPointF &position);

	// Presets.
	void togglePresets();
	void placePresetPanel();
	void usePreset(const EmitterConfig &config, const std::string &presetId);
	void saveCurrentAsPreset();

	QStringList videoSourceNames() const;
	QStringList audioSourceNames() const;

	obs_weak_source_t *weakSource_ = nullptr;

	/// Working copy; written back to the source as it is edited, or on Done.
	EmitterConfig config_;
	/// The effect as it was when the designer was opened: the target of "revert".
	EmitterConfig opened_;
	Selection selection_;

	/// Undo history. `committed_` is the state the next undo step will return to; edits made in
	/// quick succession (a slider drag) fold into one step.
	std::vector<EmitterConfig> undo_;
	std::vector<EmitterConfig> redo_;
	EmitterConfig committed_;
	QTimer *checkpointTimer_ = nullptr;

	/// Set while widgets are being repopulated, so programmatic changes are not treated as edits.
	bool updating_ = false;
	bool inspectorRebuildPending_ = false;
	bool outlineRebuildPending_ = false;

	QLabel *title_ = nullptr;
	QLabel *subtitle_ = nullptr;
	QPushButton *undoButton_ = nullptr;
	QPushButton *redoButton_ = nullptr;
	QPushButton *liveButton_ = nullptr;
	QPushButton *presetsButton_ = nullptr;

	QTreeWidget *outline_ = nullptr;
	PresetPanel *presetPanel_ = nullptr;

	QPointer<AtomPreview> preview_;
	QLabel *previewHint_ = nullptr;

	QStackedWidget *lifeStack_ = nullptr;
	QLabel *lifeTitle_ = nullptr;
	QLabel *lifeEnd_ = nullptr;
	QLabel *lifeHint_ = nullptr;
	QStackedWidget *lifeColorStack_ = nullptr;
	GradientEditor *lifeGradient_ = nullptr;
	ColorButton *lifeColor_ = nullptr;
	CurveEditor *lifeSize_ = nullptr;
	CurveEditor *lifeFade_ = nullptr;

	QLabel *headerTitle_ = nullptr;
	QLabel *headerScope_ = nullptr;
	QLabel *headerChip_ = nullptr;
	QWidget *headerActions_ = nullptr;
	QScrollArea *inspectorScroll_ = nullptr;
	QVBoxLayout *inspectorLayout_ = nullptr;
	/// Where addSection() puts the next section: the inspector, or a pop-up being filled in.
	QVBoxLayout *sectionTarget_ = nullptr;
	/// Set while the link pop-up is open, so it follows edits made inside it.
	std::function<void()> popupRebuild_;
	std::function<void()> popupChanged_;

	/// Folds a burst of live edits into one write to the source.
	QTimer *applyTimer_ = nullptr;
};

} // namespace ui
} // namespace atom
