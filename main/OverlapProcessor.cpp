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

#include "OverlapProcessor.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/model/NoteModel.h"
#include <algorithm>
#include <cmath>
#include <iostream>

using std::cerr;
using std::endl;

// OverlapGroup implementation
OverlapGroup::OverlapGroup(const EventVector& evts) : events(evts) {
    if (!events.empty()) {
        startFrame = events.front().getFrame();
        endFrame = events.front().getFrame() + events.front().getDuration();
        
        for (const auto& event : events) {
            startFrame = std::min(startFrame, event.getFrame());
            endFrame = std::max(endFrame, event.getFrame() + event.getDuration());
        }
    } else {
        startFrame = endFrame = 0;
    }
}

// OverlapProcessor implementation
OverlapProcessor::OverlapProcessor(const OverlapConfig& config) : m_config(config) {
}

std::vector<OverlapGroup> OverlapProcessor::findOverlapGroups(const EventVector& events) const {
    std::vector<OverlapGroup> groups;
    std::vector<bool> processed(events.size(), false);
    
    for (size_t i = 0; i < events.size(); ++i) {
        if (processed[i]) continue;
        
        EventVector currentGroup;
        currentGroup.push_back(events[i]);
        processed[i] = true;
        
        // Find all events that overlap with any event in the current group
        bool foundOverlap;
        do {
            foundOverlap = false;
            for (size_t j = 0; j < events.size(); ++j) {
                if (processed[j]) continue;
                
                // Check if event j overlaps with any event in the current group
                for (const auto& groupEvent : currentGroup) {
                    if (eventsOverlap(events[j], groupEvent)) {
                        currentGroup.push_back(events[j]);
                        processed[j] = true;
                        foundOverlap = true;
                        break;
                    }
                }
                if (foundOverlap) break;
            }
        } while (foundOverlap);
        
        // Only create groups for actual overlaps (more than one event)
        if (currentGroup.size() > 1) {
            groups.emplace_back(currentGroup);
        }
    }
    
    return groups;
}

float OverlapProcessor::calculateWeightedFrequency(const EventVector& overlappingEvents, 
                                                 sv_frame_t overlapStart, 
                                                 sv_frame_t overlapDuration) const {
    if (overlappingEvents.empty()) return 0.0f;
    if (overlappingEvents.size() == 1) {
        return overlappingEvents[0].hasValue() ? overlappingEvents[0].getValue() : 0.0f;
    }
    
    // Calculate weighted contributions from all events
    std::vector<float> frequencies;
    std::vector<double> weights;
    auto overlapEnd = overlapStart + overlapDuration;
    
    for (const auto& event : overlappingEvents) {
        float freq = event.hasValue() ? event.getValue() : 0.0f;
        if (freq <= 0.0f) continue; // Skip invalid frequencies
        
        // Calculate this event's contribution to the overlap
        auto eventStart = event.getFrame();
        auto eventEnd = event.getFrame() + event.getDuration();
        
        auto eventOverlapStart = std::max(eventStart, overlapStart);
        auto eventOverlapEnd = std::min(eventEnd, overlapEnd);
        auto eventOverlapContrib = std::max(0LL, eventOverlapEnd - eventOverlapStart);
        
        if (eventOverlapContrib > 0) {
            frequencies.push_back(freq);
            weights.push_back(static_cast<double>(eventOverlapContrib));
        }
    }
    
    if (frequencies.empty()) return 0.0f;
    if (frequencies.size() == 1) return frequencies[0];
    
    // Normalize weights
    double totalWeight = 0.0;
    for (double weight : weights) totalWeight += weight;
    if (totalWeight <= 0.0) {
        // Fallback to simple geometric mean
        double logSum = 0.0;
        for (float freq : frequencies) {
            logSum += std::log(freq);
        }
        return std::exp(logSum / frequencies.size());
    }
    
    for (double& weight : weights) weight /= totalWeight;
    
    // Calculate weighted geometric mean for better musical accuracy
    double weightedLogSum = 0.0;
    for (size_t i = 0; i < frequencies.size(); ++i) {
        weightedLogSum += std::log(frequencies[i]) * weights[i];
    }
    
    return std::exp(weightedLogSum);
}

