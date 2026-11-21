"""Edit saver: keep a full-size JPEG of every edited or Pick photo from Lightroom (cloud) on the NAS.

Each run:
  1. lists every photo in the Lightroom catalog (one API pass; ~1 s per 500 photos)
  2. selects photos that are edited (develop.fromDefaults == false) or flagged Pick, never Reject
  3. compares each with the state file (Lightroom asset id -> saved path + edit fingerprint)
  4. for new or re-edited photos: asks Adobe for a 'fullsize' rendition (edits applied), checks the JPEG
     opens and is full size, uploads it to <dest>/.incoming, then moves it into place
  5. writes <dest>/YYYY/YYYY-MM Month/YYYY-MM-DD NAME.jpg and <dest>/YYYY/_recipes/NAME.json
  6. writes a status file (edited vs saved counts, missing photos and since when, sign-in expiry)

Never deletes anything on the NAS. Photos removed or un-edited in Lightroom are only listed.
Exit code: 0 all saved, 2 something still missing, 1 could not run.
"""
from __future__ import annotations

import base64
import datetime as dt
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field
from typing import Optional

import click

VOLATILE_DEVELOP_KEYS = {"device", "needsUpdateAISettings"}
RENDER_TIMEOUT_S = 180
FULL_SIZE_TOLERANCE = 0.98


def log(msg: str) -> None:
    print(f"[{dt.datetime.now():%Y-%m-%d %H:%M:%S}] {msg}", flush=True)


# --- pure helpers (unit-tested) ----------------------------------------------------------------

def flag_of(payload: dict) -> Optional[str]:
    ratings = payload.get("ratings") or {}
    for r in ratings.values():
        if isinstance(r, dict) and r.get("flag"):
            return r["flag"]
    return None


def is_edited(develop: dict) -> bool:
    if "fromDefaults" in develop:
        return develop["fromDefaults"] is False
    return bool(develop.get("userUpdated"))


def wanted(payload: dict) -> bool:
    """Edited or Pick, and not Reject."""
    flag = flag_of(payload)
    if flag == "reject":
        return False
    return is_edited(payload.get("develop") or {}) or flag == "pick"


def fingerprint(payload: dict) -> str:
    """Changes when the edit (or the Pick flag) changes; ignores keys Adobe touches on its own."""
    develop = {k: v for k, v in (payload.get("develop") or {}).items() if k not in VOLATILE_DEVELOP_KEYS}
    blob = json.dumps({"develop": develop, "flag": flag_of(payload)}, sort_keys=True, default=str)
    return hashlib.sha1(blob.encode()).hexdigest()[:16]


def clean_name(file_name: str) -> str:
    """'WJD01486 (1).jpg' -> 'WJD01486'"""
    base = re.split(r"[\\/]", file_name or "photo")[-1]  # not os.path: 'a:b' is a drive on Windows
    stem = base.rsplit(".", 1)[0] if "." in base else base
    stem = re.sub(r"\s*\(\d+\)$", "", stem).strip()
    return re.sub(r'[<>:"/\\|?*]', "_", stem) or "photo"


def taken_date(payload: dict) -> dt.date:
    stamp = payload.get("captureDate") or payload.get("importTimestamp") or payload.get("userCreated") or ""
    try:
        return dt.date.fromisoformat(stamp[:10])
    except ValueError:
        return dt.date(1970, 1, 1)


def target_paths(day: dt.date, name: str, suffix: str = "") -> tuple[str, str]:
    """('2026/2026-11 November/2026-11-21 WJD01503.jpg', '2026/_recipes/WJD01503.json')"""
    month = f"{day:%Y-%m} {day:%B}"
    photo = f"{day:%Y}/{month}/{day:%Y-%m-%d} {name}{suffix}.jpg"
    recipe = f"{day:%Y}/_recipes/{name}{suffix}.json"
    return photo, recipe


@dataclass
class Action:
    asset_id: str
    kind: str  # new | resave | unchanged
    photo_path: str
    recipe_path: str
    name: str
    fp: str
    payload: dict = field(repr=False, default_factory=dict)


