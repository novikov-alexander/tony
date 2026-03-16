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
#include <cstddef>
#include <optional>

#include "base/Event.h"

namespace sv {
    typedef std::vector<Event> EventVector;
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
    std::vector<size_t> indices;
    sv_frame_t startFrame;
    sv_frame_t endFrame;

    OverlapGroup();
    explicit OverlapGroup(size_t index, const Event &event);
    bool isEmpty() const { return indices.empty(); }
    size_t size() const { return indices.size(); }
    void addEvent(size_t index, const Event &event);
};

/**
 * Main overlap processing class that handles detection and merging of overlapping events
 */
class OverlapProcessor {
public:
    explicit OverlapProcessor(const OverlapConfig& config = OverlapConfig());
    
    // Core overlap detection and processing
    struct EventPatch {
        EventVector remove;
        EventVector add;
    };

    std::vector<OverlapGroup> findOverlapGroups(const EventVector& events) const;
    std::optional<Event> mergeOverlapGroup(const OverlapGroup& group, const EventVector& events) const;
    
    // Frequency calculation methods
    float calculateWeightedFrequency(const EventVector& overlappingEvents,
                                     sv_frame_t overlapStart,
                                     sv_frame_t overlapDuration) const;
    
    // Main processing methods for different model types
    EventPatch processPitchEvents(sv_frame_t contextStart,
                                  const EventVector& incomingEvents,
                                  const EventVector& existingEvents) const;

    EventPatch processNoteEvents(sv_frame_t contextStart,
                                 const EventVector& incomingEvents,
                                 const EventVector& existingEvents) const;

    // Configuration access
    const OverlapConfig& getConfig() const { return m_config; }
    void setConfig(const OverlapConfig& config) { m_config = config; }

private:
    OverlapConfig m_config;
    
    // Helper methods
    bool eventsOverlap(const Event& a, const Event& b) const;
    const Event* findLongestEvent(const OverlapGroup& group, const EventVector& events) const;
    void categorizeEvents(const EventVector& allEvents,
                          const EventVector& newEvents,
                          EventVector& eventsToRemove,
                          EventVector& remainingEvents) const;
};

#endif // OVERLAP_PROCESSOR_H
