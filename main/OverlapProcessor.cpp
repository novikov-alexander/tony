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
#include <algorithm>
#include <cmath>

namespace {

bool lessEventForDedup(const Event &a, const Event &b)
{
    if (a.getFrame() != b.getFrame()) return a.getFrame() < b.getFrame();
    if (a.getDuration() != b.getDuration()) return a.getDuration() < b.getDuration();

    if (a.hasValue() != b.hasValue()) return a.hasValue() < b.hasValue();
    if (a.hasValue() && a.getValue() != b.getValue()) return a.getValue() < b.getValue();

    if (a.hasLabel() != b.hasLabel()) return a.hasLabel() < b.hasLabel();
    if (a.hasLabel() && a.getLabel() != b.getLabel()) return a.getLabel() < b.getLabel();

    if (a.hasLevel() != b.hasLevel()) return a.hasLevel() < b.hasLevel();
    if (a.hasLevel() && a.getLevel() != b.getLevel()) return a.getLevel() < b.getLevel();

    return false;
}

bool equalEventForDedup(const Event &a, const Event &b)
{
    return a.getFrame() == b.getFrame() &&
           a.getDuration() == b.getDuration() &&
           a.hasValue() == b.hasValue() &&
           (!a.hasValue() || a.getValue() == b.getValue()) &&
           a.hasLabel() == b.hasLabel() &&
           (!a.hasLabel() || a.getLabel() == b.getLabel()) &&
           a.hasLevel() == b.hasLevel() &&
           (!a.hasLevel() || a.getLevel() == b.getLevel());
}

void sortAndDedupeEvents(EventVector &events)
{
    std::sort(events.begin(), events.end(), lessEventForDedup);
    events.erase(std::unique(events.begin(), events.end(), equalEventForDedup),
                 events.end());
}

EventVector shiftedBy(const EventVector &events, sv_frame_t offset)
{
    EventVector shifted = events;
    std::transform(shifted.begin(), shifted.end(), shifted.begin(),
                   [offset](const auto &event) {
                       return event.withFrame(event.getFrame() + offset);
                   });
    return shifted;
}
}

// OverlapGroup implementation
OverlapGroup::OverlapGroup() : startFrame(0), endFrame(0)
{
}

OverlapGroup::OverlapGroup(size_t index, const Event &event) :
    indices{index},
    startFrame(event.getFrame()),
    endFrame(event.getFrame() + event.getDuration())
{
}

void OverlapGroup::addEvent(size_t index, const Event &event)
{
    if (indices.empty()) {
        startFrame = event.getFrame();
        endFrame = event.getFrame() + event.getDuration();
    } else {
        startFrame = std::min(startFrame, event.getFrame());
        endFrame = std::max(endFrame, event.getFrame() + event.getDuration());
    }

    indices.push_back(index);
}

// OverlapProcessor implementation
OverlapProcessor::OverlapProcessor(const OverlapConfig &config) :
    m_config(config)
{
}