float OverlapProcessor::calculateWeightedFrequency(const Event& prevEvent, 
                                                 const Event& nextEvent,
                                                 sv_frame_t overlapStart, 
                                                 sv_frame_t overlapDuration) const {
    EventVector events = {prevEvent, nextEvent};
    return calculateWeightedFrequency(events, overlapStart, overlapDuration);
}

Event OverlapProcessor::mergeOverlapGroup(const OverlapGroup& group) const {
    if (group.isEmpty()) {
        return Event(0, 0.0f, "");
    }
    
    if (group.size() == 1) {
        return group.events[0];
    }
    
    // Calculate merged event properties
    auto mergedStart = group.startFrame;
    auto mergedEnd = group.endFrame;
    auto mergedDuration = mergedEnd - mergedStart;
    
    // Calculate weighted frequency for the entire overlap
    float weightedFreq = calculateWeightedFrequency(group.events, mergedStart, mergedDuration);
    
    // Use properties from the event with the longest duration as base
    const Event* longestEvent = findLongestEvent(group.events);
    
    // Create merged event
    Event mergedEvent = longestEvent->withFrame(mergedStart)
                                   .withDuration(mergedDuration);
    
    if (weightedFreq > 0.0f) {
        mergedEvent = mergedEvent.withValue(weightedFreq);
    }
    
    // Preserve label if available
    if (longestEvent->hasLabel()) {
        mergedEvent = mergedEvent.withLabel(longestEvent->getLabel());
    }
    
    // Preserve level if available
    if (longestEvent->hasLevel()) {
        mergedEvent = mergedEvent.withLevel(longestEvent->getLevel());
    }
    
    return mergedEvent;
}

EventVector OverlapProcessor::processPitchModel(sv_frame_t contextStart, 
                                              std::shared_ptr<SparseTimeValueModel> fromModel, 
                                              std::shared_ptr<SparseTimeValueModel> toModel) const {
    auto allEvents = toModel->getAllEvents();
    auto points = fromModel->getAllEvents();

    // Add context start timestamp to all points from the new analysis
    std::transform(points.begin(), points.end(), points.begin(), [&](const auto& point) {
        return point.withFrame(point.getFrame() + contextStart);
    });

    // Remove all events from toModel that extend beyond contextStart to prevent overlaps
    EventVector eventsToRemove;
    for (const auto& event : allEvents) {
        if (event.getFrame() >= contextStart) {
            eventsToRemove.push_back(event);
        }
    }
    
    for (const auto& event : eventsToRemove) {
        toModel->remove(event);
    }

    // After cleanup, get the remaining events for overlap processing
    allEvents = toModel->getAllEvents();

    // Handle potential overlaps between the last existing event and first new event
    if (!allEvents.empty() && !points.empty()) {
        auto& lastExistingEvent = allEvents.back();
        auto& firstNewEvent = points.front();

        // Check if there's a gap or overlap between last existing and first new event
        auto gapFrames = firstNewEvent.getFrame() - lastExistingEvent.getFrame();
        
        // If events are very close (within interpolation threshold), interpolate between them
        if (gapFrames > 0 && gapFrames <= m_config.interpolationThreshold) {
            // Small gap - add interpolated point if pitch values are similar
            if (lastExistingEvent.hasValue() && firstNewEvent.hasValue()) {
                auto lastValue = lastExistingEvent.getValue();
                auto firstValue = firstNewEvent.getValue();
                auto valueDiff = std::abs(lastValue - firstValue) / lastValue;
                
                // Only interpolate if pitch values are within similarity threshold
                if (valueDiff <= m_config.pitchSimilarityThreshold) {
                    auto midFrame = lastExistingEvent.getFrame() + gapFrames / 2;
                    auto midValue = (lastValue + firstValue) / 2.0;
                    Event interpolatedEvent = Event(midFrame, midValue, "interpolated");
                    toModel->add(interpolatedEvent);
                }
            }
        }
    }

    return points;
}

