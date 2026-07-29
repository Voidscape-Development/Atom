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
#include <QHash>
#include <QPixmap>
#include <QPointer>
#include <QStringList>

#include <functional>
#include <map>
#include <string>
#include <vector>

class QCheckBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;

namespace atom {
namespace ui {

class AtomPreview;
class ParamEditor;

/// The Atom Designer.
///
/// Laid out like OBS' Add Source dialog: a category list on the left, a browsable preset grid or a
/// property page on the right, and a live preview underneath. Every page is generated from a
/// ParamSchema, so a newly registered module shows up here without any UI work.
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

private slots:
	void onSidebarChanged();
	void onPresetActivated(QListWidgetItem *item);

private:
	QWidget *buildPresetPage();
	QWidget *buildLayerPage();
	QWidget *buildLayerSectionPage(const std::string &group, bool withModuleParams, const std::string &registryName,
				       const std::string &moduleSelectorId);
	QWidget *buildEmissionPage();
	/// Generic page over any field table, reading and writing through the given accessors.
	QWidget *buildBagPage(const ParamSchema &schema, std::function<ParamBag()> read,
			      std::function<void(const ParamBag &, const QString &)> write, bool needsSourceNames);
	QWidget *buildModulationPage();
	QWidget *buildPhysicsSectionPage(const std::string &group, bool withFalloffParams);
	QWidget *buildForcesPage();
	void buildSidebar();
	void addPage(const std::string &id, QWidget *widget);

	AtomLayer *currentLayer();
	void refreshAllPages();
	void refreshLayerList();
	void refreshForcesList();
	void refreshRoutesList();
	void refreshPresetGrid();
	void refreshPreview();
	void configChanged();
	void applyToSource();
	void applyPreset(const AtomPreset &preset);
	void saveCurrentAsPreset();
	void deleteSelectedPreset();
	QStringList videoSourceNames() const;
	QStringList audioSourceNames() const;

	obs_weak_source_t *weakSource_ = nullptr;
	/// Working copy; written back to the source on apply.
	EmitterConfig config_;
	std::string currentLayerId_;
	std::string selectedPresetId_;
	/// Set while editors are being repopulated, so programmatic changes are not treated as edits.
	bool updating_ = false;

	QListWidget *sidebar_ = nullptr;
	QStackedWidget *stack_ = nullptr;
	QLabel *title_ = nullptr;
	QLabel *subtitle_ = nullptr;
	QListWidget *presetGrid_ = nullptr;
	QListWidget *layerList_ = nullptr;
	QListWidget *forcesList_ = nullptr;
	QListWidget *routesList_ = nullptr;
	QPointer<AtomPreview> preview_;
	QCheckBox *liveApply_ = nullptr;
	QPushButton *deletePreset_ = nullptr;

	/// Repopulates one generated page from the working config.
	std::vector<std::function<void()>> sectionRefreshers_;
	std::function<void()> layerPageRefresh_;
	std::function<void()> forcesPageRefresh_;
	std::function<void()> modulationPageRefresh_;

	std::map<std::string, int> pageIndex_;
	/// Simulated preset thumbnails, kept for the lifetime of the window.
	QHash<QString, QPixmap> thumbnails_;
};

} // namespace ui
} // namespace atom