std::vector<OverlapGroup> OverlapProcessor::findOverlapGroups(const EventVector& events) const {
    std::vector<OverlapGroup> groups;
    std::vector<char> processed(events.size(), 0);

    for (size_t i = 0; i < events.size(); ++i) {
        if (processed[i]) continue;

        OverlapGroup currentGroup(i, events[i]);
        processed[i] = true;

        // Find all events that overlap with any event in the current group
        bool foundOverlap;
        do {
            foundOverlap = false;
            for (size_t j = 0; j < events.size(); ++j) {
                if (processed[j]) continue;

                for (size_t groupIndex : currentGroup.indices) {
                    if (eventsOverlap(events[j], events[groupIndex])) {
                        currentGroup.addEvent(j, events[j]);
                        processed[j] = true;
                        foundOverlap = true;
                        break;
                    }
                }
                if (foundOverlap) break;
            }
        } while (foundOverlap);

        if (currentGroup.size() > 1) {
            groups.push_back(currentGroup);
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

    std::vector<float> frequencies;
    std::vector<double> weights;
    frequencies.reserve(overlappingEvents.size());
    weights.reserve(overlappingEvents.size());

    const auto overlapEnd = overlapStart + overlapDuration;

    for (const auto& event : overlappingEvents) {
        const float freq = event.hasValue() ? event.getValue() : 0.0f;
        if (freq <= 0.0f) continue;

        const auto eventStart = event.getFrame();
        const auto eventEnd = event.getFrame() + event.getDuration();

        const auto eventOverlapStart = std::max(eventStart, overlapStart);
        const auto eventOverlapEnd = std::min(eventEnd, overlapEnd);
        const auto eventOverlapContrib = std::max<sv_frame_t>(0, eventOverlapEnd - eventOverlapStart);

        if (eventOverlapContrib > 0) {
            frequencies.push_back(freq);
            weights.push_back(static_cast<double>(eventOverlapContrib));
        }
    }

    if (frequencies.empty()) return 0.0f;
    if (frequencies.size() == 1) return frequencies[0];

    double totalWeight = 0.0;
    for (double weight : weights) totalWeight += weight;
    if (totalWeight <= 0.0) {
        double logSum = 0.0;
        for (float freq : frequencies) {
            logSum += std::log(freq);
        }
        return std::exp(logSum / frequencies.size());
    }

    for (double& weight : weights) weight /= totalWeight;

    double weightedLogSum = 0.0;
    for (size_t i = 0; i < frequencies.size(); ++i) {
        weightedLogSum += std::log(frequencies[i]) * weights[i];
    }

    return std::exp(weightedLogSum);
}

std::optional<Event> OverlapProcessor::mergeOverlapGroup(const OverlapGroup& group, const EventVector& events) const {
    if (group.isEmpty()) {
        return std::nullopt;
    }

    if (group.size() == 1) {
        return events[group.indices[0]];
    }

    const auto mergedStart = group.startFrame;
    const auto mergedEnd = group.endFrame;
    const auto mergedDuration = mergedEnd - mergedStart;

    EventVector overlappingEvents;
    overlappingEvents.reserve(group.indices.size());
    for (size_t index : group.indices) {
        overlappingEvents.push_back(events[index]);
    }

    const float weightedFreq =
        calculateWeightedFrequency(overlappingEvents, mergedStart, mergedDuration);

    const Event* longestEvent = findLongestEvent(group, events);
    if (!longestEvent) {
        return std::nullopt;
    }

    Event mergedEvent = longestEvent->withFrame(mergedStart)
                                   .withDuration(mergedDuration);

    if (weightedFreq > 0.0f) {
        mergedEvent = mergedEvent.withValue(weightedFreq);
    }

    if (longestEvent->hasLabel()) {
        mergedEvent = mergedEvent.withLabel(longestEvent->getLabel());
    }

    if (longestEvent->hasLevel()) {
        mergedEvent = mergedEvent.withLevel(longestEvent->getLevel());
    }

    return mergedEvent;
}

OverlapProcessor::EventPatch OverlapProcessor::processPitchEvents(sv_frame_t contextStart,
                                                                  const EventVector& incomingEvents,
                                                                  const EventVector& existingEvents) const {
    EventPatch patch;
    EventVector shiftedIncoming = shiftedBy(incomingEvents, contextStart);

    EventVector remainingEvents;
    patch.remove.reserve(existingEvents.size());
    remainingEvents.reserve(existingEvents.size());

    for (const auto& event : existingEvents) {
        const auto eventStart = event.getFrame();
        const auto eventEnd = eventStart + event.getDuration();
        if (eventStart >= contextStart || eventEnd > contextStart) {
            patch.remove.push_back(event);
        } else {
            remainingEvents.push_back(event);
        }
    }

    sortAndDedupeEvents(patch.remove);

    patch.add = shiftedIncoming;

    if (!remainingEvents.empty() && !shiftedIncoming.empty()) {
        const auto& lastExistingEvent = remainingEvents.back();
        const auto& firstNewEvent = shiftedIncoming.front();

        const auto lastExistingEnd =
            lastExistingEvent.getFrame() + lastExistingEvent.getDuration();
        const auto gapFrames = firstNewEvent.getFrame() - lastExistingEnd;

        if (gapFrames > 0 && gapFrames <= m_config.interpolationThreshold) {
            if (lastExistingEvent.hasValue() && firstNewEvent.hasValue()) {
                const auto lastValue = lastExistingEvent.getValue();
                const auto firstValue = firstNewEvent.getValue();

                if (lastValue > 0.0f && firstValue > 0.0f) {
                    const auto valueDiff = std::abs(lastValue - firstValue) / lastValue;

                    if (valueDiff <= m_config.pitchSimilarityThreshold) {
                        const auto midFrame = lastExistingEnd + gapFrames / 2;
                        const auto midValue = (lastValue + firstValue) / 2.0f;
                        patch.add.push_back(Event(midFrame, midValue, "interpolated"));
                    }
                }
            }
        }
    }

    sortAndDedupeEvents(patch.add);

    return patch;
}

OverlapProcessor::EventPatch OverlapProcessor::processNoteEvents(sv_frame_t contextStart,
                                                                 const EventVector& incomingEvents,
                                                                 const EventVector& existingEvents) const {
    EventPatch patch;
    EventVector shiftedIncoming = shiftedBy(incomingEvents, contextStart);

    EventVector remainingEvents;
    categorizeEvents(existingEvents, shiftedIncoming, patch.remove, remainingEvents);
    sortAndDedupeEvents(patch.remove);

    EventVector combinedEvents;
    combinedEvents.reserve(remainingEvents.size() + patch.remove.size() + shiftedIncoming.size());
    combinedEvents.insert(combinedEvents.end(), remainingEvents.begin(), remainingEvents.end());
    combinedEvents.insert(combinedEvents.end(), patch.remove.begin(), patch.remove.end());
    combinedEvents.insert(combinedEvents.end(), shiftedIncoming.begin(), shiftedIncoming.end());

    const auto overlapGroups = findOverlapGroups(combinedEvents);

    EventVector finalEvents;
    finalEvents.reserve(combinedEvents.size());
    std::vector<char> processed(combinedEvents.size(), 0);

    for (const auto& group : overlapGroups) {
        if (auto merged = mergeOverlapGroup(group, combinedEvents)) {
            finalEvents.push_back(*merged);
        }

        for (size_t index : group.indices) {
            if (index < processed.size()) {
                processed[index] = true;
            }
        }
    }

    for (size_t i = 0; i < combinedEvents.size(); ++i) {
        if (!processed[i]) {
            finalEvents.push_back(combinedEvents[i]);
        }
    }

    patch.add.reserve(finalEvents.size());
    for (const auto& event : finalEvents) {
        bool include = false;

        for (const auto& originalNew : shiftedIncoming) {
            if (eventsOverlap(event, originalNew)) {
                include = true;
                break;
            }
        }

        if (!include) {
            for (const auto& removedEvent : patch.remove) {
                if (eventsOverlap(event, removedEvent)) {
                    include = true;
                    break;
                }
            }
        }

        if (include) {
            patch.add.push_back(event);
        }
    }

    sortAndDedupeEvents(patch.add);

    return patch;
}

// Private helper methods
bool OverlapProcessor::eventsOverlap(const Event& a, const Event& b) const {
    const auto aStart = a.getFrame();
    const auto aEnd = a.getFrame() + a.getDuration();
    const auto bStart = b.getFrame();
    const auto bEnd = b.getFrame() + b.getDuration();

    // Intentionally strict overlap test: both grouping and patch inclusion
    // only consider events that truly overlap in time.
    return aStart < bEnd && aEnd > bStart;
}

const Event* OverlapProcessor::findLongestEvent(const OverlapGroup& group, const EventVector& events) const {
    if (group.indices.empty()) return nullptr;

    const Event* longestEvent = &events[group.indices[0]];
    for (size_t index : group.indices) {
        const Event &event = events[index];
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
    eventsToRemove.reserve(allEvents.size());
    remainingEvents.reserve(allEvents.size());

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