def plan(assets: list[dict], state: dict) -> tuple[list[Action], list[dict]]:
    """Decide what to do with each wanted asset. Keeps an asset's existing path on re-edit.
    Returns (actions, removed) where removed = saved assets no longer edited/Pick in Lightroom."""
    saved = state.get("saved", {})
    taken = {v["photo_path"]: k for k, v in saved.items()}
    actions, seen = [], set()
    for a in sorted(assets, key=lambda a: a.get("id", "")):
        p = a.get("payload") or {}
        if not wanted(p):
            continue
        aid = a["id"]
        seen.add(aid)
        fp = fingerprint(p)
        if aid in saved:
            s = saved[aid]
            kind = "unchanged" if s.get("fp") == fp else "resave"
            actions.append(Action(aid, kind, s["photo_path"], s["recipe_path"], s.get("name", ""), fp, p))
            continue
        name = clean_name((p.get("importSource") or {}).get("fileName", ""))
        day = taken_date(p)
        n, suffix = 1, ""
        while True:
            photo, recipe = target_paths(day, name, suffix)
            if photo not in taken:
                break
            n += 1
            suffix = f"-{n}"
        taken[photo] = aid
        actions.append(Action(aid, "new", photo, recipe, name + suffix, fp, p))
    removed = [dict(asset_id=k, **v) for k, v in saved.items() if k not in seen]
    return actions, removed


def full_size_ok(rendered: tuple[int, int], develop: dict, source: dict) -> bool:
    want = max(develop.get("croppedWidth") or source.get("originalWidth") or 0,
               develop.get("croppedHeight") or source.get("originalHeight") or 0)
    return want == 0 or max(rendered) >= FULL_SIZE_TOLERANCE * want


def token_expiry(tokens: Optional[dict]) -> Optional[str]:
    """ISO date-time the refresh token (the 14-day sign-in) expires, from its JWT payload."""
    try:
        part = tokens["refresh_token"].split(".")[1]
        part += "=" * (-len(part) % 4)
        p = json.loads(base64.urlsafe_b64decode(part))
        when = int(p["created_at"]) / 1000 + int(p["expires_in"]) / 1000
        return dt.datetime.fromtimestamp(when, dt.timezone.utc).isoformat(timespec="seconds")
    except Exception:
        return None


# --- where files go ------------------------------------------------------------------------------

class LocalSink:
    """Destination on a mounted/mapped path (e.g. P:\\Xt5\\Edited on the PC)."""

    def __init__(self, root: str):
        self.root = root

    def put(self, local_file: str, rel: str) -> int:
        final = os.path.join(self.root, *rel.split("/"))
        incoming = os.path.join(self.root, ".incoming", os.path.basename(final))
        os.makedirs(os.path.dirname(incoming), exist_ok=True)
        os.makedirs(os.path.dirname(final), exist_ok=True)
        shutil.copyfile(local_file, incoming)
        if os.path.getsize(incoming) != os.path.getsize(local_file):
            raise IOError(f"size mismatch after upload: {rel}")
        os.replace(incoming, final)
        return os.path.getsize(final)


class RcloneSink:
    """Destination reached through an rclone remote (e.g. nas:photo/Xt5/Edited on wyse)."""

    def __init__(self, remote_root: str, rclone: str = "rclone"):
        self.root = remote_root.rstrip("/")
        self.rclone = rclone

    def _run(self, *args: str) -> str:
        r = subprocess.run([self.rclone, *args], capture_output=True, text=True, timeout=600)
        if r.returncode != 0:
            raise IOError(f"rclone {args[0]} failed: {r.stderr.strip()[-300:]}")
        return r.stdout

    def put(self, local_file: str, rel: str) -> int:
        incoming = f"{self.root}/.incoming/{os.path.basename(rel)}"
        final = f"{self.root}/{rel}"
        self._run("copyto", local_file, incoming)
        self._run("moveto", incoming, final)
        info = json.loads(self._run("lsjson", final) or "[]")
        size = info[0]["Size"] if info else -1
        if size != os.path.getsize(local_file):
            raise IOError(f"size mismatch after upload: {rel} ({size} vs {os.path.getsize(local_file)})")
        return size


