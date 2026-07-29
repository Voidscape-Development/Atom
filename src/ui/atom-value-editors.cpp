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

#include "atom-value-editors.hpp"

#include <QAction>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>

#include <obs-module.h>

namespace atom {
namespace ui {

namespace {

constexpr int kHandleSize = 11;
constexpr int kHandleMargin = 14;
constexpr int kPlotMargin = 8;

QColor checkerLight()
{
	return QColor(90, 90, 96);
}

QColor checkerDark()
{
	return QColor(64, 64, 70);
}

void paintChecker(QPainter &painter, const QRect &rect)
{
	const int step = 8;
	painter.fillRect(rect, checkerDark());
	for (int y = rect.top(); y < rect.bottom(); y += step) {
		for (int x = rect.left(); x < rect.right(); x += step) {
			if (((x / step) + (y / step)) % 2 == 0)
				painter.fillRect(QRect(x, y, step, step).intersected(rect), checkerLight());
		}
	}
}

} // namespace

QColor toQColor(const Color &color)
{
	return QColor::fromRgbF(saturate(color.r), saturate(color.g), saturate(color.b), saturate(color.a));
}

Color fromQColor(const QColor &color)
{
	return Color(static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
		     static_cast<float>(color.blueF()), static_cast<float>(color.alphaF()));
}

// -------------------------------------------------------------------------------------------
// ColorButton
// -------------------------------------------------------------------------------------------

ColorButton::ColorButton(QWidget *parent) : QWidget(parent)
{
	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	button_ = new QPushButton(this);
	button_->setAutoFillBackground(true);
	button_->setFlat(true);
	button_->setMinimumHeight(24);
	layout->addWidget(button_);

	connect(button_, &QPushButton::clicked, this, &ColorButton::pick);
	refresh();
}

void ColorButton::setColor(const Color &color)
{
	color_ = color;
	refresh();
}

void ColorButton::pick()
{
	const QColor picked = QColorDialog::getColor(toQColor(color_), this, obs_module_text("Atom.Designer.PickColor"),
						     QColorDialog::ShowAlphaChannel);
	if (!picked.isValid())
		return;

	color_ = fromQColor(picked);
	refresh();
	emit colorChanged();
}

void ColorButton::refresh()
{
	const QColor color = toQColor(color_);
	const bool dark = color.lightnessF() * color.alphaF() < 0.5;

	button_->setStyleSheet(QString("background-color: rgba(%1,%2,%3,%4); color: %5; border: 1px solid #202020;")
				       .arg(color.red())
				       .arg(color.green())
				       .arg(color.blue())
				       .arg(color.alpha())
				       .arg(dark ? "#f0f0f0" : "#101010"));
	button_->setText(QString("%1  (%2%)").arg(color.name(QColor::HexRgb).toUpper()).arg(int(color.alphaF() * 100)));
}

// -------------------------------------------------------------------------------------------
// GradientEditor
// -------------------------------------------------------------------------------------------

GradientEditor::GradientEditor(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(56);
	setMouseTracking(true);
	setToolTip(obs_module_text("Atom.Designer.GradientHint"));
	gradient_ = Gradient::solid(Color(1.0f, 1.0f, 1.0f, 1.0f));
}

QSize GradientEditor::sizeHint() const
{
	return QSize(280, 56);
}

void GradientEditor::setGradient(const Gradient &gradient)
{
	gradient_ = gradient;
	if (gradient_.stops.empty())
		gradient_ = Gradient::solid(Color(1.0f, 1.0f, 1.0f, 1.0f));
	gradient_.sort();
	selected_ = -1;
	update();
}

QRect GradientEditor::barRect() const
{
	return QRect(kHandleMargin / 2, 4, std::max(1, width() - kHandleMargin), std::max(1, height() - 22));
}

int GradientEditor::xFor(float t) const
{
	const QRect bar = barRect();
	return bar.left() + static_cast<int>(saturate(t) * bar.width());
}

float GradientEditor::positionFor(int x) const
{
	const QRect bar = barRect();
	return saturate(static_cast<float>(x - bar.left()) / static_cast<float>(std::max(1, bar.width())));
}

int GradientEditor::stopAt(const QPoint &point) const
{
	for (size_t i = 0; i < gradient_.stops.size(); ++i) {
		const int x = xFor(gradient_.stops[i].t);
		if (std::abs(point.x() - x) <= kHandleSize)
			return static_cast<int>(i);
	}
	return -1;
}

void GradientEditor::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	const QRect bar = barRect();
	paintChecker(painter, bar);

