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

#include "atom-param-editor.hpp"
#include "atom-value-editors.hpp"
#include "atom-core/atom-registry.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>

#include <obs-module.h>

namespace atom {
namespace ui {

namespace {

QString text(const std::string &key)
{
	return QString::fromUtf8(obs_module_text(key.c_str()));
}

/// Slider plus spin box, kept in sync. Sliders use integer steps internally.
class FloatEditor : public QWidget {
public:
	FloatEditor(const ParamSpec &spec, double value, QWidget *parent) : QWidget(parent)
	{
		QHBoxLayout *layout = new QHBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);

		step_ = spec.step > 0.0 ? spec.step : 0.01;
		minimum_ = spec.min;
		maximum_ = spec.max;

		slider_ = new QSlider(Qt::Horizontal, this);
		slider_->setMinimum(0);
		slider_->setMaximum(static_cast<int>((maximum_ - minimum_) / step_ + 0.5));

		spin_ = new QDoubleSpinBox(this);
		spin_->setRange(minimum_, maximum_);
		spin_->setSingleStep(step_);
		spin_->setDecimals(step_ < 0.01 ? 3 : (step_ < 1.0 ? 2 : 0));
		spin_->setMinimumWidth(90);
		if (!spec.suffix.empty())
			spin_->setSuffix(QString(" ") + QString::fromUtf8(spec.suffix.c_str()));

		layout->addWidget(slider_, 1);
		layout->addWidget(spin_, 0);

		setValue(value);

		connect(slider_, &QSlider::valueChanged, this, [this](int position) {
			if (syncing_)
				return;
			syncing_ = true;
			spin_->setValue(minimum_ + position * step_);
			syncing_ = false;
			if (onChanged_)
				onChanged_();
		});
		connect(spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
			if (syncing_)
				return;
			syncing_ = true;
			slider_->setValue(static_cast<int>((v - minimum_) / step_ + 0.5));
			syncing_ = false;
			if (onChanged_)
				onChanged_();
		});
	}

	void setValue(double value)
	{
		syncing_ = true;
		spin_->setValue(value);
		slider_->setValue(static_cast<int>((value - minimum_) / step_ + 0.5));
		syncing_ = false;
	}

	double value() const { return spin_->value(); }

	std::function<void()> onChanged_;

private:
	QSlider *slider_ = nullptr;
	QDoubleSpinBox *spin_ = nullptr;
	double minimum_ = 0.0;
	double maximum_ = 1.0;
	double step_ = 0.01;
	bool syncing_ = false;
};

/// Two spin boxes for a Vec2.
class Vec2Editor : public QWidget {
public:
	Vec2Editor(const ParamSpec &spec, const Vec2 &value, QWidget *parent) : QWidget(parent)
	{
		QHBoxLayout *layout = new QHBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);

		const double step = spec.step > 0.0 ? spec.step : 0.01;
		for (int i = 0; i < 2; ++i) {
			spins_[i] = new QDoubleSpinBox(this);
			spins_[i]->setRange(spec.min, spec.max);
			spins_[i]->setSingleStep(step);
			spins_[i]->setDecimals(step < 0.01 ? 3 : 2);
			layout->addWidget(new QLabel(i == 0 ? "X" : "Y", this));
			layout->addWidget(spins_[i], 1);
			connect(spins_[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
				if (onChanged_)
					onChanged_();
			});
		}

		setValue(value);
	}

	void setValue(const Vec2 &value)
	{
		const QSignalBlocker blockX(spins_[0]);
		const QSignalBlocker blockY(spins_[1]);
		spins_[0]->setValue(value.x);
		spins_[1]->setValue(value.y);
	}

	Vec2 value() const
	{
		return Vec2{static_cast<float>(spins_[0]->value()), static_cast<float>(spins_[1]->value())};
	}

	std::function<void()> onChanged_;

private:
	QDoubleSpinBox *spins_[2] = {nullptr, nullptr};
};

