/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Tony
    Realtime analysis helper (extracted from Analyser)
*/

#include "RealtimeAnalyser.h"

#include "OverlapProcessor.h"

#include <algorithm>
#include <utility>
#include <memory>
#include <iostream>

#include <QSettings>
#include <QMutexLocker>

#include <vamp-hostsdk/RealTime.h>

#include "transform/TransformFactory.h"
#include "framework/Document.h"
#include "data/model/WaveFileModel.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/model/NoteModel.h"

#include "view/Pane.h"
#include "layer/Layer.h"
#include "layer/TimeValueLayer.h"
#include "layer/FlexiNoteLayer.h"
#include "layer/ColourDatabase.h"

using std::cerr;
using std::endl;

using namespace sv;

// Global overlap processor instance for efficient processing (same as previously in Analyser.cpp)
static OverlapProcessor s_overlapProcessor;

static void logPatchFrameRange(const char *label,
                               const EventVector &events)
{
    if (events.empty()) {
        cerr << label << ": empty" << endl;
        return;
    }

    auto minFrame = events.front().getFrame();
    auto maxFrame = events.front().getFrame() + events.front().getDuration();
    for (const auto &event : events) {
        minFrame = std::min(minFrame, event.getFrame());
        maxFrame = std::max(maxFrame, event.getFrame() + event.getDuration());
    }

    cerr << label << ": count=" << events.size()
         << " start=" << minFrame
         << " end=" << maxFrame << endl;
}

// Wrapper functions
static OverlapProcessor::EventPatch processPitchEvents(sv_frame_t contextStart,
                                                       const std::shared_ptr<SparseTimeValueModel> &fromModel,
                                                       const std::shared_ptr<SparseTimeValueModel> &toModel)
{
    return s_overlapProcessor.processPitchEvents(contextStart,
                                                 fromModel->getAllEvents(),
                                                 toModel->getAllEvents());
}

static OverlapProcessor::EventPatch processNoteEvents(sv_frame_t contextStart,
                                                      const std::shared_ptr<NoteModel> &fromModel,
                                                      const std::shared_ptr<NoteModel> &toModel)
{
    return s_overlapProcessor.processNoteEvents(contextStart,
                                                fromModel->getAllEvents(),
                                                toModel->getAllEvents());
}

static std::map<QString, bool> getAnalysisSettingsFromSettings()
{
    std::map<QString, bool> analysisSettings;

    QSettings settings;
    settings.beginGroup("Analyser");

    analysisSettings["precision-analysis"] = settings.value("precision-analysis", false).toBool();
    analysisSettings["lowamp-analysis"] = settings.value("lowamp-analysis", true).toBool();
    analysisSettings["onset-analysis"] = settings.value("onset-analysis", true).toBool();
    analysisSettings["prune-analysis"] = settings.value("prune-analysis", true).toBool();

    settings.endGroup();

    return analysisSettings;
}

static void setAnalysisSettings(Transform &transform)
{
    const auto analysisSettings = getAnalysisSettingsFromSettings();

    if (analysisSettings.count("precision-analysis") > 0) {
        bool precise = analysisSettings.at("precision-analysis");
        if (precise) {
            cerr << "setting parameters for precise mode" << endl;
            transform.setParameter("precisetime", 1);
        } else {
            cerr << "setting parameters for vague mode" << endl;
            transform.setParameter("precisetime", 0);
        }
    }

    if (analysisSettings.count("lowamp-analysis") > 0) {
        bool lowamp = analysisSettings.at("lowamp-analysis");
        if (lowamp) {
            cerr << "setting parameters for lowamp suppression" << endl;
            transform.setParameter("lowampsuppression", 0.2f);
        } else {
            cerr << "setting parameters for no lowamp suppression" << endl;
            transform.setParameter("lowampsuppression", 0.0f);
        }
    }

    if (analysisSettings.count("onset-analysis") > 0) {
        bool onset = analysisSettings.at("onset-analysis");
        if (onset) {
            cerr << "setting parameters for increased onset sensitivity" << endl;
            transform.setParameter("onsetsensitivity", 0.7f);
        } else {
            cerr << "setting parameters for non-increased onset sensitivity" << endl;
            transform.setParameter("onsetsensitivity", 0.0f);
        }
    }

    if (analysisSettings.count("prune-analysis") > 0) {
        bool prune = analysisSettings.at("prune-analysis");
        if (prune) {
            cerr << "setting parameters for duration pruning" << endl;
            transform.setParameter("prunethresh", 0.1f);
        } else {
            cerr << "setting parameters for no duration pruning" << endl;
            transform.setParameter("prunethresh", 0.0f);
        }
    }
}

