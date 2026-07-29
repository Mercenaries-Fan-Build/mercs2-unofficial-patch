# T1 — the text pass

**Status: tooling ready, nothing shipped.** Both text items were measured rather than assumed, and
each is now blocked on one in-game observation. Neither is blocked on effort.

The rule this whole tier runs on, learned the hard way and recorded in the register:

> **A string in the table is not a string on screen.** With 18,299 keys carrying many near-duplicates
> and dead entries — `Enter vehicle` matched 3 distinct key hashes with identical text — confirm a
> key is actually rendered before shipping a correction to it, or the fix pack ships fixes nobody
> ever sees.

## Tooling — done

`stringdb_patch --allow-resize` now re-lays-out the container and re-splices the block, so
corrections are no longer limited to same-length text. Gated on `--selftest-resize`, which rebuilds
the shipped container substituting nothing and requires the result byte-for-byte.

```sh
cargo run -p wad_builder --bin stringdb_patch -- --source-wad <vz.wad> --out /tmp/x.wad --selftest-resize
# SELFTEST OK: no-op rebuild of 1375560 B is byte-identical
```

Ships as `data/English-patch.wad`, which mounts last in every session and so wins in both the
front-end and gameplay string slots. Proven in-game 2026-07-22.

---

## (a) Console strings on PC — narrowed from 37 to 6

A sweep for `PLAYSTATION` / `Xbox 360` / `Xbox LIVE` / `Gamertag` / `gamer profile` / `Memory Unit` /
`storage device` finds **37** console-platform strings in the PC `english` table — including one
shipped as `Your profile is not Xbox LIVE enabled.  Placeholder Text Only.`

But most are dead. Two independent reachability checks:

**1. Does the executable reference the key hash?** Searched the PC exe for each key hash as a
little-endian dword. **Control first**: 60 randomly-chosen key hashes from the same table → only
**1/60** appears by chance, so a hit is signal rather than noise.

**2. Can any Lua literal produce the hash?** Hashed all **11,446** distinct string literals across
both Lua corpora (and each `[Bracketed.Name]` with its brackets stripped) with `pandemic_hash_m2`.

| | exe-referenced | Lua-reachable |
|---|---|---|
| the 6 below | ✅ | — |
| the other 31 | ✗ | ✗ (0 of 11,446 literals) |

### The 6 candidates — worth confirming in game

All Xbox-LIVE / storage wording on code paths the PC build plausibly shares:

| key | text |
|---|---|
| `0x09CAD2B6` | You cannot play co-op because you do not have a Xbox LIVE® Gold Membership… |
| `0x0AD7E491` | Cannot connect to the EA Servers. Please select an Xbox LIVE enabled gamer profile… |
| `0x453A7339` | The active gamer profile has been changed. |
| `0xD6F8D178` | Connection failed. The date of birth on this Xbox LIVE Account does not allow you… |
| `0xDE14EC44` | Select a storage device |
| `0xE4E6D9D8` | No storage device selected. You will need to select a storage device to save games |

**Next step:** try to make each appear on PC — the two co-op/EA-server ones by failing a
multiplayer connection, the profile and storage ones around save/profile handling. Anything that
renders gets corrected to PC wording. Anything that does not, is not shipped.

### The 31 — recorded, not fixed

Every `PLAYSTATION®Network` / `PLAYSTATION®3 system` / `Xbox 360 console` / `Memory Unit` string,
plus the placeholder. Referenced by neither the exe nor any Lua literal. They confirm the PC text was
branched from a console SKU without a pass — which is a real finding about the build — but correcting
text nothing displays is precisely the trap above.

> ⚠ "Not referenced" is strong, not absolute: a hash could in principle be assembled at runtime or
> come from a data table rather than code. If one of the 31 is ever *seen* on PC, that beats this
> analysis and it should be fixed.

---

## (b) Apostrophe consistency — deferred on one question

Measured across the 18,299 keys:

| | keys | occurrences |
|---|---:|---:|
| U+2019 `’` (curly) | 1,222 | 1,791 |
| U+0027 `'` (plain) | 4,868 | — |
| both in one string | 22 | — |

Also present: U+2018 `‘` (54), U+00A0 non-breaking space (113), U+2013 `–` (14), plus `®™©` and
accented Latin (`ó ñ é í ö ò ¡ ù ì è á`) in Venezuelan names and trademarks.

**The question that decides this: does the game's font render U+2019?**

- If it **does**, this is cosmetic churn across 1,222 strings — 1,222 chances to introduce a defect,
  for no player-visible benefit. **Not worth shipping.**
- If it **does not**, every one of those 1,222 keys currently displays a box or a blank mid-word, and
  this is a genuine, visible bug worth fixing properly.

The accented Latin characters prove the font handles non-ASCII — but `ó`/`ñ` are Latin-1 Supplement
while `’` is General Punctuation, a different block a font can perfectly well omit. So that does
**not** settle it.

**Next step:** look at a string already containing U+2019 in game. No patch is needed to answer this
— 1,222 keys already contain one. Good candidates are ordinary dialogue, e.g.
`Deal. Let’s get started.` or `You don’t mean an Allied plane.`

If it renders: close this item, cosmetic only. If it does not: convert curly → plain (equal length,
one UTF-16 code unit each, so no resize needed), and check `‘` U+2018 and `–` U+2013 the same way.

## Reproducing the analysis

```sh
cargo run -p mercs2_probe --bin stringdb_dump -- --wad <vz.wad> --filter english --out en.tsv
```
then the sweeps above over `en.tsv` (columns: `key_hash`, `source`, `text`). The exe-reference check
searches the canonical build for `struct.pack('<I', key_hash)`; always run the random-key control
alongside it, or a 4-byte pattern match means nothing.
