/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

Ghost mode is modelled on the feature of AyuGram Desktop
(https://github.com/AyuGram/AyuGramDesktop), licensed under GPLv3.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "noxgram/ghost.h"

#include "noxgram/options.h"
#include "api/api_common.h"
#include "api/api_updates.h"
#include "apiwrap.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"

namespace Nox::Ghost {
namespace {

constexpr auto kOfflineDelay = crl::time(3000);

[[nodiscard]] bool Get(const Option<bool> &option) {
	return Nagram::ForDevice().Get(option);
}

[[nodiscard]] bool Hides(const Option<bool> &option) {
	return Active() && Get(option);
}

class OfflineSender final {
public:
	OfflineSender() : _timer([=] { send(); }) {
	}

	void note(not_null<Main::Session*> session) {
		_pending.emplace(session->userId().bare);
		_timer.callOnce(kOfflineDelay);
	}

private:
	void send() {
		if (!Hides(kGhostOfflineAfterOnline) || Core::Quitting()) {
			_pending.clear();
			return;
		}
		for (const auto &[index, account] : Core::App().domain().accounts()) {
			const auto session = account->maybeSession();
			if (!session || !_pending.contains(session->userId().bare)) {
				continue;
			}
			session->api().request(MTPaccount_UpdateStatus(
				MTP_bool(true)
			)).send();
		}
		_pending.clear();
	}

	base::flat_set<uint64> _pending;
	base::Timer _timer;

};

OfflineSender &Offline() {
	static auto result = OfflineSender();
	return result;
}

void ReadTill(not_null<History*> history, MsgId tillId) {
	const auto session = &history->session();
	history->owner().histories().sendRequest(
		history,
		Data::Histories::RequestType::ReadInbox,
		[=](Fn<void()> finish) {
			const auto done = [=] {
				NoteActivity(session);
				finish();
			};
			if (const auto channel = history->peer->asChannel()) {
				return session->api().request(MTPchannels_ReadHistory(
					channel->inputChannel(),
					MTP_int(tillId)
				)).done(done).fail(finish).send();
			}
			return session->api().request(MTPmessages_ReadHistory(
				history->peer->input(),
				MTP_int(tillId)
			)).done([=](const MTPmessages_AffectedMessages &result) {
				session->api().applyAffectedMessages(history->peer, result);
				done();
			}).fail(finish).send();
		});
}

} // namespace

bool Active() {
	return Get(kGhostMode);
}

rpl::producer<bool> ActiveValue() {
	return Nagram::ForDevice().Value(kGhostMode);
}

void SetActive(bool active) {
	Expects(Nagram::ForDevice().Set(kGhostMode, active));
	{
		// Report the new online state right away in both directions.
		for (const auto &[index, account] : Core::App().domain().accounts()) {
			if (const auto session = account->maybeSession()) {
				session->updates().updateOnline();
			}
		}
	}
}

void Toggle() {
	SetActive(!Active());
}

bool SendReadMessages() {
	return !Hides(kGhostHideRead);
}

bool SendReadStories() {
	return !Hides(kGhostHideStories);
}

bool SendOnline() {
	return !Hides(kGhostHideOnline);
}

bool SendTyping() {
	return !Hides(kGhostHideTyping);
}

bool PromptBeforeStory(not_null<PeerData*> peer, bool own) {
	return !own
		&& !peer->isSelf()
		&& !Active()
		&& Get(kGhostHideStories)
		&& Get(kGhostStoryPrompt);
}

bool InvertSilent() {
	const auto mode = Nagram::ForDevice().Get(kSendWithoutSound);
	return (mode == 2) || (mode == 1 && Active());
}

void NoteActivity(not_null<Main::Session*> session) {
	if (Hides(kGhostOfflineAfterOnline)) {
		Offline().note(session);
	}
}

void MarkReadAfterAction(not_null<History*> history) {
	if (SendReadMessages() || !Get(kGhostReadOnAction)) {
		NoteActivity(&history->session());
		return;
	}
	if (const auto last = history->lastServerMessage()) {
		if (!history->unreadCountKnown()
			|| history->unreadCount() > 0
			|| history->unreadMark()) {
			// Update the local state first, the request below only
			// tells the server.
			history->inboxRead(last);
			ReadTill(history, last->id);
			return;
		}
	}
	NoteActivity(&history->session());
}

void ReadUntil(not_null<HistoryItem*> item) {
	const auto history = item->history();
	history->inboxRead(item);
	ReadTill(history, item->id);
}

void ReadContents(not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	const auto ids = MTP_vector<MTPint>(1, MTP_int(item->id));
	if (const auto channel = item->history()->peer->asChannel()) {
		session->api().request(MTPchannels_ReadMessageContents(
			channel->inputChannel(),
			ids
		)).send();
	} else {
		session->api().request(MTPmessages_ReadMessageContents(
			ids
		)).done([=, peer = item->history()->peer](
				const MTPmessages_AffectedMessages &result) {
			session->api().applyAffectedMessages(peer, result);
		}).send();
	}
	item->markContentsRead();
	NoteActivity(session);
}

void ApplyScheduling(
		not_null<Main::Session*> session,
		Api::SendOptions &options,
		int delaySeconds) {
	if (!Hides(kGhostScheduleSend)
		|| options.scheduled
		|| options.shortcutId
		|| options.welcomeTemplate
		|| options.starsApproved
		|| options.price) {
		return;
	}
	// Proxies add latency; give the server a bit more time.
	const auto delay = Core::App().settings().proxy().isEnabled()
		? (delaySeconds * 6 + 4) / 5
		: delaySeconds;
	options.scheduled = base::unixtime::now() + delay;
	options.noxGhostScheduled = true;
}

} // namespace Nox::Ghost