RealtimeAnalyser::RealtimeAnalyser(QObject *parent) :
    QObject(parent)
{
}

RealtimeAnalyser::~RealtimeAnalyser()
{
    cleanup();
}

void
RealtimeAnalyser::setContext(Document *document,
                             ModelId fileModel,
                             Pane *pane,
                             TimeValueLayer *targetPitchLayer,
                             FlexiNoteLayer *targetNoteLayer)
{
    QMutexLocker locker(&m_mutex);

    m_ctx.document = document;
    m_ctx.fileModel = fileModel;
    m_ctx.pane = pane;
    m_ctx.targetPitchLayer = targetPitchLayer;
    m_ctx.targetNoteLayer = targetNoteLayer;
}

void
RealtimeAnalyser::clearContext()
{
    QMutexLocker locker(&m_mutex);

    m_ctx.document = nullptr;
    m_ctx.fileModel = ModelId();
    m_ctx.pane = nullptr;
    m_ctx.targetPitchLayer = nullptr;
    m_ctx.targetNoteLayer = nullptr;
}

void
RealtimeAnalyser::cleanup()
{
    std::vector<QPointer<Layer>> layersToClean;
    QPointer<Document> doc;
    QPointer<Pane> pane;
    std::optional<Selection> pendingToRestart;
    quint64 newGeneration = 0;
    bool retiredInFlight = false;

    {
        QMutexLocker locker(&m_mutex);

        layersToClean.swap(m_tempLayers);

        // Invalidate current callbacks and discard any queued follow-up work.
        // If we are destroying temp layers for an in-flight run, that run can
        // no longer complete naturally, so retire it here.
        if (m_inFlight && !layersToClean.empty()) {
            retiredInFlight = true;
            m_inFlight = false;
            pendingToRestart = std::exchange(m_pendingSelection, std::nullopt);
        } else {
            m_pendingSelection = std::nullopt;
        }

        ++m_generation;
        newGeneration = m_generation;

        doc = m_ctx.document;
        pane = m_ctx.pane;

        cerr << "RealtimeAnalyser::cleanup: generation=" << newGeneration
             << " inFlight=" << m_inFlight
             << " retiredInFlight=" << retiredInFlight
             << " pending=" << (m_pendingSelection.has_value() ? "yes" : "no")
             << " restartPending=" << (pendingToRestart.has_value() ? "yes" : "no")
             << " tempLayers=" << layersToClean.size() << endl;
    }

    cleanupTempLayers(std::move(layersToClean), doc, pane);

    if (pendingToRestart) {
        cerr << "RealtimeAnalyser::cleanup: restarting pending selection after forced retirement"
             << " start=" << pendingToRestart->getStartFrame()
             << " end=" << pendingToRestart->getEndFrame() << endl;
        (void)analyseChunk(*pendingToRestart);
    }
}

void
RealtimeAnalyser::invalidateGeneration()
{
    QMutexLocker locker(&m_mutex);

    // If we invalidate generation, any existing callbacks become stale.
    // Do not clear m_inFlight here: the owning run must retire itself via
    // completion so we don't break the in-flight/pending state machine.
    ++m_generation;
    m_pendingSelection = std::nullopt;

    cerr << "RealtimeAnalyser::invalidateGeneration: generation=" << m_generation
         << " inFlight=" << m_inFlight
         << " pending=" << (m_pendingSelection.has_value() ? "yes" : "no") << endl;
}

