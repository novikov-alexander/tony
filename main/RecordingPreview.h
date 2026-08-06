/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Tony
    An intonation analysis and annotation tool
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006-2012 Chris Cannam and QMUL.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#ifndef TONY_RECORDING_PREVIEW_H
#define TONY_RECORDING_PREVIEW_H

#include "PreviewChunk.h"

#include "base/BaseTypes.h"
#include "data/model/Model.h"

#include <QObject>
#include <QPointer>

namespace sv {
class TimeValueLayer;
}

/**
 * Fills in the pitch track as a recording is made, so that the
 * performer can see something while singing or playing.
 *
 * This is only ever a preview. When the recording stops, the pitch and
 * note layers are regenerated in full from the completed audio (by
 * Document::replaceModel, via MainWindowBase's refreshModel call), so
 * nothing produced here survives into the saved session. Accordingly
 * this class removes everything it added when the recording finishes,
 * leaving the pitch track exactly as it would have been without it.
 *
 * Each chunk is analysed by running a pYIN transform over one region of
 * the recording so far. The regions are adjacent and never overlap, so
 * the results are simply concatenated. Only one transform runs at a
 * time; duration updates that arrive while one is running raise the
 * mark for the next chunk rather than starting another.
 *
 * The transform output model is obtained from ModelTransformerFactory
 * rather than from Document, so it belongs to us: no layer is created
 * for it, it is not registered with the document, and nothing is added
 * to the undo history.
 */
class RecordingPreview : public QObject
{
    Q_OBJECT

public:
    explicit RecordingPreview(QObject *parent = nullptr);
    virtual ~RecordingPreview();

    /**
     * Begin previewing into the given pitch layer, analysing the given
     * source (recording) model. Returns "" on success or an error
     * string on failure.
     */
    QString begin(sv::ModelId sourceModel, sv::TimeValueLayer *targetLayer);

    /**
     * Note that the recording has reached the given frame. Starts a
     * chunk if one is not already running and there is enough new audio.
     */
    void recordedTo(sv::sv_frame_t frame);

    /**
     * The recording has finished: cancel any running analysis and
     * remove every point this preview added.
     */
    void end();

    /**
     * Abandon the preview without touching the target, for use when the
     * target is being replaced anyway.
     */
    void abandon();

    bool isActive() const { return m_active; }

signals:
    void previewUpdated();

protected slots:
    void transformCompletionChanged(sv::ModelId);

protected:
    void startChunk(const PreviewChunk::Range &range);
    void collectChunk();
    void cancelTransform();
    void removeAddedEvents();

    // ~0.25s at 44.1kHz: long enough that starting a transform is worth
    // it, short enough to feel responsive
    static const sv::sv_frame_t MIN_CHUNK_FRAMES = 11025;

    // Bound the work in any single chunk, so that a stall produces
    // several ordinary chunks rather than one very long one
    static const sv::sv_frame_t MAX_CHUNK_FRAMES = 220500;

    static const int PYIN_STEP_SIZE = 256;
    static const int PYIN_BLOCK_SIZE = 2048;

    static constexpr const char *PYIN_TRANSFORM_BASE = "vamp:pyin:pyin:";
    static constexpr const char *PYIN_F0_OUTPUT = "smoothedpitchtrack";

    bool m_active;

    sv::ModelId m_sourceModel;
    sv::sv_samplerate_t m_sampleRate;

    QPointer<sv::TimeValueLayer> m_targetLayer;
    sv::ModelId m_targetModel;

    sv::ModelId m_transformOutput;
    PreviewChunk::Range m_currentRange;

    sv::sv_frame_t m_analysedTo;
    sv::sv_frame_t m_recordedTo;

    // Exactly what we added, so that we can take exactly that away again
    sv::EventVector m_added;
};

#endif