/// Line edit plus a browse button.
class PathEditor : public QWidget {
public:
	PathEditor(const ParamSpec &spec, const QString &value, QWidget *parent)
		: QWidget(parent),
		  filter_(QString::fromUtf8(spec.filter.c_str()))
	{
		QHBoxLayout *layout = new QHBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);

		edit_ = new QLineEdit(value, this);
		QPushButton *browse = new QPushButton(text("Atom.Designer.Browse"), this);
		layout->addWidget(edit_, 1);
		layout->addWidget(browse, 0);

		connect(edit_, &QLineEdit::editingFinished, this, [this] {
			if (onChanged_)
				onChanged_();
		});
		connect(browse, &QPushButton::clicked, this, [this] {
			const QString picked =
				QFileDialog::getOpenFileName(this, text("Atom.Designer.SelectImage"), edit_->text(),
							     filter_.isEmpty() ? QString("All files (*.*)") : filter_);
			if (picked.isEmpty())
				return;
			edit_->setText(picked);
			if (onChanged_)
				onChanged_();
		});
	}

	QString value() const { return edit_->text(); }
	void setValue(const QString &value) { edit_->setText(value); }

	std::function<void()> onChanged_;

private:
	QLineEdit *edit_ = nullptr;
	QString filter_;
};

} // namespace

ParamEditor::ParamEditor(QWidget *parent) : QWidget(parent)
{
	form_ = new QFormLayout(this);
	form_->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
	form_->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
}

void ParamEditor::setSourceNames(const QStringList &names)
{
	sourceNames_ = names;
}

void ParamEditor::setSchema(const ParamSchema &schema, const ParamBag &values)
{
	schema_ = schema;
	bag_ = values;
	bag_.applyDefaults(schema_);

	while (form_->rowCount() > 0)
		form_->removeRow(0);
	rows_.clear();

	for (const ParamSpec &spec : schema_) {
		const Value *value = bag_.find(spec.id);
		QWidget *editor = createEditor(spec, value ? *value : spec.defaultValue);
		if (!editor)
			continue;

		QLabel *label = new QLabel(text(spec.label), this);
		if (!spec.description.empty())
			label->setToolTip(text(spec.description));

		form_->addRow(label, editor);

		Row row;
		row.spec = spec;
		row.label = label;
		row.editor = editor;
		rows_.push_back(std::move(row));
	}

	refreshVisibility();
}

void ParamEditor::setValues(const ParamBag &values)
{
	// Rebuilding is cheap here and keeps every editor type in sync without per-type setters;
	// this only runs when a preset is applied or another layer is selected.
	const ParamSchema schema = schema_;
	setSchema(schema, values);
}

void ParamEditor::commit(const std::string &id, Value value)
{
	if (updating_)
		return;

	bag_.set(id, std::move(value));
	refreshVisibility();
	emit valueChanged(QString::fromStdString(id));
}

void ParamEditor::refreshVisibility()
{
	for (Row &row : rows_) {
		const bool visible = evaluateVisibility(row.spec.visibleWhen, bag_);
		if (row.label)
			row.label->setVisible(visible);
		if (row.editor)
			row.editor->setVisible(visible);
	}
}

