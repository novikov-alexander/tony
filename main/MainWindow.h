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

#ifndef TONY_MAIN_WINDOW_H
#define TONY_MAIN_WINDOW_H

#include "framework/MainWindowBase.h"
#include "Analyser.h"

namespace sv {
class VersionTester;
class ActivityLog;
class LevelPanToolButton;
}

class MainWindow : public sv::MainWindowBase
{
    Q_OBJECT

public:
    MainWindow(AudioMode audioMode,
               bool withSonification = true, 
               bool withSpectrogram = true);
    virtual ~MainWindow();

signals:
    void canExportPitchTrack(bool);
    void canExportNotes(bool);
    void canSnapNotes(bool);
    void canPlayWaveform(bool);
    void canPlayPitch(bool);
    void canPlayNotes(bool);

public slots:
    virtual bool commitData(bool mayAskUser); // on session shutdown

protected slots:
    virtual void openFile();
    virtual void openLocation();
    virtual void openRecentFile();
    virtual void saveSession();
    virtual void saveSessionInAudioPath();
    virtual void saveSessionAs();
    virtual void exportPitchLayer();
    virtual void exportNoteLayer();
    virtual void importPitchLayer();
    virtual void browseRecordedAudio();
    virtual void newSession();
    virtual void closeSession();

    virtual void toolNavigateSelected();
    virtual void toolEditSelected();
    virtual void toolFreeEditSelected();

    virtual void clearPitches();
    virtual void togglePitchCandidates();
    virtual void switchPitchUp();
    virtual void switchPitchDown();

    virtual void snapNotesToPitches();
    virtual void splitNote();
    virtual void mergeNotes();
    virtual void deleteNotes();
    virtual void formNoteFromSelection();

    virtual void showAudioToggled();
    virtual void showSpectToggled();
    virtual void showPitchToggled();
    virtual void showNotesToggled();

    virtual void playAudioToggled();
    virtual void playPitchToggled();
    virtual void playNotesToggled();

    virtual void editDisplayExtents();

    virtual void analyseNow();
    virtual void resetAnalyseOptions();
    virtual void autoAnalysisToggled();
    virtual void precisionAnalysisToggled();
    virtual void lowampAnalysisToggled();
    virtual void onsetAnalysisToggled();
    virtual void pruneAnalysisToggled();
    virtual void updateAnalyseStates();

    virtual void doubleClickSelectInvoked(sv::sv_frame_t);
    virtual void abandonSelection();

    virtual void paneAdded(sv::Pane *);
    virtual void paneHidden(sv::Pane *);
    virtual void paneAboutToBeDeleted(sv::Pane *);

    virtual void paneDropAccepted(sv::Pane *, QStringList);
    virtual void paneDropAccepted(sv::Pane *, QString);

    virtual void playSpeedChanged(int);
    virtual void playSharpenToggled();
    virtual void playMonoToggled();

    virtual void speedUpPlayback();
    virtual void slowDownPlayback();
    virtual void restoreNormalPlayback();

    virtual void monitoringLevelsChanged(float, float);

    virtual void audioGainChanged(float);
    virtual void pitchGainChanged(float);
    virtual void notesGainChanged(float);

    virtual void audioPanChanged(float);
    virtual void pitchPanChanged(float);
    virtual void notesPanChanged(float);

    virtual void sampleRateMismatch(sv::sv_samplerate_t, sv::sv_samplerate_t, bool);
    virtual void audioOverloadPluginDisabled();

    virtual void documentModified();
    virtual void documentRestored();
    virtual void documentReplaced();

    virtual void updateMenuStates();
    virtual void updateDescriptionLabel();
    virtual void updateLayerStatuses();

    virtual void layerRemoved(sv::Layer *);
    virtual void layerInAView(sv::Layer *, bool);

    virtual void mainModelChanged(sv::ModelId);
    virtual void mainModelGainChanged(float);
    virtual void modelAdded(sv::ModelId);

