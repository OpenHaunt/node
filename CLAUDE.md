# Working on this repo

Notes on tooling and workflow. Project decisions live in `docs/`, especially
`docs/10-design-decisions.md` — read that before reopening a settled question.

## Part-DB is the source of truth for parts

**Always search Part-DB before searching a distributor.** Every part used in a schematic must exist
in Part-DB so inventory and BOM work. Looking up a part at DigiKey first and proposing it wastes
time and creates duplicates of things already stocked.

Order of operations for any part:

1. Search Part-DB by value *and* by alternative unit spellings
2. Only if genuinely absent, search DigiKey for a replacement
3. Report what was found in Part-DB versus what is new

### Part-DB has two APIs

Credentials in `.env` (gitignored): `PARTDB_URL`, `PARTDB_TOKEN`.

```bash
B=https://parts.hive.service42.me
# REST API - full part records, pagination
curl -H "Authorization: Bearer $PARTDB_TOKEN" "$B/api/parts?itemsPerPage=100&page=1"
# KiCad HTTP API - what KiCad actually receives
curl -H "Authorization: Bearer $PARTDB_TOKEN" "$B/en/kicad-api/v1/parts/<id>.json"
curl -H "Authorization: Bearer $PARTDB_TOKEN" "$B/en/kicad-api/v1/categories.json"
```

`/api/parts` returns `eda_info: []` even when EDA fields are set. **Use the kicad-api endpoint** to
see the real symbol, footprint, reference prefix and value.

### Unit spellings differ and will defeat naive searches

Part-DB descriptions use whatever the distributor import produced. The same capacitance appears as
`22 nF` or `0.022 µF` — with a **micro sign**, not `u`. Resistances appear as `Ohms`, `kOhms`,
`MOhms`. Values in the KiCad `value` field use yet another convention: `0R`, `150R`, `49R9`, `1M`,
`10K`, but also bare `10` and `110` on a few older entries.

**Normalize numerically when searching**, or search several spellings. A regex for `22 ?nF` misses
`0.022 µF` and vice versa. This has already caused one duplicate lookup.

### Before creating a new Part-DB entry, copy the conventions from a neighbor

Query an existing part of the same kind through the kicad-api and reuse its prefix, value style, symbol and
footprint. Guessing produces entries that look fine and then sort differently, search differently, or place
the wrong footprint.

Conventions observed in this instance:

| | Reference | Value |
|---|---|---|
| Passives | `R`, `C`, `L` | **short form**: `0R`, `49R9`, `15p`, `6n8`, `1M` |
| ICs, modules, crystals | `U`, `Y` | **full MPN** |

`exclude_from_bom`, `exclude_from_board` and `exclude_from_sim` are `False` on every part; visibility stays
on automatic.

A few older entries deviate (`10` and `110` instead of `10R`/`110R`, one crystal on `X` instead of `Y`).
Follow the majority, and mention the outlier rather than copying it.

### Check the KiCad standard libraries before building an LRV symbol

`ESP32-S3-WROOM-1`, `W5500`, `Crystal_GND24` and their footprints all exist in the stock libraries. A
custom symbol is only worth it when nothing standard fits.

```bash
K="/Applications/KiCad/KiCad.app/Contents/SharedSupport"
grep -oE '\(symbol "<name>[^"]*"' "$K/symbols/<Lib>.kicad_sym"
ls "$K/footprints/<Lib>.pretty/" | grep -i <pattern>
```

## DigiKey API

Credentials in `.env`: `DIGIKEY_CLIENT_ID`, `DIGIKEY_CLIENT_SECRET`.

**The OAuth token expires after 10 minutes.** Long analysis sessions hit 401 mid-run. Re-request the
token at the start of each batch rather than reusing a cached one.

```bash
curl -s -X POST https://api.digikey.com/v1/oauth2/token \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "client_id=$DIGIKEY_CLIENT_ID&client_secret=$DIGIKEY_CLIENT_SECRET&grant_type=client_credentials"
```

### Parameter value formats are inconsistent

