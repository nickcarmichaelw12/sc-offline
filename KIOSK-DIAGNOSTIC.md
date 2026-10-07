# Kiosk diagnostic patch 1

This is modified source, not a compiled DLL and not a complete kiosk/hangar/inventory fix.
Version: 0.3.0-kiosk-diag1. Based on the user's uploaded sc-offline 0.3.0 source.

## Changes

- Log every entitlement-response callback up to 20 calls, distinguishing unreadable, failed, successful-empty, populated and malformed range responses.
- Reuse the existing offline catalogue fallback for both returned failures and returned empty lists. Preserve populated replies and pass through unreadable/malformed replies.
- Retry catalogue construction on a later qualifying response after 3 seconds if engine classes were not ready. A zero-item first attempt is no longer cached for the entire process.
- Log entitlement/fallback/retrieval callback totals every 15 seconds, up to 40 samples. These are cumulative process totals, not kiosk-specific request timing.
- Mark the diagnostic version distinctly. Include portable response-policy tests.
- Replace the copy's CI workflow with a Windows build triggered by pushes to codex/kiosk-diag1 or manually. It neither publishes a release nor deploys anything. Existing project/toolset and action pins are retained from the supplied source.

## What this does not do

This still hooks responses, not query initiation. If the original request never completes, this patch will not invent a callback or end the search. Zero response callbacks while the kiosk remains searching is evidence to investigate the request path, not proof of which service call is stuck.

Hangar allocation, elevator destinations, persistent ship ownership, inventory storage, and hangar retrieval are not implemented here. The fallback catalogue lists spawnable classes from ships.txt; it is not a persistent list of player-owned ships. The successful-response vector layout uses the layout already constructed by upstream's fallback but still requires in-game validation.

## Validation

Passed the portable C++ policy test using g++ with C++20 and warnings as errors: unreadable, failed, empty, populated, null-start, reversed, misaligned and overflow-like ranges. This validates decision logic only, not game-memory layout or MSVC compatibility.

The repository's full check could not run: clang++ is not installed here. No Windows compiler, Windows SDK, MSBuild or authenticated GitHub build connection was available when this package was prepared. The DLL has not been compiled or run in-game. No claim of a successful build is made.

## Build and test handoff

Once an authenticated GitHub build connection and a repository owned by the user are available, push this source to codex/kiosk-diag1 to run Kiosk diagnostic build. The workflow retains the upstream Windows runner and v145 toolset requirements; runner/tool availability must be confirmed by that build. Retrieve kiosk-diagnostic-binaries only after the build succeeds.

For testing, use a separate copy of the working mod folder and retain the original DLL and player data. The test build's log must say 0.3.0-kiosk-diag1. Open one kiosk, wait 45 seconds, close it and reopen once. Inspect mod.log for entitlement callback categories and fleet-diag totals. Do not treat hook-install messages as response completion. Do not replace a working binary with source files from this archive.
