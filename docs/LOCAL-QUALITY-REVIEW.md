# Local quality review, September 8, 2026

Reviewed the pending changes against installed cover candidate f49b390 (1.7.1-3).
Everything remains local. No pushes, tags, releases, or remote writes.

## Changes reviewed and corrected

- Preserve downloaded portraits when source artwork arrives; expose missing RetroArch cover files as missing artwork. These fixes are already installed separately.
- Prioritize the selected game ahead of visible and background metadata requests. Older payloads can acquire descriptions and artwork without waiting for the normal refresh cycle.
- Explicit refresh bypasses the in-memory provider response cache. Retry is disabled when IGDB is not connected.
- Put game information above organization controls. Correct controller links to follow the new order, with a fallback when organization controls are hidden.
- Use provider image IDs to construct background URLs. Retain cover artwork as a fallback while the background is unavailable, and reduce background opacity for text readability.
- Keep the real-data render fixture consistent across the information and rating sections.

## Validation

- 130/130 CTest checks pass in private configuration, data, cache, runtime, and temporary directories. Tests do not contact the installed app instance.
- Regressions cover selected-game priority against another visible game, explicit refresh cache eviction, source-cover preservation, and missing cover files.
- The captured Mario Odyssey IGDB response and local artwork render successfully. This is an offscreen fixture, not acceptance of the live installed UI.
- Evidence: build/odyssey-audit/review-checks.log, review-render.log, and review-odyssey.png.

## Remaining work

- Regional title identification is not implemented. Fetch and preserve IGDB aliases, localizations, and platform/region release information before changing match decisions.
- The existing matcher still resolves some same-title candidates by rating count. That requires a separate identity review; this candidate does not claim to fix it.
- No title-specific Final Fantasy rule is present.
- Background selection currently takes the first usable provider artwork, then a screenshot. Further image selection and visual polish remain possible.
- The richer detail-page candidate has not replaced the installed cover-only candidate.
