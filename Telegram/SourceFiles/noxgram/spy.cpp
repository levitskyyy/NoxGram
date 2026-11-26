/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

Spy mode (saving deleted messages and edit history) is modelled on the
feature of AyuGram Desktop (https://github.com/AyuGram/AyuGramDesktop),
licensed under GPLv3.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "noxgram/spy.h"

#include "base/unixtime.h"
#include "core/file_utilities.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "noxgram/lang.h"
#include "noxgram/options.h"
#include "noxgram/spy_media.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QFileInfo>
#include <QtGui/QImageReader>

namespace Nox::Spy {
namespace {

constexpr auto kPage = 50;
constexpr auto kMaxTextLength = 64 * 1024;
constexpr auto kThumbnailHeight = 240;

[[nodiscard]] bool Get(const Option<bool> &option) {
	return Nagram::ForDevice().Get(option);
}

[[nodiscard]] bool IsBotMessage(not_null<HistoryItem*> item) {
	const auto user = item->from()->asUser();
	return (user && user->isBot()) || item->viaBot();
}

[[nodiscard]] Record MakeRecord(
		not_null<HistoryItem*> item,
		TextWithEntities text,
		TimeId date) {
	if (text.text.size() > kMaxTextLength) {
		text = TextWithEntities{ text.text.left(kMaxTextLength) };
	}
	return {
		.peer = item->history()->peer->id,
		.msg = item->id,
		.topic = item->topicRootId(),
		.grouped = item->groupId().raw(),
		.from = item->from()->id,
		.fromName = item->from()->name(),
		.date = date,
		.at = base::unixtime::now(),
		.text = std::move(text),
	};
}

[[nodiscard]] bool Savable(not_null<HistoryItem*> item) {
	if (!Get(kSaveDeleted)
		|| item->isLocal()
		|| !item->isRegular()
		|| item->isService()
		|| item->out()) {
		return false;
	}
	return !IsBotMessage(item) || Get(kSaveForBots);
}

void Keep(not_null<HistoryItem*> item, bool expired) {
	const auto history = item->history();
	auto record = MakeRecord(item, item->originalText(), item->date());
	record.expired = expired;
	record.media = KindOf(item);
	record.mediaName = MediaName(item);
	record.mediaMime = MediaMime(item);
	record.mediaSize = MediaSize(item);
	DatabaseFor(&history->session()).addDeleted(std::move(record));
	SaveMedia(item);
	if (!item->out()
		&& item->isRegular()
		&& item->id > history->inboxReadTillId()
		&& history->unreadCountKnown()
		&& history->unreadCount() > 0) {
		history->setUnreadCount(history->unreadCount() - 1);
	}
	item->setNoxDeleted();
}

[[nodiscard]] QString FormatDate(TimeId date) {
	return date
		? langDateTimeFull(base::unixtime::parse(date))
		: QString();
}

[[nodiscard]] QString KindText(MediaKind kind) {
	switch (kind) {
	case MediaKind::Photo: return tr::lng_in_dlg_photo(tr::now);
	case MediaKind::Video: return tr::lng_in_dlg_video(tr::now);
	case MediaKind::Gif: return u"GIF"_q;
	case MediaKind::Voice: return tr::lng_in_dlg_audio(tr::now);
	case MediaKind::Round: return tr::lng_in_dlg_video_message(tr::now);
	case MediaKind::Audio: return tr::lng_in_dlg_audio_file(tr::now);
	case MediaKind::File: return tr::lng_in_dlg_file(tr::now);
	case MediaKind::Sticker: return tr::lng_in_dlg_sticker(tr::now);
	case MediaKind::Other: return tr::lng_in_dlg_file(tr::now);
	case MediaKind::None: break;
	}
	return QString();
}

void AddThumbnail(
		not_null<Ui::VerticalLayout*> container,
		const QString &path) {
	auto reader = QImageReader(path);
	reader.setAutoTransform(true);
	const auto size = reader.size();
	if (!size.isValid() || size.isEmpty()) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto width = st::boxWideWidth
		- st::boxRowPadding.left()
		- st::boxRowPadding.right();
	const auto scaled = size.scaled(
		QSize(width, kThumbnailHeight) * ratio,
		Qt::KeepAspectRatio);
	reader.setScaledSize(scaled);
	auto image = reader.read();
	if (image.isNull()) {
		return;
	}
	image.setDevicePixelRatio(ratio);
	const auto pixmap = QPixmap::fromImage(std::move(image));
	const auto button = container->add(
		object_ptr<Ui::AbstractButton>(container),
		st::boxRowPadding);
	button->resize(scaled.width() / ratio, scaled.height() / ratio);
	button->setCursor(style::cur_pointer);
	button->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(button);
		p.drawPixmap(0, 0, pixmap);
	}, button->lifetime());
	button->setClickedCallback([=] {
		File::Launch(path);
	});
}

void AddMedia(
		not_null<Ui::VerticalLayout*> container,
		const Record &record,
		const QString &path) {
	if (record.media == MediaKind::None) {
		return;
	}
	const auto exists = !path.isEmpty() && QFileInfo::exists(path);
	if (exists && record.media == MediaKind::Photo) {
		AddThumbnail(container, path);
	}
	auto description = KindText(record.media);
	if (!record.mediaName.isEmpty()) {
		description += u", "_q + record.mediaName;
	}
	if (record.mediaSize > 0) {
		description += u", "_q + Ui::FormatSizeText(record.mediaSize);
	}
	if (!exists) {
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				description + u" — "_q
					+ Nox::TrNow(u"lng_nox_spy_media_not_saved"_q),
				st::boxDividerLabel),
			st::boxRowPadding);
		return;
	}
	const auto open = container->add(
		object_ptr<Ui::LinkButton>(
			container,
			Nox::TrNow(
				u"lng_nox_spy_media_open"_q,
				{ { u"media"_q, description } })),
		st::boxRowPadding);
	open->setClickedCallback([=] { File::Launch(path); });
	const auto show = container->add(
		object_ptr<Ui::LinkButton>(
			container,
			Nox::TrNow(u"lng_nox_spy_media_show"_q)),
		st::boxRowPadding);
	show->setClickedCallback([=] { File::ShowInFolder(path); });
}

