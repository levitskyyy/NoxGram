# NoxGram Desktop

<img src="Telegram/Resources/branding/noxgram.png" width="160" alt="NoxGram app icon">

NoxGram Desktop is an independent Telegram client built with Qt. It is a fork of
[Nagram Desktop](https://github.com/NextAlone/Nagram-Qt), which is itself based on
[Telegram Desktop](https://github.com/telegramdesktop/tdesktop). On top of
Nagram it adds AyuGram's features: ghost mode, spy mode, forwarding from
restricted chats and quick actions, modelled on
[AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop), and a
WebSocket connection for networks that block Telegram.

## Credits and origin

NoxGram stands on the work of these projects:

| Project | What NoxGram takes from it |
| --- | --- |
| [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) | The whole client, protocol and UI ([GPLv3](LICENSE), [legal notices](LEGAL)) |
| [Nagram Desktop](https://github.com/NextAlone/Nagram-Qt) by NextAlone and contributors | The base of this fork: every Nagram feature, the settings system, translations and build setup |
| [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop) by the AyuGram team | The design of ghost mode, spy mode, view-once media keeping, forwarding from restricted chats and the quick actions: which requests to suppress, where to hook them, and how deleted and edited messages are kept (GPLv3); the bundled SQLite amalgamation was taken from its tree |
| [SQLite](https://sqlite.org) | The on-disk database of spy mode (public domain) |
| [tg-ws-proxy](https://github.com/Flowseal/tg-ws-proxy) by Flowseal | The idea and endpoints of the WebSocket connection: MTProto inside a WebSocket to `kwsN.web.telegram.org` (MIT) |

The ghost and spy mode code in `Telegram/SourceFiles/noxgram/` was written for
NoxGram against the Nagram code base, following AyuGram's implementation. All
three projects are licensed under GPLv3, and so is NoxGram.

## Ghost mode

Settings → NoxGram → Ghost and spy mode, the "Ghost mode" switch in the main
menu, or the tray menu. While it is on, each of these can be turned on or off
separately:

- **Don't send read receipts.** Chats are read only locally, the sender keeps
  seeing one check mark. Voice and video messages stay unplayed for the sender.
  "Read up to here" in the message menu reads a chat on purpose.
- **Don't mark stories as seen.** Opening someone's story can first ask to turn
  ghost mode on.
- **Don't send online status.**
- **Don't send typing and upload status.**
- **Go offline after actions.** Telegram marks you online when you send a
  message, react or vote; NoxGram sends an offline status right after that.
- **Read chat when replying.**
- **Send messages as scheduled** (off by default). Text, stickers, GIFs and
  voice messages are scheduled a few seconds ahead, so sending them doesn't
  show you online.
- **Send without sound:** never, in ghost mode or always; the send menu then
  offers "Send with sound".

## Spy mode

- **Keep deleted messages.** Messages deleted by others stay in the chat with a
  "deleted" mark, optionally semi-transparent, until the app restarts. The chat
  menu gets "Deleted messages", which lists them later with their photos and
  files.
- **Save photos, videos and files** of deleted messages, up to a size limit.
- **Keep edit history.** "Edit history" in the message menu shows the previous
  versions.
- **Keep view-once and timed media.** View-once and self-destructing photos,
  videos and voice messages stay in the chat after viewing, marked with 🔥, and
  can be reopened and saved. "Mark as viewed for the sender" sends the receipt
  on purpose.
- **Keep auto-deleting messages** that run out on a timer.
- **Also in chats with bots** (off by default).

Everything is stored **only on this device**, unencrypted, in an SQLite
database `tdata/noxgram/<account id>/spy.sqlite` with the media in `media/`
next to it. Nothing of it is kept in memory. Settings show how much space it
takes and clear it. Messages you delete yourself, or your own messages deleted
from another device, are not kept.

Ghost mode only stops the client from sending these signals. People can still
notice indirect signs, for example a reply to a message they see as unread.

## Chats with restricted forwarding

Where forwarding is turned off, NoxGram downloads the messages and sends them
again as new ones: text with formatting, photos, videos, files, albums, voice
messages and stickers. Copying and saving such content is Nagram's "Allow
copying and saving protected content" option.

## Quick actions

- Message menu: Edit history, Read up to here, Mark as viewed for the sender.
- Chat menu: Jump to the beginning, Open the channel of a group, Deleted
  messages, Delete my messages.
- `tg://user?id=` links open the user by id.
- Presentation mode (Nagram's streamer mode: windows hidden from screen
  capture, details hidden) can be toggled from the main menu and the tray.

## WebSocket connection

Settings → NoxGram → Connection → "Connect through web.telegram.org". The
MTProto stream then goes inside a WebSocket over TLS to Telegram's own web
servers (`wss://kwsN.web.telegram.org/apiws`), the way Telegram Web connects,
so to the network it looks like ordinary HTTPS. The server certificate is
verified and the MTProto encryption inside is unchanged. It is built into the
client's network layer, no local proxy runs. If web.telegram.org can't be
reached, NoxGram falls back to a direct connection unless that is turned off.
The setting does not apply while a proxy is configured.

## Building

Build it like Nagram and Telegram Desktop: [macOS](docs/building-mac.md),
[Windows](docs/building-win.md), [Linux](docs/building-linux.md). Use your own
Telegram API credentials (`TDESKTOP_API_ID` / `TDESKTOP_API_HASH`).

Automatic updates are not set up for NoxGram yet. The updater still accepts
only packages signed with the keys in `Telegram/Resources/nagram/update`, so
build with `-DDESKTOP_APP_DISABLE_AUTOUPDATE=ON` until NoxGram has its own
update keys and releases.

The internal CMake target is still `Telegram` and the Nagram code keeps its
`Nagram` namespaces and `nagram.*` setting keys, which keeps merging upstream
Nagram changes simple. NoxGram's own code lives in `Telegram/SourceFiles/noxgram/`.

- [Source](https://github.com/levitskyyy/NoxGram) ·
  [Releases](https://github.com/levitskyyy/NoxGram/releases)
- [Brand assets and application identity](BRANDING.md)
- Nagram's own documentation is kept in [docs/nagram](docs/nagram/).

## License

Source code is licensed under [GPLv3](LICENSE), with the upstream
[legal notices](LEGAL).
