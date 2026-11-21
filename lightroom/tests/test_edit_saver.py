import datetime as dt

from lightroom_tool.edit_saver import (clean_name, fingerprint, full_size_ok, plan, target_paths,
                                       token_expiry, wanted)


def asset(aid, name="WJD01503.JPG", capture="2026-11-21T09:12:33", edited=True, flag=None, crop=(5115, 7672)):
    develop = {"fromDefaults": not edited, "croppedWidth": crop[0], "croppedHeight": crop[1], "device": "x"}
    payload = {"importSource": {"fileName": name, "originalWidth": 5152, "originalHeight": 7728},
               "captureDate": capture, "develop": develop}
    if flag:
        payload["ratings"] = {"user1": {"flag": flag}}
    return {"id": aid, "payload": payload}


def test_wanted_edited_pick_reject():
    assert wanted(asset("a")["payload"])
    assert not wanted(asset("a", edited=False)["payload"])
    assert wanted(asset("a", edited=False, flag="pick")["payload"])
    assert not wanted(asset("a", flag="reject")["payload"])


def test_wanted_falls_back_to_userUpdated():
    p = {"develop": {"userUpdated": "2026-11-21T10:00:00Z"}}
    assert wanted(p)
    assert not wanted({"develop": {}})


def test_clean_name_strips_duplicate_suffix_and_extension():
    assert clean_name("WJD01486 (1).jpg") == "WJD01486"
    assert clean_name("WJD01472.RAF") == "WJD01472"
    assert clean_name("a:b.jpg") == "a_b"


def test_target_paths_month_folder_and_date_prefix():
    photo, recipe = target_paths(dt.date(2026, 11, 21), "WJD01503")
    assert photo == "2026/2026-11 November/2026-11-21 WJD01503.jpg"
    assert recipe == "2026/_recipes/WJD01503.json"


def test_fingerprint_ignores_volatile_keys_but_sees_edits():
    a, b = asset("a")["payload"], asset("a")["payload"]
    b["develop"]["device"] = "other-pc"
    assert fingerprint(a) == fingerprint(b)
    b["develop"]["croppedWidth"] = 4000
    assert fingerprint(a) != fingerprint(b)


def test_plan_new_unchanged_resave_removed():
    a1, a2 = asset("id1"), asset("id2", name="WJD01510.JPG")
    actions, removed = plan([a1, a2], {"saved": {}})
    assert [x.kind for x in actions] == ["new", "new"]
    state = {"saved": {x.asset_id: dict(photo_path=x.photo_path, recipe_path=x.recipe_path, name=x.name, fp=x.fp,
                                        saved_at="t") for x in actions}}
    state["saved"]["gone"] = dict(photo_path="2026/old.jpg", recipe_path="r", name="old", fp="f", saved_at="t")
    a2["payload"]["develop"]["croppedWidth"] = 3000  # re-edit
    actions, removed = plan([a1, a2], state)
    kinds = {x.asset_id: x.kind for x in actions}
    assert kinds == {"id1": "unchanged", "id2": "resave"}
    assert [r["asset_id"] for r in removed] == ["gone"]


def test_plan_resave_keeps_original_path():
    a = asset("id1")
    (first,), _ = plan([a], {"saved": {}})
    state = {"saved": {"id1": dict(photo_path=first.photo_path, recipe_path=first.recipe_path, name=first.name,
                                   fp="old", saved_at="t")}}
    a["payload"]["captureDate"] = "2025-01-01T00:00:00"  # even if metadata moves, the file stays put
    (again,), _ = plan([a], state)
    assert again.kind == "resave" and again.photo_path == first.photo_path


def test_plan_collision_gets_suffix():
    a, b = asset("id1", name="WJD01486.jpg"), asset("id2", name="WJD01486 (1).jpg")
    actions, _ = plan([a, b], {"saved": {}})
    paths = sorted(x.photo_path for x in actions)
    assert paths == ["2026/2026-11 November/2026-11-21 WJD01486-2.jpg",
                     "2026/2026-11 November/2026-11-21 WJD01486.jpg"]


def test_full_size_ok():
    dev = {"croppedWidth": 5115, "croppedHeight": 7672}
    assert full_size_ok((5115, 7672), dev, {})
    assert full_size_ok((7672, 5115), dev, {})
    assert not full_size_ok((1365, 2048), dev, {})


def test_token_expiry_reads_jwt():
    import base64, json
    body = base64.urlsafe_b64encode(json.dumps({"created_at": "1795284535000", "expires_in": "1209600000"}).encode()).decode().rstrip("=")
    assert token_expiry({"refresh_token": f"h.{body}.s"}).startswith("2026-12-05")
    assert token_expiry(None) is None