# --- Lightroom API --------------------------------------------------------------------------------

class Lightroom:
    def __init__(self, api):
        self.api = api
        self.base = f"{api.BASE_URL}/{api.API_VERSION}"
        self.catalog = api.get_catalogs()[0]["id"]

    def assets(self) -> list[dict]:
        out, ep = [], f"catalogs/{self.catalog}/assets?limit=500&subtype=image&embed=asset"
        while ep:
            r = self.api._make_request("GET", ep)
            out += r.get("resources", [])
            nxt = (r.get("links", {}).get("next") or {}).get("href")
            ep = (nxt if nxt.startswith("catalogs/") else f"catalogs/{self.catalog}/{nxt}") if nxt else None
        return out

    def render_fullsize(self, asset_id: str) -> bytes:
        url = f"{self.base}/catalogs/{self.catalog}/assets/{asset_id}/renditions"
        h = self.api._get_headers()
        h["X-Generate-Renditions"] = "fullsize"
        g = self.api._session.post(url, headers=h, timeout=60)
        if g.status_code not in (200, 201, 202):
            raise IOError(f"render request failed: HTTP {g.status_code}")
        deadline = time.time() + RENDER_TIMEOUT_S
        while time.time() < deadline:
            h = self.api._get_headers()
            h.pop("Content-Type", None)
            r = self.api._session.get(f"{url}/fullsize", headers=h, timeout=120)
            if r.status_code == 200 and r.headers.get("Content-Type", "").startswith("image"):
                return r.content
            time.sleep(3)
        raise TimeoutError("render timed out")


# --- the run --------------------------------------------------------------------------------------

def load_json(path: str, default):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return default


def save_json(path: str, data) -> None:
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1, default=str)
    os.replace(tmp, path)


def manifest_csv(state: dict) -> str:
    rows = ["asset_id,photo_path,fingerprint,saved_at,bytes"]
    for aid, s in sorted(state.get("saved", {}).items(), key=lambda kv: kv[1]["photo_path"]):
        rows.append(f'{aid},"{s["photo_path"]}",{s["fp"]},{s["saved_at"]},{s.get("bytes", "")}')
    return "\n".join(rows) + "\n"