    virtual void modelGenerationFailed(QString, QString);
    virtual void modelGenerationWarning(QString, QString);
    virtual void modelRegenerationFailed(QString, QString, QString);
    virtual void modelRegenerationWarning(QString, QString, QString);
    virtual void alignmentFailed(sv::ModelId, QString);

    virtual void paneRightButtonMenuRequested(sv::Pane *, QPoint point);
    virtual void panePropertiesRightButtonMenuRequested(sv::Pane *, QPoint point);
    virtual void layerPropertiesRightButtonMenuRequested(sv::Pane *, sv::Layer *, QPoint point);

    virtual void setupRecentFilesMenu();

    virtual void handleOSCMessage(const sv::OSCMessage &);

    virtual void mouseEnteredWidget();
    virtual void mouseLeftWidget();

    virtual void help();
    virtual void about();
    virtual void keyReference();
    virtual void whatsNew();

    virtual void betaReleaseWarning();
    
    virtual void newerVersionAvailable(QString);

    virtual void selectionChangedByUser();
    virtual void regionOutlined(QRect);

    virtual void analyseNewMainModel();

    void moveOneNoteRight();
    void moveOneNoteLeft();
    void selectOneNoteRight();
    void selectOneNoteLeft();

    void ffwd();
    void rewind();

protected:
    Analyser      *m_analyser;

    sv::Overview  *m_overview;
    sv::Fader     *m_fader;
    sv::AudioDial *m_playSpeed;
    QPushButton   *m_playSharpen;
    QPushButton   *m_playMono;
    sv::WaveformLayer *m_panLayer;

    bool           m_mainMenusCreated;
    QMenu         *m_playbackMenu;
    QMenu         *m_recentFilesMenu;
    QMenu         *m_rightButtonMenu;
    QMenu         *m_rightButtonPlaybackMenu;

    QAction       *m_deleteSelectedAction;
    QAction       *m_ffwdAction;
    QAction       *m_rwdAction;
    QAction       *m_editSelectAction;
    QAction       *m_showCandidatesAction;
    QAction       *m_toggleIntelligenceAction;
    bool           m_intelligentActionOn; // GF: !!! temporary

    QAction       *m_autoAnalyse;
    QAction       *m_precise;
    QAction       *m_lowamp;
    QAction       *m_onset;
    QAction       *m_prune;
        
    QAction       *m_showAudio;
    QAction       *m_showSpect;
    QAction       *m_showPitch;
    QAction       *m_showNotes;
    QAction       *m_playAudio;
    QAction       *m_playPitch;
    QAction       *m_playNotes;
    sv::LevelPanToolButton *m_audioLPW;
    sv::LevelPanToolButton *m_pitchLPW;
    sv::LevelPanToolButton *m_notesLPW;
    
    sv::ActivityLog   *m_activityLog;
    sv::KeyReference  *m_keyReference;
    sv::VersionTester *m_versionTester;
    QString            m_newerVersionIs;

    sv::sv_frame_t m_selectionAnchor;

    bool m_withSonification;
    bool m_withSpectrogram;

    Analyser::FrequencyRange m_pendingConstraint;

    QString exportToSVL(QString path, sv::Layer *layer);
    FileOpenStatus importPitchLayer(sv::FileSource source);

    QString getReleaseText() const;

    virtual void setupMenus();
    virtual void setupFileMenu();
    virtual void setupEditMenu();
    virtual void setupViewMenu();
    virtual void setupAnalysisMenu();
    virtual void setupHelpMenu();
    virtual void setupToolbars();

    virtual void octaveShift(bool up);

    virtual void auxSnapNotes(sv::Selection s);

    virtual void closeEvent(QCloseEvent *e);
    bool checkSaveModified();
    bool waitForInitialAnalysis();

    virtual void updateVisibleRangeDisplay(sv::Pane *p) const;
    virtual void updatePositionStatusDisplays() const;

    void moveByOneNote(bool right, bool doSelect);
};


#endif
