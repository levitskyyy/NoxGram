# NoxGram Desktop

<img src="Telegram/Resources/branding/noxgram.png" width="160" alt="NoxGram app icon">

NoxGram Desktop is an independent Telegram client built with Qt. It is a fork of
[Nagram Desktop](https://github.com/NextAlone/Nagram-Qt), which is itself based on
[Telegram Desktop](https://github.com/telegramdesktop/tdesktop). On top of
Nagram it adds a ghost mode and a spy mode, modelled on the features of
[AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop).

## Credits and origin

NoxGram stands on the work of these projects:

| Project | What NoxGram takes from it |
| --- | --- |
| [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) | The whole client, protocol and UI ([GPLv3](LICENSE), [legal notices](LEGAL)) |
| [Nagram Desktop](https://github.com/NextAlone/Nagram-Qt) by NextAlone and contributors | The base of this fork: every Nagram feature, the settings system, translations and build setup |
| [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop) by the AyuGram team | The design of ghost mode and spy mode: which requests to suppress, where to hook them, and how deleted and edited messages are kept (GPLv3) |
| [tg-ws-proxy](https://github.com/Flowseal/tg-ws-proxy) by Flowseal | The idea and endpoints of the WebSocket connection: MTProto inside a WebSocket to `kwsN.web.telegram.org` (MIT) |

The ghost and spy mode code in `Telegram/SourceFiles/noxgram/` was written for
NoxGram against the Nagram code base, following AyuGram's implementation. All
three projects are licensed under GPLv3, and so is NoxGram.

## Ghost mode

Settings → NoxGram → Ghost and spy mode, or the "Ghost mode" switch in the
main menu. While it is on, each of these can be turned on or off separately:

- **Don't send read receipts.** Chats are read only locally, the sender keeps
  seeing one check mark. Voice and video messages stay unplayed for the sender.
- **Don't mark stories as seen.** Stories are marked as seen locally, the
  author does not see you among the viewers.
- **Don't send online status.** The client never reports you as online.
- **Don't send typing and upload status.**
- **Go offline after actions.** Telegram marks you online when you send a
  message, react or vote; NoxGram sends an offline status right after that.
- **Read chat when replying.** When you send something to a chat, that chat
  is marked as read, so your reply doesn't sit next to unread messages.
- **Send text messages as scheduled** (off by default). Text messages are
  scheduled about 12 seconds ahead, so sending them doesn't show you online.
  Media, stickers and forwards are sent as usual.

## Spy mode

- **Keep deleted messages.** Messages deleted by others stay in the chat with a
  "deleted" mark (until the app restarts). Their text is saved, and the chat
  menu gets a "Deleted messages" item that lists them later.
- **Keep edit history.** Previous versions of edited messages are saved;
  "Edit history" in the message menu shows them.
- **Keep self-destructing messages** that run out on a timer.
- **Also in chats with bots** (off by default).

Saved texts are stored **only on this device**, unencrypted, as JSON lines in
`tdata/noxgram/<account id>/` (`deleted.jsonl`, `edits.jsonl`). Media of a
deleted message stays visible only while it is still in the chat. Messages you
delete yourself, or your own messages deleted
from another device, are not kept.

Ghost mode only stops the client from sending these signals. People can still
notice indirect signs, for example a reply to a message they see as unread.

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
