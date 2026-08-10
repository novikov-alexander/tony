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

#include "RecordScroll.h"

#include <cmath>

using namespace sv;

namespace RecordScroll {

int
anchorX(int paneWidth, double anchor)
{
    if (paneWidth < 2) return 0;

    int x = int(round(anchor * paneWidth));

    if (x < 1) x = 1;
    if (x > paneWidth - 1) x = paneWidth - 1;

    return x;
}

sv_frame_t
centreFrameFor(sv_frame_t recordFrame,
               int paneWidth,
               ZoomLevel zoom,
               double anchor)
{
    if (paneWidth < 2) return recordFrame;

    // A pane draws its centre frame at its middle pixel, so to put
    // recordFrame at the anchor instead we ask for a centre that many
    // pixels earlier. Integer division here, to match the width()/2 the
    // pane itself uses.
    int d = anchorX(paneWidth, anchor) - paneWidth / 2;

    sv_frame_t offset = sv_frame_t(llround(zoom.pixelsToFrames(double(d))));
    sv_frame_t centre = recordFrame - offset;

    if (centre < 0) centre = 0;

    return centre;
}

}
