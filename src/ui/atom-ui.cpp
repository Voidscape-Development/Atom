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

#include "atom-ui.hpp"
#include "plugin-support.h"

#ifdef ATOM_ENABLE_QT

#include "atom-designer-dialog.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QMainWindow>
#include <QPointer>

#include <vector>

namespace atom {
namespace ui {

namespace {

/// Open designer windows. They are parented to the OBS main window and delete themselves on close,
/// so this list only tracks them well enough to raise an existing one.
std::vector<QPointer<AtomDesignerDialog>> g_dialogs;

void pruneDialogs()
{
	for (auto it = g_dialogs.begin(); it != g_dialogs.end();) {
		if (it->isNull())
			it = g_dialogs.erase(it);
		else
			++it;
	}
}

AtomDesignerDialog *findDialog(obs_source_t *source)
{
	pruneDialogs();
	for (const QPointer<AtomDesignerDialog> &dialog : g_dialogs) {
		obs_source_t *existing = dialog->acquireSource();
		const bool matches = existing == source;
		if (existing)
			obs_source_release(existing);
		if (matches)
			return dialog.data();
	}
	return nullptr;
}

} // namespace

void openDesigner(obs_source_t *source)
{
	if (!source)
		return;

	if (AtomDesignerDialog *existing = findDialog(source)) {
		existing->show();
		existing->raise();
		existing->activateWindow();
		return;
	}

	QWidget *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	AtomDesignerDialog *dialog = new AtomDesignerDialog(source, main);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->show();

	g_dialogs.push_back(QPointer<AtomDesignerDialog>(dialog));
}

void shutdown()
{
	pruneDialogs();
	for (const QPointer<AtomDesignerDialog> &dialog : g_dialogs) {
		if (!dialog.isNull())
			dialog->close();
	}
	g_dialogs.clear();
}

} // namespace ui
} // namespace atom

#else // ATOM_ENABLE_QT

namespace atom {
namespace ui {

void openDesigner(obs_source_t *source)
{
	(void)source;
	obs_log(LOG_WARNING, "the Atom Designer needs a Qt-enabled build of the plugin");
}

void shutdown() {}

} // namespace ui
} // namespace atom

#endif // ATOM_ENABLE_QT