QString
RealtimeAnalyser::analyseChunk(Selection sel)
{
    bool startedChunk = false;

    if (sel.isEmpty()) return "";

    quint64 generation = 0;
    QPointer<Document> safeDocument;
    QPointer<Pane> safePane;
    QPointer<TimeValueLayer> safeTargetPitchLayer;
    QPointer<FlexiNoteLayer> safeTargetNoteLayer;
    ModelId fileModel;
    std::shared_ptr<WaveFileModel> waveFileModel;

    {
        QMutexLocker locker(&m_mutex);

        if (m_inFlight) {
            m_pendingSelection = sel;
            cerr << "RealtimeAnalyser::analyseChunk: already in flight, replacing pending selection"
                 << " generation=" << m_generation
                 << " pendingStart=" << sel.getStartFrame()
                 << " pendingEnd=" << sel.getEndFrame() << endl;
            return "";
        }

        if (!m_ctx.document || !m_ctx.pane) {
            return "Internal error: RealtimeAnalyser::analyseChunk() called with no document or pane present";
        }

        if (m_ctx.fileModel.isNone()) {
            return "Internal error: RealtimeAnalyser::analyseChunk() called with no model present";
        }

        if (!m_ctx.targetPitchLayer || !m_ctx.targetNoteLayer) {
            return "Internal error: RealtimeAnalyser::analyseChunk() called with no target pitch/note layers present";
        }

        m_inFlight = true;
        startedChunk = true;
        generation = m_generation;

        safeDocument = m_ctx.document;
        safePane = m_ctx.pane;
        safeTargetPitchLayer = m_ctx.targetPitchLayer;
        safeTargetNoteLayer = m_ctx.targetNoteLayer;
        fileModel = m_ctx.fileModel;

        cerr << "RealtimeAnalyser::analyseChunk: acquired in-flight slot"
             << " generation=" << generation
             << " start=" << sel.getStartFrame()
             << " end=" << sel.getEndFrame() << endl;
    }

    waveFileModel = ModelById::getAs<WaveFileModel>(fileModel);
    if (!waveFileModel) {
        if (startedChunk) finishChunk();
        return "Internal error: RealtimeAnalyser::analyseChunk() called with no WaveFileModel";
    }

    auto finishIfStarted = [this, startedChunk]() {
        if (startedChunk) {
            finishChunk();
        }
    };

    auto cleanupTempLayer = [this](QPointer<Layer> safeTempLayer,
                                  QPointer<Document> doc,
                                  QPointer<Pane> pane) {
        Layer *layerToDelete = safeTempLayer.data();
        if (!layerToDelete) return;

        {
            QMutexLocker locker(&m_mutex);
            untrackTempLayerLocked(layerToDelete);
        }

        if (doc && pane) {
            doc->removeLayerFromView(pane.data(), layerToDelete);
            if (safeTempLayer) {
                doc->deleteLayer(layerToDelete);
            }
        }
    };

    auto state = std::make_shared<RealtimeChunkState>();

    auto completePart = [this, state, generation](bool canFinish) {
        --state->remainingParts;
        cerr << "RealtimeAnalyser::completePart: generation=" << generation
             << " canFinish=" << canFinish
             << " remainingParts=" << state->remainingParts << endl;
        if (state->remainingParts == 0) {
            cerr << "RealtimeAnalyser::completePart: retiring chunk for generation=" << generation
                 << " finishAllowed=" << canFinish << endl;
            finishChunk();
        }
    };

    TransformFactory *tf = TransformFactory::getInstance();

    const auto f0_transform = QString(PYIN_TRANSFORM_BASE) + QString(PYIN_F0_OUT);
    const auto note_transform = QString(PYIN_TRANSFORM_BASE) + QString(PYIN_NOTE_OUT);

    QString notFound =
        tr("Transform \"%1\" not found. Unable to perform interactive analysis."
           "<br><br>Are the %2 and %3 Vamp plugins correctly installed?");

    if (!tf->haveTransform(f0_transform)) {
        finishIfStarted();
        return notFound.arg(f0_transform).arg(PYIN_PLUGIN_NAME);
    }

    if (!tf->haveTransform(note_transform)) {
        finishIfStarted();
        return notFound.arg(note_transform).arg(PYIN_PLUGIN_NAME);
    }

    Transform t = tf->getDefaultTransformFor(f0_transform, waveFileModel->getSampleRate());
    t.setStepSize(256);
    t.setBlockSize(2048);

    setAnalysisSettings(t);

    const RealTime start =
        RealTime::frame2RealTime(sel.getStartFrame(), waveFileModel->getSampleRate());
    const RealTime end =
        RealTime::frame2RealTime(sel.getEndFrame(), waveFileModel->getSampleRate());

    RealTime duration;
    if (sel.getEndFrame() > sel.getStartFrame()) {
        duration = end - start;
    }

    cerr << "RealtimeAnalyser::analyseChunk: start " << start
         << " end " << end
         << " original selection start " << sel.getStartFrame()
         << " end " << sel.getEndFrame()
         << " duration " << duration << endl;

    if (duration <= RealTime::zeroTime) {
        cerr << "RealtimeAnalyser::analyseChunk: duration <= 0, not analysing" << endl;
        finishIfStarted();
        return "";
    }

    t.setStartTime(start);
    t.setDuration(duration);

    Transforms transforms;
    transforms.push_back(t);

    t.setOutput(PYIN_NOTE_OUT);
    transforms.push_back(t);

    if (!safeDocument) {
        finishIfStarted();
        return "Internal error: RealtimeAnalyser::analyseChunk() document deleted during scheduling";
    }

    const std::vector<Layer *> layers = safeDocument->createDerivedLayers(transforms, fileModel);

    if (layers.empty()) {
        cerr << "WARNING: RealtimeAnalyser::analyseChunk: no layers returned from createDerivedLayers" << endl;
        finishIfStarted();
        return "";
    }

    {
        QMutexLocker locker(&m_mutex);
        for (auto *layer : layers) {
            m_tempLayers.push_back(QPointer<Layer>(layer));
        }
    }

    ColourDatabase *cdb = ColourDatabase::getInstance();

    for (auto *layer : layers) {

        if (auto *tempPitchLayer = qobject_cast<TimeValueLayer *>(layer)) {

            ++state->remainingParts;
            tempPitchLayer->setBaseColour(cdb->getColourIndex(tr("Black")));

            QPointer<TimeValueLayer> safeTempLayer(tempPitchLayer);

            QObject::connect(
                tempPitchLayer,
                &TimeValueLayer::modelCompletionChanged,
                this,
                [this, safeTempLayer, safeTargetPitchLayer, safeDocument, safePane,
                 sel, state, generation, cleanupTempLayer, completePart](ModelId modelId) {

                    const auto fromModel = ModelById::getAs<SparseTimeValueModel>(modelId);
                    if (!fromModel || fromModel->getCompletion() != 100) {
                        return;
                    }

                    cerr << "RealtimeAnalyser::analyseChunk: Processing pitch track completion" << endl;

                    bool stale = false;
                    {
                        QMutexLocker locker(&m_mutex);
                        stale = (generation != m_generation);
                    }

                    if (stale) {
                        cerr << "RealtimeAnalyser::analyseChunk: Ignoring stale pitch callback from old generation"
                             << " callbackGeneration=" << generation << endl;
                        cleanupTempLayer(safeTempLayer, safeDocument, safePane);
                        completePart(false);
                        return;
                    }

                    if (safeTargetPitchLayer) {
                        const auto toModel =
                            ModelById::getAs<SparseTimeValueModel>(safeTargetPitchLayer->getModel());

                        if (toModel) {
                            const auto patch =
                                processPitchEvents(sel.getStartFrame(), fromModel, toModel);

                            cerr << "RealtimeAnalyser::pitchPatch: selectionStart=" << sel.getStartFrame()
                                 << " selectionEnd=" << sel.getEndFrame() << endl;
                            logPatchFrameRange("RealtimeAnalyser::pitchPatch.remove", patch.remove);
                            logPatchFrameRange("RealtimeAnalyser::pitchPatch.add", patch.add);

                            for (const Event &p : patch.remove) {
                                toModel->remove(p);
                            }
                            for (const Event &p : patch.add) {
                                toModel->add(p);
                            }
                        } else {
                            cerr << "ERROR: RealtimeAnalyser pitch callback - target model is null" << endl;
                        }
                    } else {
                        cerr << "WARNING: RealtimeAnalyser pitch callback - target layer deleted" << endl;
                    }

                    cleanupTempLayer(safeTempLayer, safeDocument, safePane);

                    if (safeTargetPitchLayer && safePane) {
                        safeTargetPitchLayer->layerParametersChanged();
                        safePane->layerParametersChanged();
                    }
                    emit layersChanged();

                    completePart(true);
                },
                Qt::QueuedConnection);
        }

        if (auto *tempNoteLayer = qobject_cast<FlexiNoteLayer *>(layer)) {

            ++state->remainingParts;
            tempNoteLayer->setBaseColour(cdb->getColourIndex(tr("Bright Blue")));

            QPointer<FlexiNoteLayer> safeTempLayer(tempNoteLayer);

            QObject::connect(
                tempNoteLayer,
                &FlexiNoteLayer::modelCompletionChanged,
                this,
                [this, safeTempLayer, safeTargetNoteLayer, safeDocument, safePane,
                 sel, state, generation, cleanupTempLayer, completePart](ModelId modelId) {

                    const auto fromModel = ModelById::getAs<NoteModel>(modelId);
                    if (!fromModel || fromModel->getCompletion() != 100) {
                        return;
                    }

                    cerr << "RealtimeAnalyser::analyseChunk: Processing note layer completion" << endl;

                    bool stale = false;
                    {
                        QMutexLocker locker(&m_mutex);
                        stale = (generation != m_generation);
                    }

                    if (stale) {
                        cerr << "RealtimeAnalyser::analyseChunk: Ignoring stale note callback from old generation"
                             << " callbackGeneration=" << generation << endl;
                        cleanupTempLayer(safeTempLayer, safeDocument, safePane);
                        completePart(false);
                        return;
                    }

                    if (safeTargetNoteLayer) {
                        const auto toModel =
                            ModelById::getAs<NoteModel>(safeTargetNoteLayer->getModel());

                        if (toModel) {
                            const auto patch =
                                processNoteEvents(sel.getStartFrame(), fromModel, toModel);

                            for (const Event &p : patch.remove) {
                                toModel->remove(p);
                            }
                            for (const Event &p : patch.add) {
                                toModel->add(p);
                            }
                        } else {
                            cerr << "ERROR: RealtimeAnalyser note callback - target model is null" << endl;
                        }
                    } else {
                        cerr << "WARNING: RealtimeAnalyser note callback - target layer deleted" << endl;
                    }

                    cleanupTempLayer(safeTempLayer, safeDocument, safePane);

                    emit layersChanged();

                    completePart(true);
                },
                Qt::QueuedConnection);
        }
    }

    if (state->remainingParts == 0) {
        cerr << "WARNING: RealtimeAnalyser::analyseChunk: no recognised temp layers created" << endl;
        finishIfStarted();
    }

    return "";
}