	QLinearGradient brush(bar.topLeft(), bar.topRight());
	for (const GradientStop &stop : gradient_.stops)
		brush.setColorAt(saturate(stop.t), toQColor(stop.color));
	painter.fillRect(bar, brush);
	painter.setPen(QColor(30, 30, 34));
	painter.drawRect(bar);

	for (size_t i = 0; i < gradient_.stops.size(); ++i) {
		const int x = xFor(gradient_.stops[i].t);
		const int y = bar.bottom() + 8;

		QPainterPath handle;
		handle.moveTo(x, y - 7);
		handle.lineTo(x - 5, y + 3);
		handle.lineTo(x + 5, y + 3);
		handle.closeSubpath();

		painter.setBrush(toQColor(gradient_.stops[i].color));
		painter.setPen(static_cast<int>(i) == selected_ ? QPen(QColor(255, 255, 255), 2)
								: QPen(QColor(20, 20, 24), 1));
		painter.drawPath(handle);
	}
}

void GradientEditor::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;

	selected_ = stopAt(event->position().toPoint());
	dragging_ = selected_ >= 0;
	update();
}

void GradientEditor::mouseMoveEvent(QMouseEvent *event)
{
	if (!dragging_ || selected_ < 0)
		return;

	gradient_.stops[static_cast<size_t>(selected_)].t = positionFor(event->position().toPoint().x());

	// Keep the selection pointing at the same stop after reordering.
	const GradientStop moved = gradient_.stops[static_cast<size_t>(selected_)];
	gradient_.sort();
	for (size_t i = 0; i < gradient_.stops.size(); ++i) {
		if (gradient_.stops[i].t == moved.t && gradient_.stops[i].color.toRGBA() == moved.color.toRGBA()) {
			selected_ = static_cast<int>(i);
			break;
		}
	}

	update();
	emit gradientChanged();
}

void GradientEditor::mouseReleaseEvent(QMouseEvent *)
{
	dragging_ = false;
}

void GradientEditor::mouseDoubleClickEvent(QMouseEvent *event)
{
	const int index = stopAt(event->position().toPoint());
	if (index >= 0) {
		const QColor picked = QColorDialog::getColor(
			toQColor(gradient_.stops[static_cast<size_t>(index)].color), this,
			obs_module_text("Atom.Designer.PickColor"), QColorDialog::ShowAlphaChannel);
		if (picked.isValid()) {
			gradient_.stops[static_cast<size_t>(index)].color = fromQColor(picked);
			update();
			emit gradientChanged();
		}
		return;
	}

	GradientStop stop;
	stop.t = positionFor(event->position().toPoint().x());
	stop.color = gradient_.sample(stop.t);
	gradient_.stops.push_back(stop);
	gradient_.sort();
	update();
	emit gradientChanged();
}

void GradientEditor::contextMenuEvent(QContextMenuEvent *event)
{
	const int index = stopAt(event->pos());
	if (index < 0 || gradient_.stops.size() <= 2)
		return;

	QMenu menu(this);
	QAction *remove = menu.addAction(obs_module_text("Atom.Designer.RemoveStop"));
	if (menu.exec(event->globalPos()) != remove)
		return;

	gradient_.stops.erase(gradient_.stops.begin() + index);
	selected_ = -1;
	update();
	emit gradientChanged();
}

// -------------------------------------------------------------------------------------------
// CurveEditor
// -------------------------------------------------------------------------------------------

CurveEditor::CurveEditor(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(110);
	setMouseTracking(true);
	setToolTip(obs_module_text("Atom.Designer.CurveHint"));
	curve_ = Curve::linear(1.0f, 0.0f);
}

QSize CurveEditor::sizeHint() const
{
	return QSize(280, 130);
}

void CurveEditor::setCurve(const Curve &curve)
{
	curve_ = curve;
	if (curve_.points.empty())
		curve_ = Curve::constant(1.0f);
	curve_.sort();
	selected_ = -1;
	update();
}

void CurveEditor::setValueRange(float minimum, float maximum)
{
	minimum_ = minimum;
	maximum_ = std::max(maximum, minimum + 0.01f);
	update();
}

QRect CurveEditor::plotRect() const
{
	return rect().adjusted(kPlotMargin, kPlotMargin, -kPlotMargin, -kPlotMargin);
}