EventVector OverlapProcessor::processNoteModel(sv_frame_t contextStart, 
                                             std::shared_ptr<NoteModel> fromModel, 
                                             std::shared_ptr<NoteModel> toModel) const {
    auto allEvents = toModel->getAllEvents();
    auto points = fromModel->getAllEvents();

    // Vamp doesn't add current timestamp for note features, so, do it manually
    std::transform(points.begin(), points.end(), points.begin(), [&](const auto& point) {
        return point.withFrame(point.getFrame() + contextStart);
    });

    // Enhanced cleanup strategy: more intelligent overlap-aware removal
    EventVector eventsToRemove;
    EventVector remainingEvents;
    
    // Categorize events based on potential overlap with incoming analysis
    categorizeEvents(allEvents, points, eventsToRemove, remainingEvents);
    
    // Remove potentially overlapping events from the model
    for (const auto& event : eventsToRemove) {
        toModel->remove(event);
    }

    // Create combined event set for overlap detection
    EventVector combinedEvents;
    
    // Add remaining non-overlapping events
    for (const auto& event : remainingEvents) {
        combinedEvents.push_back(event);
    }
    
    // Add previously removed events that might need merging
    for (const auto& event : eventsToRemove) {
        combinedEvents.push_back(event);
    }
    
    // Add new incoming events
    for (const auto& event : points) {
        combinedEvents.push_back(event);
    }

    // Find all overlap groups in the combined event set
    auto overlapGroups = findOverlapGroups(combinedEvents);

    // Process each overlap group
    EventVector finalEvents;
    std::vector<bool> processed(combinedEvents.size(), false);
    
    // Process overlap groups first
    for (const auto& group : overlapGroups) {
        Event mergedEvent = mergeOverlapGroup(group);
        finalEvents.push_back(mergedEvent);
        
        // Mark all events in this group as processed
        for (const auto& groupEvent : group.events) {
            for (size_t i = 0; i < combinedEvents.size(); ++i) {
                if (!processed[i] && 
                    combinedEvents[i].getFrame() == groupEvent.getFrame() &&
                    combinedEvents[i].getDuration() == groupEvent.getDuration()) {
                    processed[i] = true;
                }
            }
        }
    }
    
    // Add non-overlapping events
    for (size_t i = 0; i < combinedEvents.size(); ++i) {
        if (!processed[i]) {
            finalEvents.push_back(combinedEvents[i]);
        }
    }

    // Filter to return only the new/modified events that should be added
    EventVector resultEvents;
    for (const auto& event : finalEvents) {
        bool isNew = false;
        
        // Check if this event is derived from new analysis or is a merge result
        for (const auto& originalNew : points) {
            if (event.getFrame() >= originalNew.getFrame() - m_config.overlapTolerance && 
                event.getFrame() <= originalNew.getFrame() + originalNew.getDuration() + m_config.overlapTolerance) {
                isNew = true;
                break;
            }
        }
        
        // Also include events that are merge results (modified existing events)
        bool isMergeResult = false;
        for (const auto& removedEvent : eventsToRemove) {
            if (event.getFrame() >= removedEvent.getFrame() - m_config.overlapTolerance &&
                event.getFrame() <= removedEvent.getFrame() + removedEvent.getDuration() + m_config.overlapTolerance) {
                isMergeResult = true;
                break;
            }
        }
        
        if (isNew || isMergeResult) {
            resultEvents.push_back(event);
        }
    }

    return resultEvents;
}

// Private helper methods
bool OverlapProcessor::eventsOverlap(const Event& a, const Event& b) const {
    auto aStart = a.getFrame();
    auto aEnd = a.getFrame() + a.getDuration();
    auto bStart = b.getFrame();
    auto bEnd = b.getFrame() + b.getDuration();
    
    // Check for temporal overlap
    return aStart < bEnd && aEnd > bStart;
}

const Event* OverlapProcessor::findLongestEvent(const EventVector& events) const {
    if (events.empty()) return nullptr;
    
    const Event* longestEvent = &events[0];
    for (const auto& event : events) {
        if (event.getDuration() > longestEvent->getDuration()) {
            longestEvent = &event;
        }
    }
    return longestEvent;
}

void OverlapProcessor::categorizeEvents(const EventVector& allEvents, 
                                      const EventVector& newEvents,
                                      EventVector& eventsToRemove,
                                      EventVector& remainingEvents) const {
    // Identify events that might overlap with incoming analysis
    for (const auto& event : allEvents) {
        bool potentialOverlap = false;
        for (const auto& newEvent : newEvents) {
            if (eventsOverlap(event, newEvent)) {
                potentialOverlap = true;
                break;
            }
        }
        
        if (potentialOverlap) {
            eventsToRemove.push_back(event);
        } else {
            remainingEvents.push_back(event);
        }
    }
}
