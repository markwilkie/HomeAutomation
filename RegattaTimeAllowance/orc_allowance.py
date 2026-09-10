#!/usr/bin/env python3
"""Pairwise ORC time-allowance ("who owes whom") tables for a regatta.

Reads a JSON file describing the boats in a class and the races in the series,
then prints, for every race, a matrix of how much time each boat owes every
other boat.

Two ORC scoring families are supported:

  ToD (Time on Distance)
      Each boat's certificate gives an allowance in seconds per nautical mile.
      corrected = elapsed - allowance * distance
      A owes B:  (allowance_B - allowance_A) * distance          [seconds]
      The lower s/mile boat is the faster boat and owes time.

  ToT (Time on Time)
      Each boat's certificate gives a multiplier (ORC publishes 675/GPH, plus
      the Triple Number low/medium/high coefficients).
      corrected = elapsed * coefficient
      A and B tie when  elapsed_A * coef_A == elapsed_B * coef_B, so
      A owes B:  elapsed_B * (1 - coef_B / coef_A)               [seconds]
      That depends on how long the race takes, so a ToT race needs a
      reference elapsed time (the assumed elapsed time of the *owed* boat).
      Without one the table is expressed per hour of elapsed time.

Performance Curve Scoring (PCS / "constructed course") is deliberately not
implemented: it needs each boat's full polar table from its certificate and
the actual wind speed, and it cannot be reduced to a single pairwise number
before the race.

Usage:
    ./orc_allowance.py regatta.json
    ./orc_allowance.py regatta.json --format csv --out ./tables
    ./orc_allowance.py regatta.json --highlight Mayhem
    ./orc_allowance.py regatta.json --score
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import sys

TOD = "ToD"
TOT = "ToT"


# --------------------------------------------------------------------------
# time helpers
# --------------------------------------------------------------------------

def parse_hms(value):
    """Parse '1:23:45', '23:45', '45', or a number into float seconds."""
    if value is None:
        return None
    if isinstance(value, (int, float)):
        return float(value)
    text = str(value).strip()
    if not text:
        return None
    sign = 1.0
    if text.startswith("-"):
        sign, text = -1.0, text[1:]
    parts = text.split(":")
    if len(parts) > 3:
        raise ValueError("cannot parse time %r" % value)
    try:
        numbers = [float(p) for p in parts]
    except ValueError:
        raise ValueError("cannot parse time %r" % value)
    seconds = 0.0
    for number in numbers:
        seconds = seconds * 60.0 + number
    return sign * seconds


def fmt_hms(seconds, signed=False):
    """Format seconds as [-]H:MM:SS, dropping the hour field when it is zero."""
    if seconds is None:
        return "-"
    sign = "-" if seconds < 0 else ("+" if signed and seconds > 0 else "")
    total = int(round(abs(seconds)))
    hours, rest = divmod(total, 3600)
    minutes, secs = divmod(rest, 60)
    if hours:
        return "%s%d:%02d:%02d" % (sign, hours, minutes, secs)
    return "%s%d:%02d" % (sign, minutes, secs)


# --------------------------------------------------------------------------
# config loading and validation
# --------------------------------------------------------------------------

class ConfigError(Exception):
    pass


def load_config(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            config = json.load(handle)
    except FileNotFoundError:
        raise ConfigError("no such file: %s" % path)
    except json.JSONDecodeError as exc:
        raise ConfigError("%s is not valid JSON: %s" % (path, exc))

    boats = config.get("boats") or []
    races = config.get("races") or []
    if len(boats) < 2:
        raise ConfigError("need at least two boats to compare")
    if not races:
        raise ConfigError("no races defined")

    seen = set()
    for boat in boats:
        name = boat.get("name")
        if not name:
            raise ConfigError("every boat needs a name")
        if name in seen:
            raise ConfigError("duplicate boat name: %s" % name)
        seen.add(name)

    for index, race in enumerate(races, start=1):
        race.setdefault("name", "Race %d" % index)
        scoring = race.get("scoring", TOD)
        if scoring not in (TOD, TOT):
            raise ConfigError("%s: scoring must be %r or %r, got %r"
                              % (race["name"], TOD, TOT, scoring))
        if scoring == TOD and not race.get("distance_nm"):
            raise ConfigError("%s: ToD scoring needs distance_nm"
                              % race["name"])
    return config


def rating_for(boat, race):
    """Return the certificate number this boat races under in this race.

    ToD races look up race['band'] in boat['tod']; ToT races look it up in
    boat['tot']. A band of None (or a bare number instead of a dict) means the
    boat has a single allowance for every race.
    """
    scoring = race.get("scoring", TOD)
    key = "tod" if scoring == TOD else "tot"
    ratings = boat.get(key)
    if ratings is None:
        raise ConfigError("%s has no %r ratings, needed by %s"
                          % (boat["name"], key, race["name"]))
    if isinstance(ratings, (int, float)):
        return float(ratings)

    band = race.get("band")
    if band is None:
        if len(ratings) == 1:
            return float(next(iter(ratings.values())))
        raise ConfigError("%s scores %s but does not say which band; %s "
                          "publishes %s" % (race["name"], scoring,
                                            boat["name"],
                                            ", ".join(sorted(ratings))))
    if band not in ratings:
        raise ConfigError("%s has no %r band for %s (has %s)"
                          % (boat["name"], band, race["name"],
                             ", ".join(sorted(ratings))))
    return float(ratings[band])


# --------------------------------------------------------------------------
# the actual allowance maths
# --------------------------------------------------------------------------

def owed_seconds(rating_a, rating_b, race, reference_elapsed):
    """Seconds boat A owes boat B. Negative means A is owed by B."""
    if race.get("scoring", TOD) == TOD:
        # Certificate is s/mile; the lower (faster) rating pays the difference.
        return (rating_b - rating_a) * float(race["distance_nm"])
    # Time on time. The allowance is elapsed * (1 - slower/faster), and which
    # elapsed time you use matters, so always anchor the reference elapsed to
    # the slower boat of the pair and mirror the sign. Otherwise the table is
    # asymmetric: A would owe B a little less than B is owed by A.
    if rating_a >= rating_b:
        return reference_elapsed * (1.0 - rating_b / rating_a)
    return -reference_elapsed * (1.0 - rating_a / rating_b)


def reference_elapsed_for(race):
    """Elapsed time (seconds) a ToT table is built against, and its label."""
    if race.get("scoring", TOD) == TOD:
        return None, None
    explicit = parse_hms(race.get("reference_elapsed"))
    if explicit:
        return explicit, fmt_hms(explicit)
    return 3600.0, "1:00:00 (per hour of elapsed time)"


def build_race_table(boats, race):
    """Compute one race's ratings, ordering and pairwise owe matrix."""
    reference, reference_label = reference_elapsed_for(race)
    ratings = {boat["name"]: rating_for(boat, race) for boat in boats}

    # Fastest boat first: lowest s/mile under ToD, highest multiplier under ToT.
    reverse = race.get("scoring", TOD) == TOT
    order = sorted(ratings, key=lambda name: ratings[name], reverse=reverse)

    matrix = {}
    for a in order:
        matrix[a] = {}
        for b in order:
            if a == b:
                matrix[a][b] = None
                continue
            matrix[a][b] = owed_seconds(ratings[a], ratings[b], race, reference)

    return {
        "race": race,
        "ratings": ratings,
        "order": order,
        "matrix": matrix,
        "reference_elapsed": reference,
        "reference_label": reference_label,
    }


