# Runtime frame consumer

Run `python3 Tools/RuntimeFrame/build.py --output /tmp/satchel-runtime-frame-check` on Apple Silicon with Xcode installed. Use a new output directory. This uses the installed CoreGraphics, Combine and UIKit APIs; it adds no dependency.

The host ASan and ThreadSanitizer runs check an original two-pixel image through real CoreGraphics drawing, channel order, immutable copies, copied-backing limits, invalid metadata, detach during copy, epoch rejection, concurrent mailbox ordering and deferred observable invalidation. The recipe also compiles the actual UIKit/SwiftUI consumer for iOS17 with strict Swift6 concurrency checks. It does not validate game graphics or actual UIKit presentation.

The consumer copies a currently leased C frame synchronously on the graphics worker. It accepts only explicitly described linear-sRGB, top-left RGBA8 data, and uses an opaque host canvas that ignores alpha. These are host display constraints, not inferred game/NVN defaults. The caller must release the C lease on the graphics worker even when a copy fails, retaining ownership for retry if release refuses.

Owned copied backing is limited to8MiB per frame and24MiB globally. CoreGraphics/compositor/driver allocations are outside that counter. A CGDataProvider retains the backing for every live image reference. The mailbox retains at most one pending image; the main thread discards stale epochs and sequences. UIKit checks the real active attachment before display and clears its image immediately on detach. Observable invalidation is deferred outside representable updates and checks the epoch again.

The current consumer has no synthetic producer in the game UI. Rendering requires the execution/Window adapter to provide an actual completed frame; the waiting label remains until one arrives.
