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
#include <QTimer>
#include <QWidget>

#include <memory>

namespace atom {
namespace ui {

/// Software renderer for an AtomSystem.
///
/// It shares the simulation and DesignEvaluator with the OBS renderer, so what the designer shows
/// is what the source draws. Used for the live preview and for preset thumbnails.
QImage renderAtomFrame(const AtomSystem &system, const QSize &size, const QColor &background);

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

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	void tick();

	std::unique_ptr<AtomSystem> system_;
	/// Modulation runs here too, so LFO and noise routes animate in the preview. Audio reads as
	/// silence: the designer does not tap the audio graph.
	ModulationEngine modulation_;
	EmitterConfig base_;
	EmitterConfig modulated_;
	float time_ = 0.0f;
	QTimer timer_;
	QColor background_{18, 18, 22};
	bool playing_ = true;
	qint64 lastTick_ = 0;
};

} // namespace ui
} // namespace atom