def score_race(boats, race, table):
    """Corrected times and standings, when a race carries recorded results."""
    results = race.get("results")
    if not results:
        return None

    scored = []
    for entry in results:
        name = entry.get("boat")
        if name not in table["ratings"]:
            raise ConfigError("%s: result for unknown boat %r"
                              % (race["name"], name))
        elapsed = parse_hms(entry.get("elapsed"))
        if elapsed is None:
            start = parse_hms(entry.get("start"))
            finish = parse_hms(entry.get("finish"))
            if start is None or finish is None:
                raise ConfigError("%s: %s needs elapsed, or start and finish"
                                  % (race["name"], name))
            elapsed = finish - start
        rating = table["ratings"][name]
        if race.get("scoring", TOD) == TOD:
            corrected = elapsed - rating * float(race["distance_nm"])
        else:
            corrected = elapsed * rating
        scored.append({"boat": name, "elapsed": elapsed, "corrected": corrected})

    scored.sort(key=lambda row: row["corrected"])
    winner = scored[0]["corrected"] if scored else 0.0
    for place, row in enumerate(scored, start=1):
        row["place"] = place
        row["behind"] = row["corrected"] - winner
    return scored


# --------------------------------------------------------------------------
# rendering
# --------------------------------------------------------------------------

def rating_units(race):
    return "s/mile" if race.get("scoring", TOD) == TOD else "coefficient"


