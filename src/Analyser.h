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

#include <map>
#include <vector>

#include "framework/Document.h"
#include "base/Selection.h"
#include "base/Clipboard.h"

class WaveFileModel;
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

    // Process new main model, add derived layers; return "" on success or error string on failure
    QString newFileLoaded(Document *newDocument, WaveFileModel *model,
                          PaneStack *paneStack, Pane *pane);
		       
    void setIntelligentActions(bool);

    enum Component {
        Audio,
        PitchTrack,
        Notes,
        Spectrogram,
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

    float getGain(Component c) const;
    void setGain(Component c, float gain);

    float getPan(Component c) const;
    void setPan(Component c, float pan);

    void getEnclosingSelectionScope(size_t f, size_t &f0, size_t &f1);

    struct FrequencyRange {
        FrequencyRange() : min(0), max(0) { }
        FrequencyRange(float min_, float max_) : min(min_), max(max_) { }
        bool isConstrained() const { return min != max; }
        float min;
        float max;
    };

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
     * visible (by default they are hidden after construction until
     * the user requests them). Note that the shown/hidden state is
     * independent of whether any pitch candidates actually exist --
     * it's possible they might be shown but not have been created yet
     * because creation (through reAnalyseSelection) is asynchronous.
     *
     *!!! this interface is not right
     */
    bool arePitchCandidatesShown() const;

    /**
     * Show or hide the analysed pitch candidate layers. As in
     * arePitchCandidatesShown, this is independent of whether the
     * candidate layers actually exist. Call reAnalyseSelection to
     * schedule creation of those layers.
     *
     *!!! this interface is not right
     */
    void showPitchCandidates(bool shown);

    bool haveHigherPitchCandidate() const;
    bool haveLowerPitchCandidate() const;

    /**
     * If a re-analysis has been activated, switch the selected area
     * of the main pitch track to a different candidate from the
     * analysis results.
     */
    void switchPitchCandidate(Selection sel, bool up);

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
     * Remove any re-analysis layers (equivalent to
     * showPitchCandidates(false)) and also reset the pitch track in
     * the given selection to its state prior to the last re-analysis,
     * abandoning any changes made since then.
     */
    void clearReAnalysis(Selection sel);

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

protected:
    Document *m_document;
    WaveFileModel *m_fileModel;
    PaneStack *m_paneStack;
    Pane *m_pane;

    mutable std::map<Component, Layer *> m_layers;

    Clipboard m_preAnalysis;
    Selection m_reAnalysingSelection;
    std::vector<Layer *> m_reAnalysisCandidates;
    int m_currentCandidate;
    bool m_candidatesVisible;

    QString addVisualisations();
    QString addWaveform();
    QString addAnalyses();

    // Document::LayerCreationHandler method
    void layersCreated(std::vector<Layer *>, std::vector<Layer *>);

    void saveState(Component c) const;
    void loadState(Component c);
};

#endif
