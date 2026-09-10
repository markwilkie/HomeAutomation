# RegattaTimeAllowance

Pairwise ORC time-allowance tables: for each race in a series, how much time
every boat owes every other boat in the class.

```
./orc_allowance.py salish-2026-orc0.json --highlight Mayhem
```

Read a cell as *what the row boat owes the column boat at the finish*: `+3:40`
means the row boat has to cross the line 3:40 ahead to tie on corrected time,
`-3:40` means it can cross 3:40 behind and still win the pair.

## Filling in the data file

`salish-2026-orc0.json` holds the Salish Sea 2026 ORC 0 fleet. Set
`"placeholder": true` in a data file you are still filling in and the tool
prints a warning banner over the output until you remove it.

A boat missing the band a race is scored under is dropped from that one table
with a note, rather than failing the run — scratch sheets routinely publish a
single number for a boat whose full Triple Number is not out yet.

Each boat carries whichever ratings its certificate publishes:

| Key | Meaning |
|---|---|
| `tod.<band>` | Time-on-distance allowance, seconds per nautical mile. Lower = faster boat. Bands are whatever the RC scores under (`windward_leeward`, `circular_random`, `long_distance`, an ocean-course wind speed, …). |
| `tot.<band>` | Time-on-time multiplier. Higher = faster boat. ORC publishes `675/GPH` as a single number, plus Triple Number low / medium / high. |

Each race says how the RC is scoring it:

```json
{ "name": "Race 1", "scoring": "ToD", "band": "windward_leeward", "distance_nm": 8.0 }
{ "name": "Race 3", "scoring": "ToT", "band": "medium", "reference_elapsed": "4:30:00" }
```

- **ToD** needs `distance_nm` — the allowance is `(s/mile difference) × distance`,
  so it is fixed before the start once the course length is known.
- **ToT** needs `reference_elapsed`, because the allowance scales with how long
  the race takes. Use your best guess at the elapsed time of the *slower* boat
  in the pair (the tool anchors the reference there, which keeps the table
  symmetric). Leave it out and the table is per hour of elapsed time instead —
  multiply by the hours the race actually runs.

## Scoring the race afterwards

Add finish times to a race and pass `--score`:

```json
"results": [
  { "boat": "Mayhem", "start": "11:00:00", "finish": "12:05:12" },
  { "boat": "Boat 2", "elapsed": "1:06:30" }
]
```

`elapsed` or `start` + `finish`, either way. Corrected time is
`elapsed - allowance × distance` (ToD) or `elapsed × coefficient` (ToT).

## Output

- `--format md` (default) — Markdown, good for pasting into a crew email.
- `--format text` — fixed-width, readable on a phone at the dock.
- `--format csv --out DIR` — one CSV per race, raw seconds.
- `--highlight BOAT` — adds a one-boat summary across all races.
- `--band BAND` — score every race under one band, overriding the data file.
  Use it to see how much the answer depends on the band: `--band low`,
  `--band medium`, `--band high`.

Only the *ratios* between coefficients matter to an allowance, so a set of
coefficients on a different normalisation (ORC's APH single number against the
Triple Number bands, say) still gives correct times — but the two are not
comparable boat-for-boat, and the running order can differ between them.

## What this does not do

ORC's Performance Curve Scoring (the "constructed course" / implied-wind
options) is not implemented. It needs each boat's full polar table off its
certificate plus the wind actually observed, and it deliberately cannot be
reduced to a single owed-time number before the race. If the RC scores the
series under PCS, these tables are an approximation — use the ToD or ToT
numbers from the same certificates as a sanity check, not as the score.
