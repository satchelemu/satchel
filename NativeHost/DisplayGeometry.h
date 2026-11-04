#pragma once
#include <CoreGraphics/CoreGraphics.h>
#include <math.h>

// A single rectangle defines both presentation and the inverse touch transform.
static inline CGRect SatchelPictureRect(CGRect bounds, int mode) {
    CGFloat w = bounds.size.width, h = bounds.size.height;
    if (w <= 0 || h <= 0 || mode == 1) return bounds;
    int wide = w / h > 16.0 / 9.0;
    if (wide) w = h * 16.0 / 9.0;
    else h = w * 9.0 / 16.0;
    return CGRectMake(bounds.origin.x + floor((bounds.size.width - w) / 2),
                      bounds.origin.y + floor((bounds.size.height - h) / 2), w, h);
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
