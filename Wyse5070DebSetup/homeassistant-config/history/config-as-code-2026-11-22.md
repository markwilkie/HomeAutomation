# Dashboards and registry as code -- 2026-11-22

Before today the dashboards (MiniSplit, Spa, Map) were UI-edited storage dashboards and the
floors/areas/device settings existed only in `.storage`. Same day, in order:

- Areas tidied to follow the SmartThings rooms; floors added (Dormer, Main Floor, Lower Level,
  Outside); TV Room (wyse and its radios, SHIELD) and Shop (Thread Router, shop speaker) added.
  Bedroom/Living Room/HIDDEN areas removed (HIDDEN devices unassigned, their entities hidden).
- Dashboards: Home (new default), System (admin), Config (admin, one tab per area), Climate
  (was MiniSplit, monitoring only) and Spa (monitoring only; calibration moved to Config).
- Then exported as-is to `dashboards/*.yaml` and `registry/registry.yaml` (`--check` showed 0
  differences against live before switching), storage dashboards deleted, YAML dashboards loaded.

One-off actions (not represented in any file):
- Removed the orphaned entity `automation.minisplit_follow_me_experiment_schedule`.
- Deleted the stray UI helper `input_number.2deg` (0, unused).
- Cleared long-term statistics of 36 entities that no longer exist (old bridge endpoint names,
  early `test_product_*` firmware sensors, retired BME280/trend sensors).
- Disabled the empty duplicate "Mini-Split AC Bridge" device that SmartThings shares into HA
  (in the registry file as `disabled: true`).
