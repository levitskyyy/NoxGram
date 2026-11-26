/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

Forwarding from chats that restrict it is modelled on the feature of
AyuGram Desktop (https://github.com/AyuGram/AyuGramDesktop), licensed
under GPLv3.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"

class HistoryItem;

namespace Api {
struct SendAction;
} // namespace Api

namespace Nox::Forward {

[[nodiscard]] bool Enabled();

// The server refuses to forward the item, but its copy can be sent.
[[nodiscard]] bool NeedsReupload(not_null<const HistoryItem*> item);
[[nodiscard]] bool CanReupload(not_null<const HistoryItem*> item);

// Downloads the media of the items and sends them again as new messages,
// one after another, albums kept together.
void Send(
	const HistoryItemsList &items,
	const Api::SendAction &action,
	Data::ForwardOptions options);

} // namespace Nox::Forward
