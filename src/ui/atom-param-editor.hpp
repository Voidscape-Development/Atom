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

#include "atom-core/atom-value.hpp"

#include <QStringList>
#include <QWidget>

#include <functional>
#include <vector>

class QFormLayout;

namespace atom {
namespace ui {

/// Builds an editor for any ParamSchema.
///
/// The designer never hand-writes widgets: every page is this editor pointed at a schema, so a
/// module that registers new parameters is immediately editable.
class ParamEditor : public QWidget {
	Q_OBJECT

public:
	explicit ParamEditor(QWidget *parent = nullptr);

	void setSchema(const ParamSchema &schema, const ParamBag &values);
	void setValues(const ParamBag &values);
	const ParamBag &values() const { return bag_; }

	/// Names offered by SourceRef parameters.
	void setSourceNames(const QStringList &names);

signals:
	void valueChanged(const QString &id);

private:
	struct Row {
		ParamSpec spec;
		QWidget *label = nullptr;
		QWidget *editor = nullptr;
	};

	QWidget *createEditor(const ParamSpec &spec, const Value &value);
	void commit(const std::string &id, Value value);
	void refreshVisibility();

	QFormLayout *form_ = nullptr;
	ParamSchema schema_;
	ParamBag bag_;
	std::vector<Row> rows_;
	QStringList sourceNames_;
	bool updating_ = false;
};

} // namespace ui
} // namespace atom