def race_heading(race, table):
    bits = []
    if race.get("date"):
        bits.append(race["date"])
    if race.get("course"):
        bits.append(race["course"])
    bits.append(race.get("scoring", TOD))
    if race.get("band"):
        bits.append(str(race["band"]))
    if race.get("scoring", TOD) == TOD:
        bits.append("%.2f nm" % float(race["distance_nm"]))
    else:
        bits.append("reference elapsed %s" % table["reference_label"])
    return "%s - %s" % (race["name"], ", ".join(bits))


def render_markdown(config, tables, highlight=None, scored=None):
    lines = []
    title = config.get("regatta", "Regatta")
    if config.get("class"):
        title = "%s - %s" % (title, config["class"])
    lines.append("# Time owed: %s" % title)
    lines.append("")
    if config.get("placeholder"):
        lines.append("> **PLACEHOLDER RATINGS - NOT REAL CERTIFICATE DATA.** "
                     "Replace the numbers in the data file with the class "
                     "scratch sheet before using these tables.")
        lines.append("")
    lines.append("A cell is what the **row** boat owes the **column** boat at "
                 "the finish. `+` means the row boat has to finish that far "
                 "ahead; `-` means it can finish that far behind and still "
                 "correct out ahead.")
    lines.append("")

    for table in tables:
        race = table["race"]
        lines.append("## %s" % race_heading(race, table))
        lines.append("")

        header = ["Boat", "Rating (%s)" % rating_units(race)] + table["order"]
        lines.append("| " + " | ".join(header) + " |")
        lines.append("|" + "|".join(["---"] * len(header)) + "|")
        for a in table["order"]:
            label = "**%s**" % a if a == highlight else a
            row = [label, "%.4g" % table["ratings"][a]]
            for b in table["order"]:
                value = table["matrix"][a][b]
                row.append("-" if value is None else fmt_hms(value, signed=True))
            lines.append("| " + " | ".join(row) + " |")
        lines.append("")

        if race.get("scoring", TOD) == TOT and not race.get("reference_elapsed"):
            lines.append("_No reference elapsed time given, so this race's "
                         "figures are per hour of elapsed time: multiply by "
                         "the hours the race actually takes._")
            lines.append("")

        if scored:
            rows = scored.get(race["name"])
            if rows:
                lines.append("### Results")
                lines.append("")
                lines.append("| Place | Boat | Elapsed | Corrected | Behind |")
                lines.append("|---|---|---|---|---|")
                for row in rows:
                    lines.append("| %d | %s | %s | %s | %s |" % (
                        row["place"], row["boat"], fmt_hms(row["elapsed"]),
                        fmt_hms(row["corrected"]),
                        "-" if row["place"] == 1 else fmt_hms(row["behind"])))
                lines.append("")

    if highlight:
        lines.extend(render_highlight_markdown(tables, highlight))
    return "\n".join(lines).rstrip() + "\n"


def render_highlight_markdown(tables, highlight):
    lines = ["## %s: time owed, all races" % highlight, ""]
    others = [name for name in tables[0]["order"] if name != highlight]
    lines.append("| Boat | " + " | ".join(t["race"]["name"] for t in tables)
                 + " |")
    lines.append("|" + "|".join(["---"] * (len(tables) + 1)) + "|")
    for other in others:
        cells = []
        for table in tables:
            value = table["matrix"].get(highlight, {}).get(other)
            cells.append("-" if value is None else fmt_hms(value, signed=True))
        lines.append("| %s | %s |" % (other, " | ".join(cells)))
    lines.append("")
    lines.append("_Positive: %s owes that boat time. Negative: that boat owes "
                 "%s._" % (highlight, highlight))
    lines.append("")
    return lines