Filtering on exact strings fails silently and returns zero results, which looks like "part does not
exist" rather than "filter is wrong".

| Field | Actual value |
|---|---|
| Frequency | `25 MHz` — **with a space** |
| Load Capacitance | `18pF` — **without a space** |
| Capacitance | `0.022 µF` or `22 nF` depending on the part |
| Package / Case | `0402 (1005 Metric)` — substring match, not equality |

When a filtered search returns nothing, **print the raw parameters of a few unfiltered hits** before
concluding the part is unavailable.

### Keyword search misses parts that exist

`/products/v4/search/keyword` does not reliably find parts by partial part number. Würth's
right-angle sockets (`6130xx243121`) returned nothing for several sensible queries but resolve fine
via `/products/v4/search/<mpn>/productdetails`. **When a family is suspected to exist, probe part
numbers directly** rather than trusting a keyword search to enumerate it.

## KiCad

The project is saved in the **KiCad 10.0 file format** (downgraded from a 10.99 nightly on
2026-09-07). `/Applications/KiCad/` holds 10.0.6 and opens it. The nightly opens it too, but
**saving from the nightly re-upgrades the format** (footprint `transform` blocks instead of `at`,
stackup `dielectric_model`, geometric `constraint` blocks, bold stroke semantics), after which 10.0.6
reports only "Failed to load schematic". The same applies to footprints in the LRV library: one file
in nightly format makes KiCad 10 refuse the whole library.

```bash
export LRV_CUSTOM_LIBS=/Users/hannes/source/ffp/kicad-custom-libs
K="/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
"$K" sch export netlist --format kicadsexpr -o /tmp/net.net <project>.kicad_sch
```

`LRV_CUSTOM_LIBS` must be exported or library lookups fail.

### GND pour and via coverage

`hardware/tools/gnd_reach/gnd_reach.sh <board.kicad_pcb>` renders how far each point of the GND copper is
from the nearest GND via (through the copper), marks thin pour sections and lists SMD GND pads without a
nearby via. See its README. It only reads the board, so save in KiCad first.

### Reviewing a schematic

Export the netlist and parse it. That is more precise than reading the `.kicad_sch` directly and it
catches the errors that matter: unconnected pins without NC flags, signals on the wrong pin, symbols
not sourced from Part-DB.

Useful checks:

- **Nets with exactly one node.** A label placed on one side but never on the other produces a net with a
  single pin. This is the most common way a signal ends up unconnected while the schematic looks finished,
  and it does not show up as an unconnected pin.
- Nets named `unconnected-(...)` versus the count of `(no_connect` in the schematic. A mismatch means
  a pin is open **by accident**.
- `(lib_id "...")` per symbol — anything not `Part-DB:` or `LRV:` is a stock symbol that should
  probably be replaced.
- Pin numbers against the datasheet. Symbol pin *names* can be misleading: on the LAN transformer,
  `TCT2` is pin 4 and `TCT4` is pin 10, and they have been swapped once already.

### Do not grep for your own output format

A check for `Y1\.[0-9]` in a netlist found nothing and produced a wrong "the crystal is unconnected"
finding. `Y1.1` was the format of the *formatted output*; the netlist itself stores
`(ref "Y1") (pin "1")`.

**Grep the source format, or better, reuse the parser** that produced the earlier answer. Two wrong
findings in this project came from checking against a rendering rather than the data.

### DNP and property parsing

**`(dnp yes)` appears before the property block of a symbol, not after.** A regex that starts at
`(property "Reference"` and scans forward picks up the *next* symbol's flag and reports DNP against
the wrong part. This produced three wrong findings in one review.

Parse symbol blocks by **paren matching** from `(symbol` and check for `(dnp yes)` at symbol
indentation level:

```python
import re
for m in re.finditer(r'\(symbol\s*\n', s):
    a = m.start(); d = 0
    for b in range(a, len(s)):
        if s[b] == '(': d += 1
        elif s[b] == ')':
            d -= 1
            if d == 0: b += 1; break
    blk = s[a:b]
    if '(lib_id' not in blk: continue
    dnp = re.search(r'\n\t\t\(dnp yes\)', blk) is not None
```

