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

#include <QColor>
#include <QWidget>

class QPushButton;

namespace atom {
namespace ui {

QColor toQColor(const Color &color);
Color fromQColor(const QColor &color);

/// Colour swatch button that opens the colour picker, alpha included.
class ColorButton : public QWidget {
	Q_OBJECT

public:
	explicit ColorButton(QWidget *parent = nullptr);

	void setColor(const Color &color);
	Color color() const { return color_; }

signals:
	void colorChanged();

private:
	void pick();
	void refresh();

	Color color_;
	QPushButton *button_ = nullptr;
};

/// Lifetime gradient editor: a preview bar with draggable stops, plus click-to-edit colours.
class GradientEditor : public QWidget {
	Q_OBJECT

public:
	explicit GradientEditor(QWidget *parent = nullptr);

	void setGradient(const Gradient &gradient);
	const Gradient &gradient() const { return gradient_; }

	QSize sizeHint() const override;

signals:
	void gradientChanged();

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;

private:
	int stopAt(const QPoint &point) const;
	float positionFor(int x) const;
	int xFor(float t) const;
	QRect barRect() const;

	Gradient gradient_;
	int selected_ = -1;
	bool dragging_ = false;
};

/// Editable response curve: drag points, double-click to add, right-click to remove.
class CurveEditor : public QWidget {
	Q_OBJECT

public:
	explicit CurveEditor(QWidget *parent = nullptr);

	void setCurve(const Curve &curve);
	const Curve &curve() const { return curve_; }

	/// Vertical range of the plot. Size-over-life curves go above 1, alpha curves do not.
	void setValueRange(float minimum, float maximum);

	QSize sizeHint() const override;

signals:
	void curveChanged();

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;

private:
	QPoint toPixel(const CurvePoint &point) const;
	CurvePoint toValue(const QPoint &pixel) const;
	int pointAt(const QPoint &pixel) const;
	QRect plotRect() const;

	Curve curve_;
	float minimum_ = 0.0f;
	float maximum_ = 1.0f;
	int selected_ = -1;
	bool dragging_ = false;
};

} // namespace ui
} // namespace atom