QPoint CurveEditor::toPixel(const CurvePoint &point) const
{
	const QRect plot = plotRect();
	const float normalized = (point.value - minimum_) / (maximum_ - minimum_);
	return QPoint(plot.left() + static_cast<int>(saturate(point.t) * plot.width()),
		      plot.bottom() - static_cast<int>(saturate(normalized) * plot.height()));
}

CurvePoint CurveEditor::toValue(const QPoint &pixel) const
{
	const QRect plot = plotRect();
	CurvePoint point;
	point.t = saturate(static_cast<float>(pixel.x() - plot.left()) / static_cast<float>(std::max(1, plot.width())));
	const float normalized = saturate(static_cast<float>(plot.bottom() - pixel.y()) /
					  static_cast<float>(std::max(1, plot.height())));
	point.value = lerp(minimum_, maximum_, normalized);
	return point;
}

int CurveEditor::pointAt(const QPoint &pixel) const
{
	for (size_t i = 0; i < curve_.points.size(); ++i) {
		const QPoint p = toPixel(curve_.points[i]);
		if ((p - pixel).manhattanLength() <= 10)
			return static_cast<int>(i);
	}
	return -1;
}

void CurveEditor::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	const QRect plot = plotRect();
	painter.fillRect(plot, QColor(38, 38, 44));

	painter.setPen(QPen(QColor(60, 60, 68), 1));
	for (int i = 1; i < 4; ++i) {
		const int x = plot.left() + plot.width() * i / 4;
		const int y = plot.top() + plot.height() * i / 4;
		painter.drawLine(x, plot.top(), x, plot.bottom());
		painter.drawLine(plot.left(), y, plot.right(), y);
	}
	painter.setPen(QPen(QColor(80, 80, 90), 1));
	painter.drawRect(plot);

	// Sampled curve, so the drawn line matches exactly what the simulation evaluates.
	QPainterPath path;
	for (int x = 0; x <= plot.width(); ++x) {
		const float t = static_cast<float>(x) / static_cast<float>(std::max(1, plot.width()));
		const QPoint pixel = toPixel(CurvePoint{t, curve_.sample(t)});
		if (x == 0)
			path.moveTo(pixel);
		else
			path.lineTo(pixel);
	}
	painter.setPen(QPen(QColor(120, 190, 255), 2));
	painter.drawPath(path);

	for (size_t i = 0; i < curve_.points.size(); ++i) {
		const QPoint pixel = toPixel(curve_.points[i]);
		painter.setBrush(static_cast<int>(i) == selected_ ? QColor(255, 255, 255) : QColor(120, 190, 255));
		painter.setPen(QPen(QColor(20, 20, 24), 1));
		painter.drawEllipse(pixel, 4, 4);
	}
}

void CurveEditor::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;

	selected_ = pointAt(event->position().toPoint());
	dragging_ = selected_ >= 0;
	update();
}

void CurveEditor::mouseMoveEvent(QMouseEvent *event)
{
	if (!dragging_ || selected_ < 0)
		return;

	const CurvePoint updated = toValue(event->position().toPoint());
	CurvePoint &point = curve_.points[static_cast<size_t>(selected_)];

	// The first and last points stay pinned to the ends so the curve always spans a full life.
	const bool isFirst = selected_ == 0;
	const bool isLast = static_cast<size_t>(selected_) == curve_.points.size() - 1;
	point.value = updated.value;
	if (!isFirst && !isLast)
		point.t = updated.t;

	curve_.sort();
	update();
	emit curveChanged();
}

void CurveEditor::mouseReleaseEvent(QMouseEvent *)
{
	dragging_ = false;
}

void CurveEditor::mouseDoubleClickEvent(QMouseEvent *event)
{
	if (pointAt(event->position().toPoint()) >= 0)
		return;

	curve_.points.push_back(toValue(event->position().toPoint()));
	curve_.sort();
	update();
	emit curveChanged();
}

void CurveEditor::contextMenuEvent(QContextMenuEvent *event)
{
	const int index = pointAt(event->pos());
	if (index < 0 || curve_.points.size() <= 2)
		return;

	QMenu menu(this);
	QAction *remove = menu.addAction(obs_module_text("Atom.Designer.RemovePoint"));
	if (menu.exec(event->globalPos()) != remove)
		return;

	curve_.points.erase(curve_.points.begin() + index);
	selected_ = -1;
	update();
	emit curveChanged();
}

} // namespace ui
} // namespace atom