## Libraries

`LRV_CUSTOM_LIBS` → `/Users/hannes/source/ffp/kicad-custom-libs` (separate repo).

- Symbols: `symbol/LRV.kicad_sym`, footprints: `footprints/LRV/`, 3D: `3d/`
- 3D models are referenced as `${LRV_CUSTOM_LIBS}/3d/<name>.stp` so the repo stays relocatable
- Naming follows `<vendor or series>_<MPN>`: `WE-LAN_7490220122`, `Neutrik_NE8FAH-LR-DAE`,
  `WR-MJ_634008149821`

### Importing a ComponentSearchEngine part

componentsearchengine.com returns **403 to automated requests** — the user must download the ZIP.
The KiCad files inside need work before they belong in LRV:

- Footprints are legacy `(module ...)` format → `kicad-cli fp upgrade <dir>.pretty --force`
- Symbols are old versions → `kicad-cli sym upgrade <file> --force`
- Rewrite the 3D model path to `${LRV_CUSTOM_LIBS}/...`
- Rename symbol, its `_0_1`/`_1_1` sub-symbols, `Value` and `Footprint` to the LRV convention
- **Check the metadata**: CSE ships typos (a `Height` of `98.8` for an 8.8 mm part) and drops
  umlauts from manufacturer names

## Datasheets

`poppler` is installed: `pdftotext -layout`, `pdftoppm -r 200 -png`, `pdfinfo`.

- **`pdftotext` alone is often not enough.** Schematics and mechanical drawings are vector graphics;
  the text extract gives scattered numbers with no geometry. Render to PNG and read the image.
- **Some "PDFs" are PNGs.** Neutrik's CDN serves `...Datasheet.pdf?format=pdf` as a PNG of page 1
  only. Drop the query string to get the real multi-page PDF.
- Distributor parametric data is not authoritative. A Halo transformer's DC bias figure existed only
  in DigiKey's parameters, not in the datasheet, and the datasheet contradicted the conclusion drawn
  from it. **When a number decides a design, read the datasheet.**
- Manufacturer reference schematics and their own datasheets can disagree. WIZnet's W5500 reference
  shows 18 pF load capacitors while the datasheet specifies CL = 18 pF, which needs about 27 pF.
  Say so rather than picking one silently.

## Git

**Do not commit or stage unless asked.** `git add -A` has previously swept in the user's in-progress
KiCad files and an unrelated `.gitignore` edit. Stage explicitly, or leave the tree alone.

The KiCad project may be open while working — `~*.lck` files mean an editor holds it.

## Reporting review findings

Three of the findings reported in this project were wrong, and all three had the same cause: a check that
looked right but tested the wrong thing. The user caught each one.

- **Verify before reporting.** If a finding depends on parsing, print the raw evidence for one case and
  confirm the parse matches what the file actually contains.
- **When the user pushes back, re-check rather than defend.** "Doch, die sind richtig" and "eigentlich
  stimmt DNP bei mir schon" were both correct, and in both cases the parsing was at fault.
- **Correct plainly and move on.** Say which finding was wrong and why, then continue. No extended apology.

## Design guidance

- **Reference designs and datasheets can disagree.** Say so and give both numbers rather than silently
  picking one. WIZnet's W5500 reference shows 18 pF crystal load capacitors while its own datasheet
  specifies CL = 18 pF, which needs about 27 pF.
- **A decision recorded in `docs/10-design-decisions.md` can be invalidated by a later one** without anyone
  noticing. D9 (external flash) existed because the ESP32 had hard real-time work; D19 moved that work to
  the RP2354 and quietly removed the reason. When revisiting a decision, check what has changed underneath
  it.
- **Superseded decisions are amended, not deleted.** The original reasoning usually explains why the
  replacement looks the way it does, and D3's broadcast argument stayed true even after being overruled.
