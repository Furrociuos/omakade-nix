# Regional identity fixtures

Read-only IGDB responses captured September 8, 2026 using GameMetadata::aliasSearchQuery.
Only identity and release facts are retained. These are provider assertions, not an independent
historical audit. Refresh deliberately when catalog evidence changes.

- FF3 SNES: regional numbering, one catalog ID, different platform/territory dates.
- FF3 NES/Famicom: a distinct game despite the same English title.
- FF2 SNES: two exact title/alias candidates; requires identification.
- Starwing: an alternative name resolves to Star Fox on SNES.
- Paperboy NES: port identity refresh must retain an existing portrait.

Sources: https://api-docs.igdb.com/#game-localization and
https://api-docs.igdb.com/#release-date.
