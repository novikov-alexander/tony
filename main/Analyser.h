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

#ifndef ANALYSER_H
#define ANALYSER_H

#include <QObject>
#include <QRect>
#include <QMutex>

#include <map>
#include <vector>

#include "framework/Document.h"
#include "base/Selection.h"
#include "base/Clipboard.h"
#include "data/model/WaveFileModel.h"

class Pane;
class PaneStack;
class Layer;
class TimeValueLayer;
class Layer;

class Analyser : public QObject,
                 public Document::LayerCreationHandler
{
    Q_OBJECT

public:
    Analyser();
    virtual ~Analyser();

    // Process new main model, add derived layers; return "" on
    // success or error string on failure
    QString newFileLoaded(Document *newDocument,
                          ModelId model,
                          PaneStack *paneStack,
                          Pane *pane);

    // Remove any derived layers, process the main model, add derived
    // layers; return "" on success or error string on failure
    QString analyseExistingFile();

    // Discard any layers etc associated with the current document
    void fileClosed();
		       
    void setIntelligentActions(bool);

    bool getDisplayFrequencyExtents(double &min, double &max);
    bool setDisplayFrequencyExtents(double min, double max);

    // Return completion %age for initial analysis -- 100 means it's done
    int getInitialAnalysisCompletion();

    enum Component {
        Audio = 0,
        PitchTrack = 1,
        Notes = 2,
        Spectrogram = 3,
    };

    bool isVisible(Component c) const;
    void setVisible(Component c, bool v);
    void toggleVisible(Component c) { setVisible(c, !isVisible(c)); }

    bool isAudible(Component c) const;
    void setAudible(Component c, bool v);
    void toggleAudible(Component c) { setAudible(c, !isAudible(c)); }

    void cycleStatus(Component c) {
        if (isVisible(c)) {
            if (isAudible(c)) {
                setVisible(c, false);
                setAudible(c, false);
            } else {
                setAudible(c, true);
            }
        } else {
            setVisible(c, true);
            setAudible(c, false);
        }
    }

    ModelId getMainModelId() const {
        return m_fileModel;
    }
    std::shared_ptr<WaveFileModel> getMainModel() const {
        return ModelById::getAs<WaveFileModel>(m_fileModel);
    }

    float getGain(Component c) const;
    void setGain(Component c, float gain);

    float getPan(Component c) const;
    void setPan(Component c, float pan);

    void getEnclosingSelectionScope(sv_frame_t f, sv_frame_t &f0, sv_frame_t &f1);

    struct FrequencyRange {
        FrequencyRange() : min(0), max(0) { }
        FrequencyRange(double min_, double max_) : min(min_), max(max_) { }
        bool isConstrained() const { return min != max; }
        double min;
        double max;
        bool operator==(const FrequencyRange &r) {
            return min == r.min && max == r.max;
        }
    };

    /**
     * Return the QSettings keys, and their default values, that
     * affect analysis behaviour. These all live within the Analyser
     * group in QSettings.
     */
    static std::map<QString, QVariant> getAnalysisSettings();
    
    /**
     * Analyse the selection and schedule asynchronous adds of
     * candidate layers for the region it contains. Returns "" on
     * success or a user-readable error string on failure. If the
     * frequency range isConstrained(), analysis will be constrained
     * to that range.
     */
    QString reAnalyseSelection(Selection sel, FrequencyRange range);

    /**
     * Return true if the analysed pitch candidates are currently
     * visible (they are hidden from the call to reAnalyseSelection
     * until they are requested through showPitchCandidates()). Note
     * that this may return true even when no pitch candidate layers
     * actually exist yet, because they are constructed
     * asynchronously. If that is the case, then the layers will
     * appear when they are created (otherwise they will remain hidden
     * after creation).
     */
    bool arePitchCandidatesShown() const;

    /**
     * Show or hide the analysed pitch candidate layers. This is reset
     * (to "hide") with each new call to reAnalyseSelection. Because
     * the layers are created asynchronously, setting this to true
     * does not guarantee that they appear immediately, only that they
     * will appear once they have been created.
     */
    void showPitchCandidates(bool shown);

    /**
     * If a re-analysis has been activated, switch the selected area
     * of the main pitch track to a different candidate from the
     * analysis results.
     */
    void switchPitchCandidate(Selection sel, bool up);

    /**
     * Return true if it is possible to switch up to another pitch
     * candidate. This may mean that the currently selected pitch
     * candidate is not the highest, or it may mean that no alternate
     * pitch candidate has been selected at all yet (but some are
     * available).
     */
    bool haveHigherPitchCandidate() const;

    /**
     * Return true if it is possible to switch down to another pitch
     * candidate. This may mean that the currently selected pitch
     * candidate is not the lowest, or it may mean that no alternate
     * pitch candidate has been selected at all yet (but some are
     * available).
     */
    bool haveLowerPitchCandidate() const;

    /**
     * Delete the pitch estimates from the selected area of the main
     * pitch track.
     */
    void deletePitches(Selection sel);

    /**
     * Move the main pitch track and any active analysis candidate
     * tracks up or down an octave in the selected area.
     */
    void shiftOctave(Selection sel, bool up);

    /**
     * Remove any re-analysis layers and also reset the pitch track in
     * the given selection to its state prior to the last re-analysis,
     * abandoning any changes made since then. No re-analysis layers
     * will be available until after the next call to
     * reAnalyseSelection.
     */
    void abandonReAnalysis(Selection sel);

    /**
     * Remove any re-analysis layers, without any expectation of
     * adding them later, unlike showPitchCandidates(false), and
     * without changing the current pitch track, unlike
     * abandonReAnalysis().
     */
    void clearReAnalysis();

    /**
     * Import the pitch track from the given layer into our
     * pitch-track layer.
     */
    void takePitchTrackFrom(Layer *layer);

    Pane *getPane() {
        return m_pane;
    }

    Layer *getLayer(Component type) {
        return m_layers[type];
    }

signals:
    void layersChanged();
    void initialAnalysisCompleted();

protected slots:
    void layerAboutToBeDeleted(Layer *);
    void layerCompletionChanged(ModelId);
    void reAnalyseRegion(sv_frame_t, sv_frame_t, float, float);
    void materialiseReAnalysis();

protected:
    Document *m_document;
    ModelId m_fileModel;
    PaneStack *m_paneStack;
    Pane *m_pane;

    mutable std::map<Component, Layer *> m_layers;

    Clipboard m_preAnalysis;
    Selection m_reAnalysingSelection;
    FrequencyRange m_reAnalysingRange;
    std::vector<Layer *> m_reAnalysisCandidates;
    int m_currentCandidate;
    bool m_candidatesVisible;
    Document::LayerCreationAsyncHandle m_currentAsyncHandle;
    QMutex m_asyncMutex;

    QString doAllAnalyses(bool withPitchTrack);

    QString addVisualisations();
    QString addWaveform();
    QString addAnalyses();

    void discardPitchCandidates();

    void stackLayers();
    
    // Document::LayerCreationHandler method
    void layersCreated(Document::LayerCreationAsyncHandle,
                       std::vector<Layer *>, std::vector<Layer *>);

    void saveState(Component c) const;
    void loadState(Component c);
};

#endif
