/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

The message and chat actions are modelled on the ones of AyuGram Desktop
(https://github.com/AyuGram/AyuGramDesktop), licensed under GPLv3.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "noxgram/menu.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_histories.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "data/data_peer_id.h"
#include "nagram/chats/top_bar_actions.h"
#include "nagram/core/options.h"
#include "nagram/filters/model.h"
#include "noxgram/ghost.h"
#include "noxgram/lang.h"
#include "noxgram/spy.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Nox::Menu {
namespace {

constexpr auto kSearchLimit = 100;
constexpr auto kDeleteChunk = 100;

[[nodiscard]] QVector<MTPint> CollectIds(const MTPmessages_Messages &result) {
	auto ids = QVector<MTPint>();
	const auto collect = [&](const QVector<MTPMessage> &messages) {
		for (const auto &message : messages) {
			message.match([&](const MTPDmessage &data) {
				if (data.is_out()) {
					ids.push_back(data.vid());
				}
			}, [&](const MTPDmessageService &data) {
				if (data.is_out()) {
					ids.push_back(data.vid());
				}
			}, [](const MTPDmessageEmpty &) {
			});
		}
	};
	result.match([&](const MTPDmessages_messagesNotModified &) {
	}, [&](const auto &data) {
		collect(data.vmessages().v);
	});
	return ids;
}

class MyMessagesRemover final
	: public std::enable_shared_from_this<MyMessagesRemover> {
public:
	MyMessagesRemover(
		not_null<History*> history,
		base::weak_ptr<Window::SessionController> controller)
	: _history(history)
	, _controller(controller) {
	}

	void start() {
		request(0);
	}

private:
	void request(MsgId offsetId) {
		const auto session = &_history->session();
		const auto self = shared_from_this();
		session->api().request(MTPmessages_Search(
			MTP_flags(MTPmessages_Search::Flag::f_from_id),
			_history->peer->input(),
			MTP_string(),
			MTP_inputPeerSelf(),
			MTP_inputPeerEmpty(),
			MTPVector<MTPReaction>(),
			MTP_int(0), // top_msg_id
			MTP_inputMessagesFilterEmpty(),
			MTP_int(0), // min_date
			MTP_int(0), // max_date
			MTP_int(offsetId.bare),
			MTP_int(0), // add_offset
			MTP_int(kSearchLimit),
			MTP_int(0), // max_id
			MTP_int(0), // min_id
			MTP_long(0) // hash
		)).done([=](const MTPmessages_Messages &result) {
			self->found(CollectIds(result));
		}).fail([=] {
			self->finish();
		}).send();
	}

	void found(QVector<MTPint> ids) {
		if (ids.isEmpty()) {
			finish();
			return;
		}
		auto lowest = MsgId(ids.front().v);
		for (const auto &id : ids) {
			lowest = std::min(lowest, MsgId(id.v));
		}
		_deleted += int(ids.size());
		auto &histories = _history->owner().histories();
		for (auto i = 0; i < ids.size(); i += kDeleteChunk) {
			histories.deleteMessages(
				_history,
				ids.mid(i, kDeleteChunk),
				true);
		}
		request(lowest);
	}

	void finish() {
		if (const auto strong = _controller.get()) {
			strong->showToast(Nox::TrNow(
				u"lng_nox_delete_mine_done"_q,
				{ { u"count"_q, QString::number(_deleted) } }));
		}
	}

	const not_null<History*> _history;
	const base::weak_ptr<Window::SessionController> _controller;
	int _deleted = 0;

};

void AddReadUntil(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item) {
	const auto history = item->history();
	if (Ghost::SendReadMessages()
		|| item->out()
		|| !item->isRegular()
		|| item->id <= history->inboxReadTillId()) {
		return;
	}
	const auto itemId = item->fullId();
	const auto session = &history->session();
	menu->addAction(Nox::TrNow(u"lng_nox_read_until"_q), [=] {
		if (const auto item = session->data().message(itemId)) {
			Ghost::ReadUntil(item);
		}
	}, &st::menuIconMarkRead);
}

void AddBurn(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media
		|| !media->ttlSeconds()
		|| !item->isIncomingUnreadMedia()
		|| !item->isRegular()) {
		return;
	}
	const auto itemId = item->fullId();
	const auto session = &item->history()->session();
	menu->addAction(Nox::TrNow(u"lng_nox_burn"_q), [=] {
		if (const auto item = session->data().message(itemId)) {
			Ghost::ReadContents(item);
		}
	}, &st::menuIconTimer);
}

void AddJumpToBeginning(
		const Ui::Menu::MenuCallback &addAction,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	const auto history = peer->owner().history(peer);
	addAction(Nox::TrNow(u"lng_nox_jump_to_beginning"_q), crl::guard(
		controller,
		[=] {
			Nagram::Chats::RunTopBarAction(
				Nagram::Chats::TopBarAction::JumpToStart,
				controller,
				history);
		}), &st::menuIconShowInChat);
}

void AddOpenLinkedChannel(
		const Ui::Menu::MenuCallback &addAction,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	const auto group = peer->asMegagroup();
	const auto channel = group ? group->discussionLink() : nullptr;
	if (!channel) {
		return;
	}
	addAction(Nox::TrNow(u"lng_nox_open_linked_channel"_q), crl::guard(
		controller,
		[=] {
			controller->showPeerHistory(
				channel,
				Window::SectionShow::Way::Forward);
		}), &st::menuIconChannel);
}

void AddDeleteMine(
		const Ui::Menu::MenuCallback &addAction,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	if (!peer->isChat() && !peer->isMegagroup()) {
		return;
	}
	const auto history = peer->owner().history(peer);
	addAction({
		.text = Nox::TrNow(u"lng_nox_delete_mine"_q),
		.handler = crl::guard(controller, [=] {
			const auto weak = base::make_weak(controller);
			controller->show(Ui::MakeConfirmBox({
				.text = Nox::TrNow(u"lng_nox_delete_mine_sure"_q),
				.confirmed = [=](Fn<void()> close) {
					std::make_shared<MyMessagesRemover>(
						history,
						weak)->start();
					close();
				},
				.confirmText = tr::lng_box_delete(),
				.confirmStyle = &st::attentionBoxButton,
			}));
		}),
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
}

[[nodiscard]] QJsonObject FilterRules(not_null<Main::Session*> session) {
	const auto raw = Nagram::ForAccount(session).Get(Nagram::Filters::kRules);
	return (raw.isEmpty() || !Nagram::Filters::Validate(raw))
		? Nagram::Filters::Defaults()
		: QJsonDocument::fromJson(raw).object();
}

[[nodiscard]] bool ShadowBanned(not_null<PeerData*> peer) {
	const auto author = QString::number(SerializePeerId(peer->id));
	return FilterRules(&peer->session()).value(
		u"hiddenAuthors"_q).toArray().contains(author);
}

void AddShadowBan(
		const Ui::Menu::MenuCallback &addAction,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	if (peer->isSelf() || peer->isChat() || peer->isMegagroup()) {
		return;
	}
	const auto banned = ShadowBanned(peer);
	addAction(Nox::TrNow(banned
		? u"lng_nox_shadow_unban"_q
		: u"lng_nox_shadow_ban"_q), crl::guard(controller, [=] {
		const auto session = &peer->session();
		const auto author = QString::number(SerializePeerId(peer->id));
		auto rules = FilterRules(session);
		auto authors = rules.value(u"hiddenAuthors"_q).toArray();
		const auto index = authors.toVariantList().indexOf(author);
		if (index >= 0) {
			authors.removeAt(index);
		} else {
			authors.push_back(author);
			rules.insert(u"enabled"_q, true);
		}
		rules.insert(u"hiddenAuthors"_q, authors);
		const auto bytes = QJsonDocument(rules).toJson(
			QJsonDocument::Compact);
		if (Nagram::ForAccount(session).Set(
				Nagram::Filters::kRules,
				bytes)) {
			controller->showToast(Nox::TrNow((index >= 0)
				? u"lng_nox_shadow_unbanned"_q
				: u"lng_nox_shadow_banned"_q));
		}
	}), &st::menuIconBlock);
}

} // namespace

void AddMessageActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		HistoryItem *item) {
	if (!item) {
		return;
	}
	Spy::AddMessageActions(menu, controller, item);
	AddReadUntil(menu, item);
	AddBurn(menu, item);
}

void AddPeerActions(
		const Ui::Menu::MenuCallback &addAction,
		not_null<Window::SessionController*> controller,
		PeerData *peer) {
	if (!peer) {
		return;
	}
	AddJumpToBeginning(addAction, controller, peer);
	AddOpenLinkedChannel(addAction, controller, peer);
	Spy::AddPeerMenu(addAction, controller, peer);
	AddShadowBan(addAction, controller, peer);
	AddDeleteMine(addAction, controller, peer);
}

} // namespace Nox::Menu