bool
RealtimeAnalyser::hasValidContextLocked() const
{
    return (m_ctx.document && m_ctx.pane &&
            !m_ctx.fileModel.isNone() &&
            m_ctx.targetPitchLayer &&
            m_ctx.targetNoteLayer);
}

bool
RealtimeAnalyser::isStaleGenerationLocked(quint64 generation) const
{
    return generation != m_generation;
}

void
RealtimeAnalyser::untrackTempLayerLocked(Layer *layer)
{
    m_tempLayers.erase(
        std::remove_if(m_tempLayers.begin(),
                       m_tempLayers.end(),
                       [layer](const QPointer<Layer> &p) {
                           return p.isNull() || p.data() == layer;
                       }),
        m_tempLayers.end());
}

void
RealtimeAnalyser::cleanupTempLayers(std::vector<QPointer<Layer>> layersToClean,
                                   const QPointer<Document> &doc,
                                   const QPointer<Pane> &pane)
{
    if (!doc || !pane) return;

    for (const auto &layerPtr : layersToClean) {
        Layer *layer = layerPtr.data();
        if (!layer) continue;

        doc->removeLayerFromView(pane.data(), layer);
        doc->deleteLayer(layer);
    }
}

void
RealtimeAnalyser::finishChunk()
{
    std::optional<Selection> pending;
    quint64 generation = 0;

    {
        QMutexLocker locker(&m_mutex);

        m_inFlight = false;
        pending = std::exchange(m_pendingSelection, std::nullopt);
        generation = m_generation;

        cerr << "RealtimeAnalyser::finishChunk: generation=" << generation
             << " pending=" << (pending.has_value() ? "yes" : "no") << endl;
    }

    if (pending) {
        cerr << "RealtimeAnalyser::finishChunk: starting pending realtime selection"
             << " start=" << pending->getStartFrame()
             << " end=" << pending->getEndFrame() << endl;
        (void)analyseChunk(*pending);
    }
}