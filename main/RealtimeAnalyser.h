/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

 /*
     Tony
     Realtime analysis helper (extracted from Analyser)
 */

#ifndef REALTIMEANALYSER_H
#define REALTIMEANALYSER_H

#include <QObject>
#include <QPointer>
#include <QMutex>
#include <QtGlobal>
#include <QString>

#include <optional>
#include <vector>

#include "framework/Document.h"
#include "base/Selection.h"

namespace sv {
class Pane;
class Layer;
class TimeValueLayer;
class FlexiNoteLayer;
}

class RealtimeAnalyser : public QObject
{
    Q_OBJECT

public:
    explicit RealtimeAnalyser(QObject *parent = nullptr);
    ~RealtimeAnalyser() override;

    void setContext(sv::Document *document,
                    sv::ModelId fileModel,
                    sv::Pane *pane,
                    sv::TimeValueLayer *targetPitchLayer,
                    sv::FlexiNoteLayer *targetNoteLayer);

    void clearContext();

    // Cancels in-flight work, clears pending selection, removes temp layers, bumps generation
    void cleanup();

    // Ignore stale callbacks after external state changes; does not delete layers
    void invalidateGeneration();

    // One-in-flight realtime analysis: if a chunk is already running, replaces the pending selection
    QString analyseChunk(sv::Selection sel);

signals:
    void layersChanged();

private:
    struct Context {
        QPointer<sv::Document> document;
        sv::ModelId fileModel;
        QPointer<sv::Pane> pane;
        QPointer<sv::TimeValueLayer> targetPitchLayer;
        QPointer<sv::FlexiNoteLayer> targetNoteLayer;
    };

    struct RealtimeChunkState {
        int remainingParts = 0;
    };

    static constexpr const char* PYIN_PLUGIN_NAME = "pYIN";
    static constexpr const char* PYIN_TRANSFORM_BASE = "vamp:pyin:pyin:";
    static constexpr const char* PYIN_F0_OUT = "smoothedpitchtrack";
    static constexpr const char* PYIN_NOTE_OUT = "notes";

    bool hasValidContextLocked() const;
    bool isStaleGenerationLocked(quint64 generation) const;

    void untrackTempLayerLocked(sv::Layer *layer);
    void cleanupTempLayers(std::vector<QPointer<sv::Layer>> layersToClean,
                           const QPointer<sv::Document> &doc,
                           const QPointer<sv::Pane> &pane);

    void finishChunk();

private:
    mutable QMutex m_mutex;
    Context m_ctx;
    std::vector<QPointer<sv::Layer>> m_tempLayers;

    bool m_inFlight = false;
    std::optional<sv::Selection> m_pendingSelection;
    quint64 m_generation = 0;
};

#endif