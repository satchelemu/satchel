#pragma once
#include <CoreGraphics/CoreGraphics.h>
#include <math.h>
#include <stdbool.h>

// A single rectangle defines both presentation and the inverse touch transform.
// aspect is the presented picture's width / height: 16:9 unless the game presents another
// shape (nxnative front_picture_shape, upstream 600c0b37; a 1.43:1 picture under
// DM_SCREEN_ASPECT). Invalid shapes fall back to 16:9.
static inline CGRect SatchelPictureRectAspect(CGRect bounds, int mode, CGFloat aspect) {
    CGFloat w = bounds.size.width, h = bounds.size.height;
    if (w <= 0 || h <= 0 || mode == 1) return bounds;
    if (!(aspect >= 1.0 && aspect <= 3.0)) aspect = 16.0 / 9.0;
    int wide = w / h > aspect;
    if (wide) w = h * aspect;
    else h = aspect == 16.0 / 9.0 ? w * 9.0 / 16.0 : w / aspect;
    return CGRectMake(bounds.origin.x + floor((bounds.size.width - w) / 2),
                      bounds.origin.y + floor((bounds.size.height - h) / 2), w, h);
}
static inline CGRect SatchelPictureRect(CGRect bounds, int mode) {
    return SatchelPictureRectAspect(bounds, mode, 16.0 / 9.0);
}
static inline CGPoint SatchelPictureTouch(CGPoint point, CGRect picture) {
    if (picture.size.width <= 0 || picture.size.height <= 0) return CGPointZero;
    return CGPointMake(fmin(fmax((point.x - picture.origin.x) / picture.size.width, 0), 0.999),
                       fmin(fmax((point.y - picture.origin.y) / picture.size.height, 0), 0.999));
}

// Half-fold places the picture above the controls, independent of landscape rotation.
static inline CGRect SatchelFoldRegion(CGRect bounds, int half, int controls) {
    if (!half) return bounds;
    CGFloat height = floor(bounds.size.height / 2);
    return CGRectMake(bounds.origin.x, bounds.origin.y + (controls ? height : 0), bounds.size.width,
                      controls ? bounds.size.height - height : height);
}
// Project a real occlusion onto its nearest outer edge. The SDK frame includes
// interactive margins already. Empty and offscreen regions reserve no space.
static inline CGRect SatchelOcclusionSafeRect(CGRect safe, CGRect full, CGRect occlusion) {
    CGRect hit = CGRectIntersection(full, occlusion);
    if (CGRectIsNull(hit) || CGRectIsEmpty(hit) || !CGRectIntersectsRect(safe, hit)) return safe;
    CGFloat distances[] = {CGRectGetMinX(hit) - CGRectGetMinX(full), CGRectGetMaxX(full) - CGRectGetMaxX(hit),
                           CGRectGetMinY(hit) - CGRectGetMinY(full), CGRectGetMaxY(full) - CGRectGetMaxY(hit)};
    int edge = 0;
    for (int i = 1; i < 4; i++) if (distances[i] < distances[edge]) edge = i;
    CGFloat x = CGRectGetMinX(safe), y = CGRectGetMinY(safe), right = CGRectGetMaxX(safe), bottom = CGRectGetMaxY(safe);
    if (edge == 0) x = fmax(x, CGRectGetMaxX(hit));
    if (edge == 1) right = fmin(right, CGRectGetMinX(hit));
    if (edge == 2) y = fmax(y, CGRectGetMaxY(hit));
    if (edge == 3) bottom = fmin(bottom, CGRectGetMinY(hit));
    return CGRectMake(x, y, fmax(0, right - x), fmax(0, bottom - y));
}

// Which edge of the phone is up while a half-folded game plays upright: its bottom edge
// (true) or its top edge (false). `device` is the phone's reported orientation
// (UIDeviceOrientation: 1 upright, 2 upside down, 5 lying face up). The motion sensor sits in
// the phone's top half, so a phone that reports lying face up at the moment the game turns
// has that half flat as the base, and the picture belongs on the other half (seen on the
// maintainer's phone, 2026-11-05). After the turn only an upright report changes the answer:
// a screen half that leans far back also reports face up.
static inline bool SatchelUprightTopDown(int device, bool entering, bool last) {
    if (device == 1) return false;
    if (device == 2) return true;
    return entering ? device == 5 : last;
}
