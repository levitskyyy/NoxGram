# NoxGram Desktop branding

NoxGram Desktop is an independent Telegram client. It is a fork of
[Nagram Desktop](https://github.com/NextAlone/Nagram-Qt) (by NextAlone), which
is based on Telegram Desktop; ghost mode and spy mode are modelled on
[AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop). Upstream source,
history, license and copyright notices remain with their respective authors.

The NoxGram icon is `Telegram/Resources/branding/noxgram-logo.png`. The
square rendition `noxgram.png` and every application icon (the PNGs in
`Telegram/Resources/art`, the macOS `Icon.iconset`, the Windows ICO and the
alternate icons in `Telegram/Resources/nagram/icons`) are derived from it.

The Nagram icon artwork (Copyright © MaitungTM) is not part of NoxGram: its
Icon Composer documents were removed and the macOS build uses the static
icon set. The monochrome tray glyph `nagram-symbolic.svg` is still the
Nagram one and should be replaced with a NoxGram glyph.

Application identity:

- Display name: NoxGram Desktop; executable: NoxGram; macOS bundle:
  NoxGram Desktop.
- macOS and Linux application ID: `app.noxgram.desktop`.
- Windows application ID: `NoxGram.NoxGramDesktop`.
- Portable directory: `NoxGramForcePortable`.
- Telegram `tg:` and `tonsite:` protocol compatibility is retained.

Release links point to https://gitlab.com/levitskyyy/NoxGram. Update packages
are still verified with the Nagram keys in `Telegram/Resources/nagram/update`;
replace them with NoxGram keys before publishing updates, or build with
`DESKTOP_APP_DISABLE_AUTOUPDATE=ON`.

Internal names stay as in Nagram to keep upstream merges simple: the CMake
target `Telegram`, the `Nagram` C++ namespace, `nagram/` source paths,
`lng_nagram_*` string keys and `nagram.*` setting keys. Packaging scripts in
`tools/nagram`, `packaging/` and `snap/` still describe Nagram releases.