def render_text(config, tables, highlight=None, scored=None):
    """Plain fixed-width tables, for reading on a phone at the dock."""
    lines = []
    title = config.get("regatta", "Regatta")
    if config.get("class"):
        title = "%s - %s" % (title, config["class"])
    lines.append("Time owed: %s" % title)
    if config.get("placeholder"):
        lines.append("*** PLACEHOLDER RATINGS - NOT REAL CERTIFICATE DATA ***")
    lines.append("")

    for table in tables:
        race = table["race"]
        lines.append(race_heading(race, table))
        lines.append("-" * len(race_heading(race, table)))

        names = table["order"]
        width = max([len(n) for n in names] + [8])
        cell = max(9, width)
        header = "%-*s %8s" % (width, "Boat", "Rating")
        header += "".join(" %*s" % (cell, n) for n in names)
        lines.append(header)
        for a in names:
            marker = "*" if a == highlight else " "
            row = "%-*s %8.4g" % (width, marker + a, table["ratings"][a])
            for b in names:
                value = table["matrix"][a][b]
                text = "-" if value is None else fmt_hms(value, signed=True)
                row += " %*s" % (cell, text)
            lines.append(row)
        lines.append("")

        if scored:
            rows = scored.get(race["name"])
            if rows:
                lines.append("  Results (corrected):")
                for row in rows:
                    lines.append("    %d. %-*s elapsed %s  corrected %s" % (
                        row["place"], width, row["boat"],
                        fmt_hms(row["elapsed"]), fmt_hms(row["corrected"])))
                lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def write_csv(tables, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    written = []
    for table in tables:
        race = table["race"]
        safe = "".join(c if c.isalnum() else "_" for c in race["name"]).strip("_")
        path = os.path.join(out_dir, "%s.csv" % safe.lower())
        with open(path, "w", encoding="utf-8", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(["# " + race_heading(race, table)])
            writer.writerow(["owes (row) / owed (column), seconds"])
            writer.writerow(["Boat", "Rating"] + table["order"])
            for a in table["order"]:
                row = [a, table["ratings"][a]]
                for b in table["order"]:
                    value = table["matrix"][a][b]
                    row.append("" if value is None else round(value, 1))
                writer.writerow(row)
        written.append(path)
    return written


# --------------------------------------------------------------------------

def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Pairwise ORC time-allowance tables for a regatta.")
    parser.add_argument("config", help="JSON file describing boats and races")
    parser.add_argument("--format", choices=("md", "text", "csv"),
                        default="md", help="output format (default: md)")
    parser.add_argument("--out", metavar="DIR",
                        help="directory for --format csv (default: ./tables)")
    parser.add_argument("--highlight", metavar="BOAT",
                        help="add a summary of what this boat owes every "
                             "other boat across all races")
    parser.add_argument("--score", action="store_true",
                        help="also correct any finish times in the data file")
    args = parser.parse_args(argv)

    try:
        config = load_config(args.config)
        boats = config["boats"]
        tables = [build_race_table(boats, race) for race in config["races"]]

        if args.highlight and args.highlight not in tables[0]["ratings"]:
            raise ConfigError("no boat named %r; entries are: %s"
                              % (args.highlight,
                                 ", ".join(sorted(tables[0]["ratings"]))))

        scored = None
        if args.score:
            scored = {}
            for table in tables:
                rows = score_race(boats, table["race"], table)
                if rows:
                    scored[table["race"]["name"]] = rows
            if not scored:
                print("note: no results in %s to score" % args.config,
                      file=sys.stderr)
    except ConfigError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1

    if args.format == "csv":
        for path in write_csv(tables, args.out or "tables"):
            print("wrote %s" % path)
        return 0

    render = render_markdown if args.format == "md" else render_text
    sys.stdout.write(render(config, tables, args.highlight, scored))
    return 0


if __name__ == "__main__":
    sys.exit(main())
