/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Tony
    An intonation analysis and annotation tool
    Centre for Digital Music, Queen Mary, University of London.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#ifndef TEST_RECORD_SCROLL_H
#define TEST_RECORD_SCROLL_H

#include "../RecordScroll.h"

#include <QObject>
#include <QtTest>

using namespace sv;

class TestRecordScroll : public QObject
{
    Q_OBJECT

private:
    /** Where a pane would actually draw the given frame, mirroring
     *  View::getXForFrame: the centre frame is quantised down to a zoom
     *  level boundary, and the offset from it is measured in pixels. */
    static int viewX(sv_frame_t frame, int paneWidth,
                     ZoomLevel zoom, sv_frame_t centreFrame) {
        sv_frame_t rounded = centreFrame;
        if (zoom.zone == ZoomLevel::FramesPerPixel) {
            rounded = (centreFrame / zoom.level) * zoom.level;
        }
        double pixels = zoom.framesToPixels(double(frame - rounded));
        return int(pixels) + paneWidth / 2;
    }

    static ZoomLevel fpp(int level) {
        return ZoomLevel(ZoomLevel::FramesPerPixel, level);
    }

private slots:

    void anchorIsBelowThePageThreshold() {

        // Load-bearing. We drive the view ourselves but leave the panes
        // in their usual PlaybackScrollPage mode, which is only safe
        // because the anchor sits inside the band where that mode
        // decides no scrolling is needed: it pages when the position
        // goes above 7/8 of the width, or below 1/8. If anyone raises
        // defaultAnchor past 7/8 the panes start fighting us, so fail
        // here rather than in the field.
        for (int w = 8; w <= 2000; ++w) {
            int x = RecordScroll::anchorX(w, RecordScroll::defaultAnchor);
            QVERIFY2(x <= (w * 7) / 8,
                     QString("anchor %1 exceeds page threshold at width %2")
                     .arg(x).arg(w).toLocal8Bit().data());
            QVERIFY2(x > w / 8,
                     QString("anchor %1 below page threshold at width %2")
                     .arg(x).arg(w).toLocal8Bit().data());
        }
    }

    void positionLandsExactlyOnTheAnchor() {

        // The whole point: the recording position must sit on the anchor
        // pixel, not near it, or it would creep across the pane over the
        // course of a take.
        const QVector<int> widths { 200, 640, 641, 1920 };
        const QVector<int> levels { 1, 256, 512, 1024, 4096 };
        const QVector<sv_frame_t> frames { 44100, 44100 * 37 + 13, 1000000007 };

        int checked = 0;

        for (int w: widths) {
            for (int l: levels) {
                for (sv_frame_t f: frames) {

                    sv_frame_t centre = RecordScroll::centreFrameFor
                        (f, w, fpp(l), RecordScroll::defaultAnchor);

                    // Only once the recording is long enough to fill the
                    // pane up to the anchor. Before that the centre is
                    // clamped at the start of the audio and the position
                    // is still walking toward the anchor -- that regime
                    // is checked by the next test.
                    if (centre == 0) continue;

                    QCOMPARE(viewX(f, w, fpp(l), centre),
                             RecordScroll::anchorX(w, RecordScroll::defaultAnchor));
                    ++checked;
                }
            }
        }

        QVERIFY2(checked > 40, "too few cases exercised the unclamped path");
    }

    void viewHoldsStillUntilThePositionReachesTheAnchor() {

        // At the very start of a recording the anchor would imply a
        // centre frame before the beginning of the audio. Rather than
        // scrolling into empty space, the view stays put and the
        // position walks across it until it arrives at the anchor.
        const int w = 800;
        const ZoomLevel z = fpp(512);
        const int anchor = RecordScroll::anchorX(w, RecordScroll::defaultAnchor);

        int previousX = -1;

        for (sv_frame_t f = 0; f < sv_frame_t(w) * 512; f += 512) {

            sv_frame_t centre = RecordScroll::centreFrameFor
                (f, w, z, RecordScroll::defaultAnchor);

            QVERIFY(centre >= 0);

            int x = viewX(f, w, z, centre);
            QVERIFY(x >= previousX);          // only ever moves rightward
            QVERIFY(x <= anchor);             // and never past the anchor
            previousX = x;
        }

        QCOMPARE(previousX, anchor);          // and does get there
    }

    void viewNeverScrollsBackwards() {

        const int w = 640;
        const ZoomLevel z = fpp(256);

        sv_frame_t previous = -1;

        for (sv_frame_t f = 0; f < 44100 * 20; f += 4410) {
            sv_frame_t centre = RecordScroll::centreFrameFor
                (f, w, z, RecordScroll::defaultAnchor);
            QVERIFY(centre >= previous);
            previous = centre;
        }
    }

    void zoomChangeKeepsTheAnchor() {

        // Regression guard: the offset must be recomputed from the zoom
        // level every time, never cached, or zooming mid-recording would
        // leave the position stranded away from the anchor.
        const int w = 900;
        const sv_frame_t f = 44100 * 60;

        for (int l: { 256, 4096 }) {
            sv_frame_t centre = RecordScroll::centreFrameFor
                (f, w, fpp(l), RecordScroll::defaultAnchor);
            QCOMPARE(viewX(f, w, fpp(l), centre),
                     RecordScroll::anchorX(w, RecordScroll::defaultAnchor));
        }
    }

    void degenerateGeometryIsHarmless() {

        QCOMPARE(RecordScroll::anchorX(0, RecordScroll::defaultAnchor), 0);
        QCOMPARE(RecordScroll::anchorX(1, RecordScroll::defaultAnchor), 0);

        // A pane with no width yet must not move the view at all
        QCOMPARE(RecordScroll::centreFrameFor(44100, 0, fpp(256), 0.8),
                 sv_frame_t(44100));
        QCOMPARE(RecordScroll::centreFrameFor(44100, 1, fpp(256), 0.8),
                 sv_frame_t(44100));

        // Extreme anchors stay inside the pane
        for (int w: { 2, 3, 100, 1000 }) {
            QVERIFY(RecordScroll::anchorX(w, 0.0) >= 1);
            QVERIFY(RecordScroll::anchorX(w, 1.0) <= w - 1);
        }
    }
};

#endif
