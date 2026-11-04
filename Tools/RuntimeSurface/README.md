# Runtime display attachment

The real UIKit canvas publishes a locked Foundation snapshot only while attached
to an active scene with nonzero dimensions. A new attachment receives a fresh
epoch; resize preserves the current epoch. Detach, backgrounding and dismantling
invalidate it. Snapshot validity is instantaneous and is not a rendering lease.
The canvas currently displays a waiting label and accepts no game frames.

Run `zsh scripts/test_runtime_surface.sh --output /tmp/fresh-surface-gate`.
The original lifecycle/exhaustion/concurrent-reader tests use ThreadSanitizer;
the UIKit/SwiftUI source also compiles for iOS17 with Swift6 strict concurrency.
These checks do not establish physical view attachment or game rendering.
