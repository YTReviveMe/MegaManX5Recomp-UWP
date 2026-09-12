# Original-disc AOT overlays

Mega Man X5 USA (SLUS-01334) packages native code for 53 original-disc
archive images. Every Windows and Linux release freshly extracts, compiles,
audits and stages every configured image, including with `-SkipRegen` or
`--skip-build`. Historical runtime captures and caches are not build inputs.

The declarative [profile](../aot/overlays.json) consumes psxrecomp's reusable
`sector_extent_members` method. Descriptors are little-endian pairs of
archive-relative sector offset and byte size. Payloads are consecutive after
sector alignment. The shared implementation contains no title-ID branches.

## Original loader evidence

The initializer at `0x80015DDC` finds `ROCK_X5.BIN` and reads one header sector.
At `0x80015E78`, its loop adds each descriptor's first word to the file LBA and
copies its byte size; the loop covers 59 entries. Loader `0x80015EB4` selects
an entry by table index and preserves the supplied destination. Sector transfer
`0x80014464` copies payload verbatim, including its leading logical ID, and
rounds the final transfer to four bytes.

| Table indices (decimal) | Destination | Original evidence |
| --- | --- | --- |
| 0-29 | `0x800EE970` | Byte selector table `0x8006FD50`, destination word `0x80010000`, call `0x800136EC` |
| 30-37 | `0x800FA000` | Byte selector table `0x8006FC5C`, call `0x80013568` |
| 40-58 | `0x800FA000` | Byte selector table `0x8006FD80`, call `0x800136B4` |

Four selected members (13, 15, 17, 19) contain resident function-pointer data
and no established callable roots. They are explicitly excluded from code
generation. Members 38 and 39 contain code, but their loader selection and
destination were not established in this bounded pass; they are also explicitly
excluded. The remaining 53 members have static callable roots. A BIOS resident
helper brings the inventory to 54 independently compiled recipes.

The profile pins the original disc SHA-1 and checks the loader and selector
words. The parser validates descriptor count, termination, bounds, and complete
archive coverage after sector alignment. All 59 descriptors remain checked,
including the six exclusions. Original-disc discovery found no additional
supported code producers in `ROCK_X5.DAT`; that is not a proof that all
possible execution sources have been identified.

## Release checks and limits

Each package includes `AOT_CACHE_AUDIT.json` with native-pair hashes. The audit
requires nonempty guarded native coverage for every recipe, the current codegen
ABI and matching original bytes. It does not establish that every indirect
entry or execution path is covered. Interpreter and runtime compilation fallback
remain enabled. Code-changing mods can invalidate stock guards and need fallback;
this release does not claim AOT coverage for every mod combination.

Release smoke checks disable runtime compilation. Gameplay validation remains
a separate user check: opening stage, multiple selectable stages and bosses,
X and Zero, menus, saves, movement, graphics, audio and performance.