---
name: session-state-doc
description: Append an end-of-session state summary to docs/STATE.md and push it
---
At the end of a session with non-trivial changes:
1. Append a dated entry (most recent on top) to docs/STATE.md — create the file if it doesn't exist yet.
2. Each entry, under 20 lines, covers:
   - What changed this session.
   - What's currently deployed and where (which host).
   - What's verified working, and the exact command used to verify it.
   - Any open blockers, with the exact error text.
3. Commit docs/STATE.md (use the commit-push skill) and push.
Do this proactively at the natural end of a session that touched deployments, infra, or multi-step work — don't wait to be asked.
