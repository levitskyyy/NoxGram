/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "noxgram/spy_db.h"

class HistoryItem;

namespace Nox::Spy {

[[nodiscard]] MediaKind KindOf(not_null<HistoryItem*> item);
[[nodiscard]] QString MediaName(not_null<HistoryItem*> item);
[[nodiscard]] QString MediaMime(not_null<HistoryItem*> item);
[[nodiscard]] int64 MediaSize(not_null<HistoryItem*> item);
[[nodiscard]] int64 MediaLimitBytes(int index);

// Calls done once the photo or file of the item is in target.
bool FetchMedia(
	not_null<HistoryItem*> item,
	const QString &target,
	Fn<void(bool)> done);
[[nodiscard]] QString MediaExtension(not_null<HistoryItem*> item);

// Copies the photo or file of a deleted message into the spy folder,
// from memory, the cache or the downloads, or loads it if it is not there.
void SaveMedia(not_null<HistoryItem*> item);

} // namespace Nox::Spy