void AddRecordRows(
		not_null<Ui::VerticalLayout*> container,
		const Record &record,
		const QString &header,
		const QString &mediaPath) {
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			rpl::single(Ui::Text::Bold(header)),
			st::boxDividerLabel),
		st::boxRowPadding);
	if (!record.text.text.isEmpty()
		|| record.media == MediaKind::None) {
		const auto label = container->add(
			object_ptr<Ui::FlatLabel>(container, st::boxLabel),
			st::boxRowPadding);
		if (record.text.text.isEmpty()) {
			label->setText(Nox::TrNow(u"lng_nox_spy_empty_text"_q));
		} else {
			label->setMarkedText(record.text);
		}
		label->setSelectable(true);
	}
	AddMedia(container, record, mediaPath);
	Ui::AddSkip(container);
}

void EditHistoryBox(
		not_null<Ui::GenericBox*> box,
		std::vector<Record> versions,
		TextWithEntities current,
		TimeId currentDate) {
	box->setTitle(Nox::Tr(u"lng_nox_spy_edit_history"_q));
	box->setWidth(st::boxWideWidth);
	const auto container = box->verticalLayout();
	for (const auto &version : versions) {
		AddRecordRows(container, version, FormatDate(version.date), {});
	}
	AddRecordRows(
		container,
		Record{ .text = std::move(current) },
		Nox::TrNow(u"lng_nox_spy_current_version"_q)
			+ (currentDate ? (u", "_q + FormatDate(currentDate)) : QString()),
		{});
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

[[nodiscard]] QString DeletedHeader(const Record &record) {
	auto result = record.fromName.isEmpty()
		? QString()
		: (record.fromName + u", "_q);
	result += FormatDate(record.date);
	if (record.at) {
		result += u" → "_q + Nox::TrNow(
			record.expired
				? u"lng_nox_spy_expired_at"_q
				: u"lng_nox_spy_deleted_at"_q,
			{ { u"date"_q, FormatDate(record.at) } });
	}
	return result;
}

void DeletedBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer) {
	box->setTitle(Nox::Tr(u"lng_nox_spy_deleted_messages"_q));
	box->setWidth(st::boxWideWidth);
	const auto session = &peer->session();
	auto &database = DatabaseFor(session);
	const auto container = box->verticalLayout();
	const auto list = container->add(
		object_ptr<Ui::VerticalLayout>(container));
	struct State {
		int64 before = 0;
		Ui::LinkButton *more = nullptr;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto load = [=] {
		auto &database = DatabaseFor(session);
		const auto page = database.deleted(peer->id, state->before, kPage);
		for (const auto &record : page) {
			AddRecordRows(
				list,
				record,
				DeletedHeader(record),
				database.absolute(record.mediaPath));
			state->before = record.rowId;
		}
		if (state->more && int(page.size()) < kPage) {
			delete state->more;
			state->more = nullptr;
		}
		return int(page.size());
	};
	if (!database.hasDeleted(peer->id)) {
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				Nox::Tr(u"lng_nox_spy_deleted_none"_q),
				st::boxLabel),
			st::boxRowPadding);
	} else {
		if (load() == kPage) {
			state->more = container->add(
				object_ptr<Ui::LinkButton>(
					container,
					Nox::TrNow(u"lng_nox_spy_load_more"_q)),
				st::boxRowPadding);
			state->more->setClickedCallback([=] {
				load();
			});
		}
		box->addLeftButton(Nox::Tr(u"lng_nox_spy_clear"_q), [=] {
			DatabaseFor(session).clearDeleted(peer->id);
			box->closeBox();
		});
	}
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace

