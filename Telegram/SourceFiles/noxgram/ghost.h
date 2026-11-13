/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

Ghost mode is modelled on the feature of AyuGram Desktop
(https://github.com/AyuGram/AyuGramDesktop), licensed under GPLv3.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

class History;
class HistoryItem;

namespace Api {
struct SendOptions;
} // namespace Api

namespace Main {
class Session;
} // namespace Main

namespace Nox::Ghost {

// Master switch.
[[nodiscard]] bool Active();
[[nodiscard]] rpl::producer<bool> ActiveValue();
void SetActive(bool active);
void Toggle();

// What the client is still allowed to send to the server.
[[nodiscard]] bool SendReadMessages();
[[nodiscard]] bool SendReadStories();
[[nodiscard]] bool SendOnline();
[[nodiscard]] bool SendTyping();

// After an action that the server treats as activity (sending a message,
// reacting, voting) it marks the account online. Ghost mode answers with
// an explicit offline packet a moment later.
void NoteActivity(not_null<Main::Session*> session);

// When ghost mode hides read receipts, replying to a chat still reads it,
// so the reply does not look odd next to unread messages.
void MarkReadAfterAction(not_null<History*> history);

// Sends a message as scheduled a few seconds ahead, so sending it does not
// make the account appear online.
void ApplyScheduling(
	not_null<Main::Session*> session,
	Api::SendOptions &options,
	int delaySeconds = 12);

} // namespace Nox::Ghost