QWidget *ParamEditor::createEditor(const ParamSpec &spec, const Value &value)
{
	const std::string id = spec.id;

	switch (spec.type) {
	case ParamType::Bool: {
		QCheckBox *box = new QCheckBox(this);
		box->setChecked(detail::asBool(value));
		connect(box, &QCheckBox::toggled, this, [this, id](bool checked) { commit(id, Value(checked)); });
		return box;
	}

	case ParamType::Int: {
		QSpinBox *box = new QSpinBox(this);
		box->setRange(static_cast<int>(spec.min), static_cast<int>(spec.max));
		box->setSingleStep(static_cast<int>(std::max(1.0, spec.step)));
		box->setValue(static_cast<int>(detail::asDouble(value)));
		if (!spec.suffix.empty())
			box->setSuffix(QString(" ") + QString::fromUtf8(spec.suffix.c_str()));
		connect(box, QOverload<int>::of(&QSpinBox::valueChanged), this,
			[this, id](int v) { commit(id, Value(static_cast<int64_t>(v))); });
		return box;
	}

	case ParamType::Float: {
		FloatEditor *editor = new FloatEditor(spec, detail::asDouble(value), this);
		editor->onChanged_ = [this, id, editor] {
			commit(id, Value(editor->value()));
		};
		return editor;
	}

	case ParamType::Color: {
		ColorButton *button = new ColorButton(this);
		const Color *color = std::get_if<Color>(&value);
		button->setColor(color ? *color : Color());
		connect(button, &ColorButton::colorChanged, this,
			[this, id, button] { commit(id, Value(button->color())); });
		return button;
	}

	case ParamType::Vec2: {
		const Vec2 *vec = std::get_if<Vec2>(&value);
		Vec2Editor *editor = new Vec2Editor(spec, vec ? *vec : Vec2(), this);
		editor->onChanged_ = [this, id, editor] {
			commit(id, Value(editor->value()));
		};
		return editor;
	}

	case ParamType::Enum: {
		QComboBox *box = new QComboBox(this);
		const std::vector<std::pair<std::string, std::string>> items =
			spec.enumRegistry.empty() ? spec.enumItems : registryEnumItems(spec.enumRegistry);
		for (const auto &item : items)
			box->addItem(text(item.second), QString::fromStdString(item.first));

		const int index = box->findData(QString::fromStdString(detail::asString(value)));
		box->setCurrentIndex(index >= 0 ? index : 0);
		connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			[this, id, box](int i) { commit(id, Value(box->itemData(i).toString().toStdString())); });
		return box;
	}

	case ParamType::SourceRef: {
		QComboBox *box = new QComboBox(this);
		box->setEditable(true);
		box->addItem(text("Atom.Endpoint.Source.None"), QString());
		for (const QString &name : sourceNames_)
			box->addItem(name, name);

		const QString current = QString::fromStdString(detail::asString(value));
		const int index = box->findText(current);
		if (index >= 0)
			box->setCurrentIndex(index);
		else
			box->setCurrentText(current);

		connect(box, &QComboBox::currentTextChanged, this,
			[this, id](const QString &t) { commit(id, Value(t.toStdString())); });
		return box;
	}

	case ParamType::Text: {
		QLineEdit *edit = new QLineEdit(QString::fromStdString(detail::asString(value)), this);
		connect(edit, &QLineEdit::textEdited, this,
			[this, id](const QString &t) { commit(id, Value(t.toStdString())); });
		return edit;
	}

	case ParamType::Path: {
		PathEditor *editor = new PathEditor(spec, QString::fromStdString(detail::asString(value)), this);
		editor->onChanged_ = [this, id, editor] {
			commit(id, Value(editor->value().toStdString()));
		};
		return editor;
	}

	case ParamType::Gradient: {
		GradientEditor *editor = new GradientEditor(this);
		const Gradient *gradient = std::get_if<Gradient>(&value);
		editor->setGradient(gradient ? *gradient : Gradient::solid(Color()));
		connect(editor, &GradientEditor::gradientChanged, this,
			[this, id, editor] { commit(id, Value(editor->gradient())); });
		return editor;
	}

	case ParamType::Curve: {
		CurveEditor *editor = new CurveEditor(this);
		const Curve *curve = std::get_if<Curve>(&value);
		editor->setCurve(curve ? *curve : Curve::constant(1.0f));
		// Size curves are multipliers and usefully go above 1; alpha-style curves do not.
		editor->setValueRange(0.0f, spec.id.find("size") != std::string::npos ? 3.0f : 1.0f);
		connect(editor, &CurveEditor::curveChanged, this,
			[this, id, editor] { commit(id, Value(editor->curve())); });
		return editor;
	}
	}

	return nullptr;
}

} // namespace ui
} // namespace atom
