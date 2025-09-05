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

#ifndef OVERLAP_PROCESSOR_H
#define OVERLAP_PROCESSOR_H

#include <vector>
#include <memory>

// Forward declarations - includes will be in the .cpp file
namespace sv {
    class Event;
    typedef std::vector<Event> EventVector;
    class SparseTimeValueModel;
    class NoteModel;
    typedef int64_t sv_frame_t;
}

using namespace sv;

/**
 * Configuration parameters for overlap processing algorithms
 */
struct OverlapConfig {
    sv_frame_t interpolationThreshold = 512;  // ~11ms at 44.1kHz
    double pitchSimilarityThreshold = 0.1;    // 10% pitch difference threshold
    sv_frame_t overlapTolerance = 1000;       // Tolerance for event matching
    
    OverlapConfig() = default;
    OverlapConfig(sv_frame_t interpThresh, double pitchThresh, sv_frame_t tolerance)
        : interpolationThreshold(interpThresh)
        , pitchSimilarityThreshold(pitchThresh)
        , overlapTolerance(tolerance) {}
};

/**
 * Structure to represent a group of overlapping events
 */
struct OverlapGroup {
    EventVector events;
    sv_frame_t startFrame;
    sv_frame_t endFrame;
    
    explicit OverlapGroup(const EventVector& evts);
    bool isEmpty() const { return events.empty(); }
    size_t size() const { return events.size(); }
};

/**
 * Main overlap processing class that handles detection and merging of overlapping events
 */
class OverlapProcessor {
public:
    explicit OverlapProcessor(const OverlapConfig& config = OverlapConfig());
    
    // Core overlap detection and processing
    std::vector<OverlapGroup> findOverlapGroups(const EventVector& events) const;
    Event mergeOverlapGroup(const OverlapGroup& group) const;
    
    // Frequency calculation methods
    float calculateWeightedFrequency(const EventVector& overlappingEvents, 
                                   sv_frame_t overlapStart, 
                                   sv_frame_t overlapDuration) const;
    
    float calculateWeightedFrequency(const Event& prevEvent, 
                                   const Event& nextEvent,
                                   sv_frame_t overlapStart, 
                                   sv_frame_t overlapDuration) const;
    
    // Main processing methods for different model types
    EventVector processPitchModel(sv_frame_t contextStart, 
                                std::shared_ptr<SparseTimeValueModel> fromModel, 
                                std::shared_ptr<SparseTimeValueModel> toModel) const;
    
    EventVector processNoteModel(sv_frame_t contextStart, 
                               std::shared_ptr<NoteModel> fromModel, 
                               std::shared_ptr<NoteModel> toModel) const;

    // Configuration access
    const OverlapConfig& getConfig() const { return m_config; }
    void setConfig(const OverlapConfig& config) { m_config = config; }

private:
    OverlapConfig m_config;
    
    // Helper methods
    bool eventsOverlap(const Event& a, const Event& b) const;
    const Event* findLongestEvent(const EventVector& events) const;
    void categorizeEvents(const EventVector& allEvents, 
                         const EventVector& newEvents,
                         EventVector& eventsToRemove,
                         EventVector& remainingEvents) const;
};

#endif // OVERLAP_PROCESSOR_H
