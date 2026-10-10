# Fonts and themes

Fonts and themes can be set in Settings → Appearance (client), an account’s Appearance page, and Manage → General for a space or room. Empty font and theme-file fields inherit from the nearest space, account, or client. A room can override the text font while inheriting the code font and theme. Choosing a built-in theme at a lower level replaces an inherited custom palette.

Enter an installed font family or a font file path. Click **Apply fonts and theme**, or close the settings editor, to apply the saved choices. Applying follows the account and room currently being viewed; editing another account saves its choices for when that account is selected.

A custom theme is a JSON palette file. Colors are unsigned 32-bit ARGB integers. Every color is optional; omitted colors use the selected built-in theme. See `themes/example.json` for an example. Available colors are `background`, `sidebar`, `chosen`, `text`, `dim`, `accent`, `error`, `selected`, `selected_text`, `band`, `section`, `tile`, `sent_time`, `bubble`, `out_bubble`, `chat`, `chat_top`, `pattern`, and `on_accent`.

# Notifications

Notification modes and sound preferences use Matrix push rules. Changes received through sync update both the account and room controls. Room sound changes also cover mention and keyword rules, which precede ordinary room rules. Failed or offline edits are retained for retry.

XMPP room modes use XEP-0492 in XEP-0402 bookmarks, including live bookmark notifications. Unknown bookmark extensions and other clients’ advanced settings are preserved. XEP-0492 defines per-chat modes, so XMPP account defaults, direct-chat choices, and sounds remain local. No mux-specific protocol extension is published.

Sender-name and message-preview switches have been removed; notifications always include both. Notification backend, custom sound files, and UnifiedPush device registration remain local. Custom sounds accept Ogg Opus or Vorbis files; an empty path uses the default sound.