QString DeletedMark() {
	return Nox::TrNow(u"lng_nox_deleted_mark"_q);
}

bool KeepDeleted(not_null<HistoryItem*> item) {
	if (item->isNoxDeleted()) {
		return true;
	} else if (!Savable(item)) {
		return false;
	}
	Keep(item, false);
	return true;
}

bool KeepExpired(not_null<HistoryItem*> item) {
	if (item->isNoxDeleted()) {
		return true;
	} else if (!Get(kKeepSelfDestructing) || !Savable(item)) {
		return false;
	}
	Keep(item, true);
	return true;
}

void RememberEdit(not_null<HistoryItem*> item, const QString &newText) {
	if (!Get(kSaveEdits)
		|| item->isLocal()
		|| !item->isRegular()
		|| item->out()
		|| (IsBotMessage(item) && !Get(kSaveForBots))) {
		return;
	}
	const auto &old = item->originalText();
	if (old.text.isEmpty() || old.text == newText) {
		return;
	}
	const auto edited = item->Get<HistoryMessageEdited>();
	const auto date = (edited && edited->date) ? edited->date : item->date();
	DatabaseFor(&item->history()->session()).addEdit(
		MakeRecord(item, old, date));
}

bool KeepsViewOnce() {
	return Get(kKeepViewOnce);
}

void RememberViewOnce(not_null<HistoryItem*> item) {
	if (!item->isRegular() || item->out()) {
		return;
	}
	auto record = MakeRecord(item, item->originalText(), item->date());
	record.expired = true;
	record.media = KindOf(item);
	record.mediaName = MediaName(item);
	record.mediaMime = MediaMime(item);
	record.mediaSize = MediaSize(item);
	DatabaseFor(&item->history()->session()).addDeleted(std::move(record));
	SaveMedia(item);
}

bool IgnoresExpiredEdit(
		not_null<HistoryItem*> item,
		const MTPMessage &data) {
	const auto media = item->media();
	if (!KeepsViewOnce()
		|| item->out()
		|| !media
		|| !media->ttlSeconds()
		|| data.type() != mtpc_message) {
		return false;
	}
	const auto &fields = data.c_message();
	const auto incoming = fields.vmedia();
	if (!incoming) {
		return true;
	}
	return incoming->match([](const MTPDmessageMediaPhoto &data) {
		return !data.vphoto().has_value();
	}, [](const MTPDmessageMediaDocument &data) {
		return !data.vdocument().has_value();
	}, [](const auto &) {
		return true;
	});
}

bool HasEdits(not_null<HistoryItem*> item) {
	return DatabaseFor(&item->history()->session()).hasEdits(
		item->history()->peer->id,
		item->id);
}

void ShowEditHistory(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto edited = item->Get<HistoryMessageEdited>();
	controller->show(Box(
		EditHistoryBox,
		DatabaseFor(&item->history()->session()).edits(
			item->history()->peer->id,
			item->id),
		item->originalText(),
		edited ? edited->date : item->date()));
}

void ShowDeleted(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Box(DeletedBox, peer));
}

void AddMessageActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		HistoryItem *item) {
	if (!item || !item->isRegular() || !HasEdits(item)) {
		return;
	}
	const auto itemId = item->fullId();
	const auto session = &item->history()->session();
	menu->addAction(Nox::TrNow(u"lng_nox_spy_edit_history"_q), crl::guard(
		controller,
		[=] {
			if (const auto item = session->data().message(itemId)) {
				ShowEditHistory(controller, item);
			}
		}), &st::menuIconEdit);
}

void AddPeerMenu(
		const Ui::Menu::MenuCallback &addAction,
		not_null<Window::SessionController*> controller,
		PeerData *peer) {
	if (!peer || !DatabaseFor(&peer->session()).hasDeleted(peer->id)) {
		return;
	}
	addAction(
		Nox::TrNow(u"lng_nox_spy_deleted_messages"_q),
		crl::guard(controller, [=] { ShowDeleted(controller, peer); }),
		&st::menuIconRestore);
}

} // namespace Nox::Spy
