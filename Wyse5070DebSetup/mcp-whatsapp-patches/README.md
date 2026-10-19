# mcp-whatsapp patches

Applied in order by `setup-mcp-gateway-whatsapp.sh` on top of the pinned
Sealjay/mcp-whatsapp release. Each one is a candidate to send upstream;
delete it once a release includes the fix.

## 0001-sender-and-contact-names.patch

Found 2026-10-19: names were missing almost everywhere.

- **Group senders from history sync.** Backfilled group messages carry the
  sender in `WebMessageInfo.participant`, not `Key.Participant`, so every
  one was stored as sent by the group itself (2,249 of 2,292 group
  messages). Now read from either field.
- **Push names from history sync.** Each backfilled message's sender
  WhatsApp name is saved as a contact push name, so people outside the
  address book get a name instead of a number.
- **Sender name lookup.** Messages store senders as a bare number, but
  `whatsmeow_contacts` is keyed on full JIDs, so `GetSenderName` never
  matched a contact. Now also tries `<n>@s.whatsapp.net` and `<n>@lid`.
- **Chat names.** A 1:1 chat that arrives before its contact syncs is named
  after the bare number forever (`GetChatName` reuses the stored name).
  `RefreshChatNames` renames those once the contact is known; it runs after
  each history sync and before `list_chats`, so name search finds them.
