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

#include "RecordingPreview.h"

#include "base/Debug.h"
#include "base/RealTime.h"
#include "transform/Transform.h"
#include "transform/TransformFactory.h"
#include "transform/ModelTransformerFactory.h"
#include "data/model/WaveFileModel.h"
#include "data/model/SparseTimeValueModel.h"
#include "layer/TimeValueLayer.h"

using namespace sv;

RecordingPreview::RecordingPreview(QObject *parent) :
    QObject(parent),
    m_active(false),
    m_sampleRate(0),
    m_currentRange({ 0, 0 }),
    m_analysedTo(0),
    m_recordedTo(0)
{
}

RecordingPreview::~RecordingPreview()
{
    cancelTransform();
}

QString
RecordingPreview::begin(ModelId sourceModel, TimeValueLayer *targetLayer)
{
    abandon();

    if (!targetLayer) {
        return "Internal error: RecordingPreview::begin() called with no target layer";
    }

    auto source = ModelById::getAs<WaveFileModel>(sourceModel);
    if (!source) {
        return "Internal error: RecordingPreview::begin() called with no source model";
    }

    QString transformId = QString("%1%2").arg(PYIN_TRANSFORM_BASE).arg(PYIN_F0_OUTPUT);

    if (!TransformFactory::getInstance()->haveTransform(transformId)) {
        return tr("Transform \"%1\" not found. Unable to preview analysis while "
                  "recording.<br><br>Is the pYIN Vamp plugin correctly installed?")
            .arg(transformId);
    }

    m_sourceModel = sourceModel;
    m_sampleRate = source->getSampleRate();
    m_targetLayer = targetLayer;
    m_targetModel = targetLayer->getModel();
    m_analysedTo = 0;
    m_recordedTo = 0;
    m_added.clear();
    m_active = true;

    SVDEBUG << "RecordingPreview::begin: previewing into model "
            << m_targetModel << endl;

    return "";
}

void
RecordingPreview::recordedTo(sv_frame_t frame)
{
    if (!m_active) return;

    if (frame > m_recordedTo) {
        m_recordedTo = frame;
    }

    if (!m_transformOutput.isNone()) {
        // A chunk is already running; it will pick up the new mark when
        // it finishes
        return;
    }

    auto range = PreviewChunk::nextRange(m_analysedTo, m_recordedTo,
                                         MIN_CHUNK_FRAMES, MAX_CHUNK_FRAMES);
    if (range) {
        startChunk(*range);
    }
}

void
RecordingPreview::startChunk(const PreviewChunk::Range &range)
{
    auto source = ModelById::getAs<WaveFileModel>(m_sourceModel);
    if (!source) {
        abandon();
        return;
    }

    QString transformId = QString("%1%2").arg(PYIN_TRANSFORM_BASE).arg(PYIN_F0_OUTPUT);

    Transform transform = TransformFactory::getInstance()->
        getDefaultTransformFor(transformId, m_sampleRate);

    transform.setStepSize(PYIN_STEP_SIZE);
    transform.setBlockSize(PYIN_BLOCK_SIZE);

    transform.setStartTime(RealTime::frame2RealTime(range.from, m_sampleRate));
    transform.setDuration(RealTime::frame2RealTime(range.length(), m_sampleRate));

    QString message;

    // Not Document::createDerivedLayer: we want the output model only,
    // with no layer, no registration with the document and nothing added
    // to the undo history. The model returned here belongs to us.
    ModelId output = ModelTransformerFactory::getInstance()->
        transform(transform, ModelTransformer::Input(m_sourceModel), message);

    if (output.isNone()) {
        SVDEBUG << "RecordingPreview::startChunk: transform failed: "
                << message << endl;
        // Move past this region rather than retrying it forever
        m_analysedTo = range.to;
        return;
    }

    m_transformOutput = output;
    m_currentRange = range;

    auto model = ModelById::get(output);
    if (!model) {
        m_transformOutput = {};
        return;
    }

    connect(model.get(), SIGNAL(completionChanged(ModelId)),
            this, SLOT(transformCompletionChanged(ModelId)));

    // The transform may have finished already, in which case the signal
    // has been and gone and nothing further would arrive
    if (model->getCompletion() == 100) {
        collectChunk();
    }
}

void
RecordingPreview::transformCompletionChanged(ModelId modelId)
{
    if (modelId != m_transformOutput) return;

    auto model = ModelById::get(modelId);
    if (!model || model->getCompletion() != 100) return;

    collectChunk();
}

void
RecordingPreview::collectChunk()
{
    auto output = ModelById::getAs<SparseTimeValueModel>(m_transformOutput);
    auto target = ModelById::getAs<SparseTimeValueModel>(m_targetModel);

    if (output && target && m_targetLayer &&
        m_targetLayer->getModel() == m_targetModel) {

        // pYIN's smoothedpitchtrack output carries the host's block
        // timestamps, so these frames are already absolute. withinRange
        // discards anything that isn't, rather than trusting it.
        EventVector events = PreviewChunk::withinRange
            (output->getAllEvents(), m_currentRange);

        for (const Event &e: events) {
            target->add(e);
            m_added.push_back(e);
        }

        if (!events.empty()) {
            emit previewUpdated();
        }
    }

    m_analysedTo = m_currentRange.to;

    cancelTransform();

    // Pick up anything that arrived while this chunk was running
    auto range = PreviewChunk::nextRange(m_analysedTo, m_recordedTo,
                                         MIN_CHUNK_FRAMES, MAX_CHUNK_FRAMES);
    if (range) {
        startChunk(*range);
    }
}

void
RecordingPreview::cancelTransform()
{
    if (m_transformOutput.isNone()) return;

    ModelId output = m_transformOutput;
    m_transformOutput = {};

    auto model = ModelById::get(output);
    if (model) {
        disconnect(model.get(), SIGNAL(completionChanged(ModelId)),
                   this, SLOT(transformCompletionChanged(ModelId)));
    }

    // cancel() waits for the transform's thread to exit, so the model is
    // no longer in use by the time we release it
    ModelTransformerFactory::getInstance()->cancel(output);
    ModelById::release(output);
}

void
RecordingPreview::removeAddedEvents()
{
    if (m_added.empty()) return;

    // Only if the model we wrote into is still the one the layer is
    // using. If the full re-analysis has already replaced it, our points
    // went with it and there is nothing to undo.
    if (m_targetLayer && m_targetLayer->getModel() == m_targetModel) {
        auto target = ModelById::getAs<SparseTimeValueModel>(m_targetModel);
        if (target) {
            for (const Event &e: m_added) {
                target->remove(e);
            }
        }
    }

    m_added.clear();
}

void
RecordingPreview::end()
{
    if (!m_active) return;

    SVDEBUG << "RecordingPreview::end: removing " << m_added.size()
            << " preview point(s)" << endl;

    cancelTransform();
    removeAddedEvents();

    m_active = false;
    m_targetLayer = nullptr;
    m_targetModel = {};
    m_sourceModel = {};
}

void
RecordingPreview::abandon()
{
    cancelTransform();

    m_added.clear();
    m_active = false;
    m_targetLayer = nullptr;
    m_targetModel = {};
    m_sourceModel = {};
    m_analysedTo = 0;
    m_recordedTo = 0;
}
