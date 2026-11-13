#!/usr/bin/env python3
"""Put a built NoxGram Desktop.app into a disk image to install it from.

The image opens as a window with the application next to a link to
/Applications, to drag one onto the other, over the background that
macos_dmg_background.py draws. Runs on macOS and needs dmgbuild in the
versions that macos_dmg.txt pins.
"""

import argparse
import sys
from pathlib import Path

INSTALL = "python3 -m pip install --require-hashes -r tools/nagram/macos_dmg.txt"
VOLUME = "NoxGram"
# The oldest macOS that NoxGram runs on reads this compression.
FORMAT = "ULFO"
WINDOW = ((200, 120), (640, 400))
ICON_SIZE = 128
APPLICATION_AT = (170, 180)
APPLICATIONS_AT = (470, 180)
TEXT_SIZE = 13
BACKGROUND = (Path(__file__).resolve().parents[2]
    / "Telegram" / "Resources" / "branding" / "dmg" / "background.tiff")


class DmgError(Exception):
    pass


def volume_icon(app):
    """The icon of the bundle, shown for the mounted image too."""
    resources = app / "Contents" / "Resources"
    for name in ("NoxGram.icns", "Icon.icns"):
        if (resources / name).is_file():
            return resources / name
    return None


def settings(app):
    """What dmgbuild makes the image from."""
    result = {
        "format": FORMAT,
        "files": [str(app)],
        "symlinks": {"Applications": "/Applications"},
        "icon_locations": {app.name: APPLICATION_AT, "Applications": APPLICATIONS_AT},
        "window_rect": WINDOW,
        "default_view": "icon-view",
        "icon_size": ICON_SIZE,
        "text_size": TEXT_SIZE,
        "background": str(BACKGROUND),
        "hide_extensions": [app.name],
        "show_status_bar": False,
        "show_tab_view": False,
        "show_toolbar": False,
        "show_pathbar": False,
        "show_sidebar": False,
    }
    icon = volume_icon(app)
    if icon:
        result["icon"] = str(icon)
    return result


def build(app, out):
    if not (app / "Contents" / "MacOS").is_dir():
        raise DmgError(f"{app} is not an application bundle")
    if out.suffix != ".dmg":
        raise DmgError(f"{out} must be named *.dmg")
    try:
        import dmgbuild
    except ImportError as error:
        raise DmgError(f"{error}: {INSTALL}") from error
    out.parent.mkdir(parents=True, exist_ok=True)
    out.unlink(missing_ok=True)
    dmgbuild.build_dmg(str(out), VOLUME, settings=settings(app))
    if not out.is_file():
        raise DmgError(f"dmgbuild did not write {out}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--app", required=True, help="the built NoxGram Desktop.app")
    parser.add_argument("--out", required=True, help="the disk image to write, *.dmg")
    args = parser.parse_args(argv)
    try:
        build(Path(args.app).resolve(), Path(args.out).resolve())
    except DmgError as error:
        print(error, file=sys.stderr)
        return 1
    print(args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
