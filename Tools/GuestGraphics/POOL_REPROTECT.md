# Focused pool permission regression

Run from the repository root:

```sh
zsh scripts/test_pool_reprotect.sh
```

This rebuilds only `GuestPools.c`, `VulkanBackend.cpp` and the original test under ASan/UBSan, then runs that one real-Vulkan integration gate. It does not run the broad renderer suite, alter production sources or promote artifacts. Outputs default to `/tmp/satchel-pool-reprotect-build`; use `--output` for another task-owned scratch directory.

The preserved original fixture has196,700 assertions, mostly byte comparisons. It tests retained pool leases across RW/R/NONE splits, physical overlap through permission views and a disjoint locked alias, refused resolve/lease/flush/invalidate without GPU mirror or CPU guard mutation, restoredRW cache copies and stable generations/budgets. It then changes permissions with a real GPU job pending, destroys the CPU, verifies retained ownership through cancellation, and finishes CPU-free pool/device cleanup with zero resources.

The installed MoltenVK1.4.2 macOS archive and every consumed package header/library are checked against the official package checksum already used by this project. No download or new dependency is added. The CPU archive is bound to the exact reviewed reprotection production sources/header through `PoolReprotectCheckpoint.json`. This file also preserves the original test hash and first matched backend/object evidence. A later CPU build requires a reviewed matching checkpoint via `--cpu-checkpoint` and optional `--cpu-library`; changing the checkpoint is an explicit integration review, not permission to skip assertions. Rebuilt graphics inputs and toolchain are hashed before/after each run and recorded with object/executable hashes in `validation-manifest.json`.

For validating a staged recipe against the repository's unchanged production files, `--source-root /absolute/repository/path` is supported. The test source always belongs to this recipe. The existing large raster source lock and build recipe are unchanged. This is host compatibility evidence, not a new phone graphics or game-frame claim.
