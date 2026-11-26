/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

Spy mode (saving deleted messages and edit history) is modelled on the
feature of AyuGram Desktop (https://github.com/AyuGram/AyuGramDesktop),
licensed under GPLv3.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "noxgram/spy_db.h"
#include "ui/widgets/menu/menu_add_action_callback.h"

class HistoryItem;
class PeerData;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Nox::Spy {

[[nodiscard]] QString DeletedMark();

// Returns true when the item was kept (and marked) in place of being
// destroyed, the caller must not destroy it then.
[[nodiscard]] bool KeepDeleted(not_null<HistoryItem*> item);
[[nodiscard]] bool KeepExpired(not_null<HistoryItem*> item);

void RememberEdit(not_null<HistoryItem*> item, const QString &newText);

[[nodiscard]] float64 Opacity(not_null<const HistoryItem*> item);

[[nodiscard]] bool KeepsViewOnce();
void RememberViewOnce(not_null<HistoryItem*> item);
[[nodiscard]] bool IgnoresExpiredEdit(
	not_null<HistoryItem*> item,
	const MTPMessage &data);

[[nodiscard]] bool HasEdits(not_null<HistoryItem*> item);

void ShowEditHistory(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);
void ShowDeleted(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

void AddMessageActions(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	HistoryItem *item);

void AddPeerMenu(
	const Ui::Menu::MenuCallback &addAction,
	not_null<Window::SessionController*> controller,
	PeerData *peer);

} // namespace Nox::Spy
