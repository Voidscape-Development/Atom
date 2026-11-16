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

#include "atom-core/atom-modulation.hpp"
#include "atom-core/atom-system.hpp"

#include <QImage>
#include <QPointF>
#include <QTimer>
#include <QWidget>

#include <memory>
#include <vector>

class QPainter;

namespace atom {
namespace ui {

/// Software renderer for an AtomSystem.
///
/// It shares the simulation and DesignEvaluator with the OBS renderer, so what the designer shows
/// is what the source draws. Used for the live preview and for preset thumbnails.
///
/// `highlightLayer` dims every other kind of atom, which is how the designer shows which one is
/// being edited; -1 draws them all normally.
QImage renderAtomFrame(const AtomSystem &system, const QSize &size, const QColor &background, int highlightLayer = -1);

/// Runs a configuration for `warmup` seconds and returns a single frame.
QImage renderPresetThumbnail(const EmitterConfig &config, const QSize &size, float warmup = 1.6f);

/// Live, animated preview of one emitter configuration.
class AtomPreview : public QWidget {
	Q_OBJECT

public:
	explicit AtomPreview(QWidget *parent = nullptr);
	~AtomPreview() override;

	/// Reconfigures the preview. Keeps live atoms so edits do not restart the effect.
	void setConfig(const EmitterConfig &config);

	void restart();
	void setPlaying(bool playing);
	bool playing() const { return playing_; }

	void setBackground(const QColor &color);

	/// What the preview draws on top of the atoms to show what a selection controls.
	enum class Overlay {
		None,
		/// Where atoms are born: the emitter shape, as a scatter of sample points.
		Shape,
		/// Launch direction and spread. Dragging in the preview aims it.
		Launch,
		/// The destination point. Dragging in the preview moves it.
		Target,
	};
	void setOverlay(Overlay overlay);
	/// Dims every kind of atom except this one; -1 shows them all.
	void setHighlightLayer(int layer);
	/// Checkerboard instead of a flat backdrop, to judge transparency.
	void setChecker(bool checker);
	/// Emits `count` atoms at once, like the source's burst trigger.
	void burst(int count);
	/// Middle of the emitter shape in emitter pixels: where the launch arrow starts.
	QPointF shapeCenter() const { return QPointF(shapeCenter_.x, shapeCenter_.y); }

signals:
	/// The pointer was pressed or dragged while an interactive overlay is showing. The position
	/// is in emitter pixels.
	void dragged(const QPointF &position);

public:
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;

private:
	void tick();

	std::unique_ptr<AtomSystem> system_;
	/// Modulation runs here too, so LFO and noise routes animate in the preview. Audio reads as
	/// silence: the designer does not tap the audio graph.
	ModulationEngine modulation_;
	EmitterConfig base_;
	float time_ = 0.0f;
	QTimer timer_;
	QColor background_{18, 18, 22};
	bool playing_ = true;

	void paintOverlay(QPainter &painter);
	void resampleShape();
	/// Emitter pixels to widget pixels: `widget = emitter * scale + offset`.
	void mapping(float &scale, QPointF &offset) const;

	Overlay overlay_ = Overlay::None;
	int highlightLayer_ = -1;
	bool checker_ = false;
	/// Points picked by the configured emitter shape, cached per configuration.
	std::vector<Vec2> shapeSamples_;
	Vec2 shapeCenter_;
	qint64 lastTick_ = 0;
};

} // namespace ui
} // namespace atom
