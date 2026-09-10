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

`salish-2026-orc0.json` is a template and **every rating in it is a
placeholder** — the tool prints a warning banner until you delete
`"placeholder": true`. Replace the numbers with the class scratch sheet or the
boats' ORC certificates.

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

## What this does not do

ORC's Performance Curve Scoring (the "constructed course" / implied-wind
options) is not implemented. It needs each boat's full polar table off its
certificate plus the wind actually observed, and it deliberately cannot be
reduced to a single owed-time number before the race. If the RC scores the
series under PCS, these tables are an approximation — use the ToD or ToT
numbers from the same certificates as a sanity check, not as the score.
