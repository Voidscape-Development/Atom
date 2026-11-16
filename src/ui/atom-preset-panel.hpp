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

#include <QFrame>
#include <QHash>
#include <QPixmap>
#include <QStringList>

#include <functional>
#include <string>

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTimer;

namespace atom {
namespace ui {

/// The pop-up behind the designer's "Presets and revert" button.
///
/// Lists the effect as it is now, the effect as it was when the designer was opened, and every
/// preset. Clicking one only shows it in the preview; nothing reaches the emitter until "Use this".
/// It is a child of the designer rather than a window of its own, so it can sit over the outline
/// and leave the preview in view.
class PresetPanel : public QFrame {
	Q_OBJECT

public:
	struct Callbacks {
		/// Show this configuration in the preview. Null means "back to the effect being edited".
		std::function<void(const EmitterConfig *)> preview;
		/// Replace the effect. `presetId` is empty when reverting to the opened state.
		std::function<void(const EmitterConfig &, const std::string &presetId)> use;
		/// The panel closed without choosing anything.
		std::function<void()> cancelled;
	};

	explicit PresetPanel(QWidget *parent);

	/// Shows the panel. `current` and `opened` are the two "this emitter" entries.
	void open(const EmitterConfig &current, const EmitterConfig &opened, Callbacks callbacks);
	/// Hides the panel as if Cancel had been pressed.
	void cancel();

private:
	void rebuildList();
	void renderNextThumbnail();
	void onSelectionChanged();
	void useSelection();
	void deleteSelection();
	/// The configuration behind a list entry, or null for the "now" entry.
	const EmitterConfig *configFor(const QString &id) const;

	QComboBox *category_ = nullptr;
	QListWidget *list_ = nullptr;
	QLabel *status_ = nullptr;
	QPushButton *use_ = nullptr;
	QPushButton *delete_ = nullptr;
	QTimer *thumbnailTimer_ = nullptr;

	EmitterConfig current_;
	EmitterConfig opened_;
	Callbacks callbacks_;
	/// Entries still waiting for their thumbnail to be simulated.
	QStringList pending_;
	/// Simulated preset thumbnails, kept while the designer is open.
	QHash<QString, QPixmap> thumbnails_;
};

} // namespace ui
} // namespace atom
