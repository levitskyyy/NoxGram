/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

Spy mode (saving deleted messages and edit history) is modelled on the
feature of AyuGram Desktop (https://github.com/AyuGram/AyuGramDesktop),
licensed under GPLv3.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

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

struct Record {
	PeerId peer = 0;
	MsgId msg = 0;
	PeerId from = 0;
	QString fromName;
	TimeId date = 0; // When the message (or this version) was sent.
	TimeId at = 0; // When it was deleted or replaced by an edit.
	QString text;
};

[[nodiscard]] QString DeletedMark();

// Deleted messages. Returns true when the item was kept (and marked) in
// place of being destroyed, the caller must not destroy it then.
[[nodiscard]] bool KeepDeleted(not_null<HistoryItem*> item);

// Self-destructing (TTL) messages that ran out.
[[nodiscard]] bool KeepExpired(not_null<HistoryItem*> item);

// Called before an edit from the server is applied.
void RememberEdit(not_null<HistoryItem*> item, const QString &newText);

[[nodiscard]] bool HasEdits(not_null<HistoryItem*> item);
[[nodiscard]] std::vector<Record> Edits(not_null<HistoryItem*> item);
[[nodiscard]] std::vector<Record> Deleted(not_null<PeerData*> peer);
void ClearDeleted(not_null<PeerData*> peer);

void ShowEditHistory(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);
void ShowDeleted(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

// Message context menu: "Edit history".
void AddMessageActions(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	HistoryItem *item);

// Chat menu: "Deleted messages".
void AddPeerMenu(
	const Ui::Menu::MenuCallback &addAction,
	not_null<Window::SessionController*> controller,
	PeerData *peer);

} // namespace Nox::Spy
