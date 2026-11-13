#!/usr/bin/env bash
# Publish a NoxGram release on GitLab from this Mac.
#
# Usage: tools/noxgram/release_gitlab.sh [<tag>] [file...]
#
# Without a tag the next one is taken: v<app version>-nox.<N + 1>. Builds the
# universal (Apple Silicon + Intel) Release app in out/, signs it (ad hoc
# unless the Developer ID variables of tools/nagram/sign_macos.sh are set),
# packs NoxGram-macos.dmg, pushes the current branch and the new tag to the
# `gitlab` remote, uploads the image and every given file (Linux and Windows
# executables) to the generic package registry and creates the release with
# links to them. The notes list the commits since the previous release;
# NOXGRAM_RELEASE_NOTES is added to them. Needs glab logged in to gitlab.com
# and the libraries of Telegram/build/prepare/mac.sh.
set -euo pipefail

tag=""
if [ $# -gt 0 ] && [ "${1#v}" != "$1" ] && [ ! -f "$1" ]; then
	tag=$1
	shift
fi

root=$(cd "$(dirname "$0")/../.." && pwd)
project="levitskyyy/NoxGram"
encoded="levitskyyy%2FNoxGram"
remote=gitlab
helper='credential.https://gitlab.com.helper=!glab auth git-credential'
app_name="NoxGram Desktop.app"
built="$root/out/Release/$app_name"

for file in "$@"; do
	[ -f "$file" ] || { echo "No such file: $file" >&2; exit 2; }
done
glab auth status --hostname gitlab.com > /dev/null 2>&1 \
	|| { echo "Log in first: glab auth login" >&2; exit 1; }
if ! git -C "$root" diff --quiet HEAD -- ; then
	echo "Commit your changes first: the release is built from HEAD." >&2
	exit 1
fi

previous=$(git -C "$root" describe --tags --abbrev=0 --match 'v*-nox.*' \
	2> /dev/null || true)
if [ -z "$tag" ]; then
	base=$(sed -n 's/^AppVersionStr[[:space:]]\{1,\}//p' "$root/Telegram/build/version")
	last=$(git -C "$root" tag --list "v$base-nox.*" \
		| sed "s/^v$base-nox\.//" | sort -n | tail -n 1)
	tag="v$base-nox.$(( ${last:-0} + 1 ))"
fi
version=${tag#v}
echo "Releasing $tag..."

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

echo "Building the universal app..."
cmake --build "$root/out" --config Release --target Telegram -- \
	"ARCHS=arm64 x86_64" ONLY_ACTIVE_ARCH=NO > "$work/build.log" 2>&1 \
	|| { tail -40 "$work/build.log" >&2; exit 1; }

echo "Signing and packing..."
ditto "$built" "$work/$app_name"
strip "$work/$app_name/Contents/MacOS/NoxGram Desktop"
# Finder metadata on the bundle breaks a strict signature check.
xattr -cr "$work/$app_name"
bash "$root/tools/nagram/sign_macos.sh" \
	"$work/$app_name" "$root/Telegram/Telegram/Telegram.entitlements"
venv="$root/out/dmg-venv"
if [ ! -x "$venv/bin/python" ]; then
	python3 -m venv "$venv"
	"$venv/bin/pip" install -q --require-hashes -r "$root/tools/nagram/macos_dmg.txt"
fi
dmg="$work/NoxGram-macos.dmg"
"$venv/bin/python" -I "$root/tools/nagram/macos_dmg.py" \
	--app "$work/$app_name" --out "$dmg" > /dev/null
bash "$root/tools/nagram/sign_macos.sh" "$dmg"

echo "Tagging $tag..."
if ! git -C "$root" rev-parse -q --verify "refs/tags/$tag" > /dev/null; then
	git -C "$root" tag "$tag"
fi
git -C "$root" -c "$helper" push -q "$remote" HEAD "refs/tags/$tag"

echo "Uploading..."
links="["
for file in "$dmg" "$@"; do
	name=$(basename "$file")
	glab api -X PUT \
		"projects/$encoded/packages/generic/noxgram/$version/$name" \
		-H "Content-Type: application/octet-stream" \
		--input "$file" > /dev/null
	url="https://gitlab.com/api/v4/projects/$encoded/packages/generic/noxgram/$version/$name"
	links="$links{\"name\":\"$name\",\"url\":\"$url\",\"link_type\":\"package\"},"
done
links="${links%,}]"

notes="NoxGram $version.

- macOS: open NoxGram-macos.dmg and drag NoxGram to Applications. The app is not notarized: on the first start macOS refuses it, then open System Settings → Privacy & Security → Open Anyway.
- Windows: run the .exe.
- Linux: chmod +x and run the file."
if [ -n "$previous" ] && [ "$previous" != "$tag" ]; then
	changes=$(git -C "$root" log --no-merges --format='- %s' "$previous..$tag" \
		| sed 's/ \[[a-z]*-only\]//g')
	if [ -n "$changes" ]; then
		notes="$notes

Changes since ${previous#v}:
$changes"
	fi
fi
if [ -n "${NOXGRAM_RELEASE_NOTES:-}" ]; then
	notes="$notes

$NOXGRAM_RELEASE_NOTES"
fi
glab release create "$tag" -R "$project" \
	--name "NoxGram $version" \
	--notes "$notes" \
	--assets-links "$links" > /dev/null
echo "https://gitlab.com/$project/-/releases/$tag"
