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

#ifndef TONY_RECORD_SCROLL_H
#define TONY_RECORD_SCROLL_H

#include "base/BaseTypes.h"
#include "base/ZoomLevel.h"

/**
 * Pure helpers for scrolling the view to follow a recording as it is
 * made. These have no dependency on the document, models or UI, so they
 * can be tested directly.
 */
namespace RecordScroll {

/**
 * Where the recording position should sit, as a fraction of the pane
 * width.
 *
 * This must stay comfortably between 1/8 and 7/8. Those are the points
 * at which a pane's own PlaybackScrollPage logic decides to move the
 * view a page at a time -- see the shouldScroll tests in
 * View::movePlayPointer. Keeping the recording position between them
 * means that logic looks at the pointer, finds it already nicely placed,
 * and does nothing. That is what lets us drive the view ourselves
 * without having to disturb the panes' playback follow mode at all, and
 * so without having anything to restore afterwards.
 */
constexpr double defaultAnchor = 0.8;

/**
 * Return the pixel column at which the recording position should sit in
 * a pane of the given width. Returns 0 for a pane too narrow to have
 * one, otherwise a column in [1, paneWidth-1].
 */
int anchorX(int paneWidth, double anchor);

/**
 * Return the centre frame that places recordFrame at the anchor column
 * of a pane of the given width and zoom level.
 *
 * Clamped at zero, so at the start of a recording the position simply
 * walks rightward from the centre of the pane until it reaches the
 * anchor, and the view only begins to scroll once it gets there.
 *
 * The result is computed afresh from recordFrame every time rather than
 * by accumulating an offset, so a zoom change or a window resize part
 * way through a recording corrects itself on the next update.
 */
sv::sv_frame_t centreFrameFor(sv::sv_frame_t recordFrame,
                              int paneWidth,
                              sv::ZoomLevel zoom,
                              double anchor);

}

#endif