def run(lr: Lightroom, sink, state_path: str, status_path: str, tokens: Optional[dict],
        limit: Optional[int] = None, dry_run: bool = False) -> int:
    from PIL import Image

    now = dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")
    state = load_json(state_path, {"saved": {}, "missing_since": {}})
    assets = lr.assets()
    actions, removed = plan(assets, state)
    todo = [a for a in actions if a.kind != "unchanged"]
    log(f"Lightroom: {len(assets)} photos, {len(actions)} edited or Pick; "
        f"{sum(a.kind == 'new' for a in todo)} new, {sum(a.kind == 'resave' for a in todo)} re-edited, "
        f"{len(actions) - len(todo)} already saved")
    if limit is not None:
        todo = todo[:limit]
    failed: dict[str, str] = {}
    saved_now = 0
    with tempfile.TemporaryDirectory() as tmp:
        for a in todo:
            if dry_run:
                log(f"would {a.kind}: {a.photo_path}")
                continue
            try:
                img = lr.render_fullsize(a.asset_id)
                im = Image.open(io.BytesIO(img))
                im.load()
                if not full_size_ok(im.size, a.payload.get("develop") or {}, a.payload.get("importSource") or {}):
                    raise IOError(f"render is not full size: {im.size}")
                local = os.path.join(tmp, "render.jpg")
                with open(local, "wb") as f:
                    f.write(img)
                size = sink.put(local, a.photo_path)
                recipe = os.path.join(tmp, "recipe.json")
                save_json(recipe, {"asset_id": a.asset_id, "source": (a.payload.get("importSource") or {}).get("fileName"),
                                   "captureDate": a.payload.get("captureDate"), "flag": flag_of(a.payload),
                                   "develop": a.payload.get("develop")})
                sink.put(recipe, a.recipe_path)
                state["saved"][a.asset_id] = dict(photo_path=a.photo_path, recipe_path=a.recipe_path, name=a.name,
                                                  fp=a.fp, saved_at=now, bytes=size)
                state["missing_since"].pop(a.asset_id, None)
                saved_now += 1
                log(f"{'saved' if a.kind == 'new' else 're-saved'}: {a.photo_path} ({im.size[0]}x{im.size[1]}, {size // 1024} KB)")
            except Exception as exc:
                failed[a.asset_id] = f"{a.name}: {exc}"
                state["missing_since"].setdefault(a.asset_id, now)
                log(f"FAILED {a.name}: {exc}")
            save_json(state_path, state)  # after every photo, so a crash loses nothing
        if not dry_run:
            m = os.path.join(tmp, "manifest.csv")
            with open(m, "w", encoding="utf-8", newline="\n") as f:
                f.write(manifest_csv(state))
            try:
                sink.put(m, ".manifest.csv")
            except Exception as exc:
                log(f"WARN: could not write manifest to the NAS: {exc}")

    wanted_ids = {a.asset_id for a in actions}
    missing = [aid for aid in wanted_ids
               if aid not in state["saved"] or state["saved"][aid]["fp"] != next(a.fp for a in actions if a.asset_id == aid)]
    for aid in list(state["missing_since"]):
        if aid not in wanted_ids or aid not in missing:
            state["missing_since"].pop(aid)
    names = {a.asset_id: a.name for a in actions}
    status = dict(
        last_run=now, ok=not missing, edited_total=len(actions),
        saved_total=len(actions) - len(missing), saved_this_run=saved_now,
        missing=[dict(name=names[aid], since=state["missing_since"].get(aid, now), error=failed.get(aid))
                 for aid in sorted(missing, key=lambda x: names[x])],
        removed_in_lightroom=[r.get("photo_path") for r in removed],
        sign_in_expires=token_expiry(tokens),
    )
    if not dry_run:
        save_json(state_path, state)
        save_json(status_path, status)
    log(f"Edits on NAS: {status['saved_total']} of {status['edited_total']} "
        + ("OK" if status["ok"] else f"-- {len(missing)} missing")
        + (f"; Lightroom sign-in expires {status['sign_in_expires'][:10]}" if status["sign_in_expires"] else ""))
    return 0 if status["ok"] else 2


@click.command("save-edits")
@click.option("--dest", help="Local/mapped destination, e.g. P:\\Xt5\\Edited")
@click.option("--rclone-remote", help="rclone destination instead of --dest, e.g. nas:photo/Xt5/Edited")
@click.option("--rclone", "rclone_bin", default="rclone", show_default=True, help="rclone binary")
@click.option("--state-dir", required=True, help="Where state.json and status.json live")
@click.option("--limit", type=int, help="Save at most N photos this run (testing)")
@click.option("--dry-run", is_flag=True, help="List what would be saved; change nothing")
def save_edits(dest, rclone_remote, rclone_bin, state_dir, limit, dry_run):
    """Save every edited or Pick photo to the NAS as a full-size JPEG."""
    from lightroom_tool.config import Config
    from lightroom_tool.photos.api_client import LightroomAPIClient

    if bool(dest) == bool(rclone_remote):
        raise click.UsageError("give exactly one of --dest or --rclone-remote")
    sink = LocalSink(dest) if dest else RcloneSink(rclone_remote, rclone_bin)
    status_path = os.path.join(state_dir, "edit-saver-status.json")
    try:
        api = LightroomAPIClient(Config())
        tokens = api.token_manager.get_tokens()
        if not api.token_manager.get_valid_access_token():
            raise RuntimeError("Lightroom sign-in missing or expired: run 'Renew Lightroom sign-in'")
        code = run(Lightroom(api), sink, os.path.join(state_dir, "edit-saver-state.json"), status_path,
                   tokens, limit, dry_run)
    except Exception as exc:
        log(f"ERROR: {exc}")
        prev = load_json(status_path, {})
        prev.update(last_attempt=dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
                    ok=False, error=str(exc))
        if not dry_run:
            save_json(status_path, prev)
        sys.exit(1)
    sys.exit(code)
