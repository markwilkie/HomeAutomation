---
name: commit-push
description: Stage relevant changes, write a why-focused commit message, and push
---
1. Run `git status` and `git diff --stat` to see what's changed; never use `git add -A` or `git add .` — stage specific files by name. Only pull a full `git diff <file>` for a specific file when `--stat` isn't enough to understand the change or write the message — large diffs (e.g. big deletions/rewrites) burn a lot of context, so prefer `--stat`, `git log -p -- <file> | head`, or reading the file directly over dumping the whole diff.
2. Skip anything that looks like a secret (.env, credentials, tokens) and warn me if I asked to commit one anyway.
3. Write a concise (1-2 sentence) commit message focused on *why*, matching this repo's existing commit style (check `git log` for tone).
4. End the commit message with:
   Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
5. Commit, then push, then run `git status` to confirm a clean tree.
6. If a pre-commit hook fails, fix the issue and create a NEW commit — never `--amend` a commit that a failed hook prevented, and never use `--no-verify`.
Only commit when I've explicitly asked for it in this session.
