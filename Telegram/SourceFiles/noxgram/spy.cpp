/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

Spy mode (saving deleted messages and edit history) is modelled on the
feature of AyuGram Desktop (https://github.com/AyuGram/AyuGramDesktop),
licensed under GPLv3.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "noxgram/spy.h"

#include "noxgram/options.h"
#include "base/unixtime.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QSaveFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Nox::Spy {
namespace {

// Keep the files and the boxes bounded.
constexpr auto kMaxShown = 500;
constexpr auto kMaxTextLength = 16 * 1024;

[[nodiscard]] bool Get(const Option<bool> &option) {
	return Nagram::ForDevice().Get(option);
}

[[nodiscard]] QString FromName(not_null<HistoryItem*> item) {
	return item->from()->name();
}

[[nodiscard]] Record MakeRecord(
		not_null<HistoryItem*> item,
		const QString &text,
		TimeId date) {
	return {
		.peer = item->history()->peer->id,
		.msg = item->id,
		.from = item->from()->id,
		.fromName = FromName(item),
		.date = date,
		.at = base::unixtime::now(),
		.text = text.left(kMaxTextLength),
	};
}

[[nodiscard]] QJsonObject Serialize(const Record &record) {
	auto result = QJsonObject();
	result.insert(u"peer"_q, QString::number(record.peer.value));
	result.insert(u"msg"_q, QString::number(record.msg.bare));
	result.insert(u"from"_q, QString::number(record.from.value));
	result.insert(u"name"_q, record.fromName);
	result.insert(u"date"_q, double(record.date));
	result.insert(u"at"_q, double(record.at));
	result.insert(u"text"_q, record.text);
	return result;
}

[[nodiscard]] std::optional<Record> Parse(const QJsonObject &object) {
	auto ok1 = false, ok2 = false, ok3 = false;
	const auto peer = object.value(u"peer"_q).toString().toULongLong(&ok1);
	const auto msg = object.value(u"msg"_q).toString().toLongLong(&ok2);
	const auto from = object.value(u"from"_q).toString().toULongLong(&ok3);
	if (!ok1 || !ok2 || !peer) {
		return std::nullopt;
	}
	return Record{
		.peer = PeerId(PeerIdHelper(peer)),
		.msg = MsgId(msg),
		.from = ok3 ? PeerId(PeerIdHelper(from)) : PeerId(),
		.fromName = object.value(u"name"_q).toString(),
		.date = TimeId(object.value(u"date"_q).toDouble()),
		.at = TimeId(object.value(u"at"_q).toDouble()),
		.text = object.value(u"text"_q).toString(),
	};
}

// One append-only JSON lines file per kind of record and account. The data
// lives next to the other account data in tdata, like AyuGram's database.
class Store final {
public:
	explicit Store(uint64 userId)
	: _folder(cWorkingDir() + u"tdata/noxgram/"_q + QString::number(userId)) {
	}

	void addDeleted(Record record) {
		load();
		const auto key = std::make_pair(record.peer, record.msg);
		if (!_deletedKeys.emplace(key).second) {
			return;
		}
		append(u"deleted.jsonl"_q, record);
		_deleted[record.peer].push_back(std::move(record));
	}

	void addEdit(Record record) {
		load();
		append(u"edits.jsonl"_q, record);
		const auto key = std::make_pair(record.peer, record.msg);
		_edits[key].push_back(std::move(record));
	}

	[[nodiscard]] const std::vector<Record> &deleted(PeerId peer) {
		load();
		static const auto empty = std::vector<Record>();
		const auto i = _deleted.find(peer);
		return (i != end(_deleted)) ? i->second : empty;
	}

	[[nodiscard]] const std::vector<Record> &edits(PeerId peer, MsgId msg) {
		load();
		static const auto empty = std::vector<Record>();
		const auto i = _edits.find(std::make_pair(peer, msg));
		return (i != end(_edits)) ? i->second : empty;
	}

	void clearDeleted(PeerId peer) {
		load();
		_deleted.remove(peer);
		auto keys = base::flat_set<std::pair<PeerId, MsgId>>();
		for (const auto &key : _deletedKeys) {
			if (key.first != peer) {
				keys.emplace(key);
			}
		}
		_deletedKeys = std::move(keys);
		rewriteDeleted();
	}

private:
	[[nodiscard]] QString path(const QString &name) const {
		return _folder + '/' + name;
	}

	void load() {
		if (_loaded) {
			return;
		}
		_loaded = true;
		read(u"deleted.jsonl"_q, [&](Record record) {
			const auto key = std::make_pair(record.peer, record.msg);
			if (_deletedKeys.emplace(key).second) {
				_deleted[record.peer].push_back(std::move(record));
			}
		});
		read(u"edits.jsonl"_q, [&](Record record) {
			const auto key = std::make_pair(record.peer, record.msg);
			_edits[key].push_back(std::move(record));
		});
	}

	void read(const QString &name, Fn<void(Record)> callback) {
		auto file = QFile(path(name));
		if (!file.open(QIODevice::ReadOnly)) {
			return;
		}
		while (!file.atEnd()) {
			const auto line = file.readLine().trimmed();
			if (line.isEmpty()) {
				continue;
			}
			const auto document = QJsonDocument::fromJson(line);
			if (!document.isObject()) {
				continue;
			} else if (auto record = Parse(document.object())) {
				callback(std::move(*record));
			}
		}
	}

	[[nodiscard]] bool ensureFolder() const {
		return QDir().mkpath(_folder);
	}

	void append(const QString &name, const Record &record) {
		if (!ensureFolder()) {
			return;
		}
		auto file = QFile(path(name));
		if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
			return;
		}
		file.write(QJsonDocument(Serialize(record)).toJson(
			QJsonDocument::Compact));
		file.write("\n");
	}

	void rewriteDeleted() {
		if (!ensureFolder()) {
			return;
		}
		// Written to a temporary file and swapped in, so a crash can't
		// lose the records of other chats.
		auto file = QSaveFile(path(u"deleted.jsonl"_q));
		if (!file.open(QIODevice::WriteOnly)) {
			return;
		}
		for (const auto &[peer, list] : _deleted) {
			for (const auto &record : list) {
				file.write(QJsonDocument(Serialize(record)).toJson(
					QJsonDocument::Compact));
				file.write("\n");
			}
		}
		file.commit();
	}

	const QString _folder;
	bool _loaded = false;
	base::flat_map<PeerId, std::vector<Record>> _deleted;
	base::flat_set<std::pair<PeerId, MsgId>> _deletedKeys;
	base::flat_map<std::pair<PeerId, MsgId>, std::vector<Record>> _edits;

};

[[nodiscard]] Store &StoreFor(not_null<Main::Session*> session) {
	static auto stores = base::flat_map<uint64, std::unique_ptr<Store>>();
	const auto id = session->userId().bare;
	auto i = stores.find(id);
	if (i == end(stores)) {
		i = stores.emplace(id, std::make_unique<Store>(id)).first;
	}
	return *i->second;
}

[[nodiscard]] bool Savable(not_null<HistoryItem*> item) {
	if (!Get(kSaveDeleted)
		|| item->isLocal()
		|| !item->isRegular()
		|| item->isService()
		|| item->out()) {
		return false;
	} else if (const auto user = item->history()->peer->asUser()) {
		if (user->isBot() && !Get(kSaveForBots)) {
			return false;
		}
	}
	return true;
}

void Keep(not_null<HistoryItem*> item) {
	const auto history = item->history();
	StoreFor(&history->session()).addDeleted(MakeRecord(
		item,
		item->notificationText().text,
		item->date()));
	// The server already forgot this message, so it can't stay unread.
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

void AddRecordRows(
		not_null<Ui::GenericBox*> box,
		const Record &record,
		const QString &header) {
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(Ui::Text::Bold(header)),
		st::boxDividerLabel));
	const auto text = record.text.isEmpty()
		? tr::lng_nox_spy_empty_text(tr::now)
		: record.text;
	const auto label = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		text,
		st::boxLabel));
	label->setSelectable(true);
	Ui::AddSkip(box->verticalLayout());
}

void EditHistoryBox(
		not_null<Ui::GenericBox*> box,
		std::vector<Record> versions,
		QString current,
		TimeId currentDate) {
	box->setTitle(tr::lng_nox_spy_edit_history());
	box->setWidth(st::boxWideWidth);
	const auto count = int(versions.size());
	const auto shown = std::min(count, kMaxShown);
	for (auto i = count - shown; i != count; ++i) {
		AddRecordRows(box, versions[i], FormatDate(versions[i].date));
	}
	AddRecordRows(
		box,
		Record{ .text = current },
		tr::lng_nox_spy_current_version(tr::now)
			+ (currentDate ? (u", "_q + FormatDate(currentDate)) : QString()));
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

void DeletedBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer) {
	box->setTitle(tr::lng_nox_spy_deleted_messages());
	box->setWidth(st::boxWideWidth);
	const auto list = Deleted(peer);
	if (list.empty()) {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_nox_spy_deleted_none(),
			st::boxLabel));
	}
	const auto count = int(list.size());
	const auto shown = std::min(count, kMaxShown);
	for (auto i = count - shown; i != count; ++i) {
		const auto &record = list[i];
		const auto header = (record.fromName.isEmpty()
			? QString()
			: (record.fromName + u", "_q))
			+ FormatDate(record.date)
			+ (record.at
				? (u" → "_q + tr::lng_nox_spy_deleted_at(
					tr::now,
					lt_date,
					FormatDate(record.at)))
				: QString());
		AddRecordRows(box, record, header);
	}
	if (!list.empty()) {
		box->addLeftButton(tr::lng_nox_spy_clear(), [=] {
			ClearDeleted(peer);
			box->closeBox();
		});
	}
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace

QString DeletedMark() {
	return tr::lng_nox_deleted_mark(tr::now);
}

bool KeepDeleted(not_null<HistoryItem*> item) {
	if (item->isNoxDeleted()) {
		return true;
	} else if (!Savable(item)) {
		return false;
	}
	Keep(item);
	return true;
}

bool KeepExpired(not_null<HistoryItem*> item) {
	if (item->isNoxDeleted()) {
		return true;
	} else if (!Get(kKeepSelfDestructing) || !Savable(item)) {
		return false;
	}
	Keep(item);
	return true;
}

void RememberEdit(not_null<HistoryItem*> item, const QString &newText) {
	if (!Get(kSaveEdits)
		|| item->isLocal()
		|| !item->isRegular()
		|| item->out()) {
		return;
	}
	const auto &old = item->originalText().text;
	if (old.isEmpty() || old == newText) {
		return;
	}
	const auto edited = item->Get<HistoryMessageEdited>();
	const auto date = (edited && edited->date) ? edited->date : item->date();
	StoreFor(&item->history()->session()).addEdit(
		MakeRecord(item, old, date));
}

bool HasEdits(not_null<HistoryItem*> item) {
	return !StoreFor(&item->history()->session()).edits(
		item->history()->peer->id,
		item->id).empty();
}

std::vector<Record> Edits(not_null<HistoryItem*> item) {
	return StoreFor(&item->history()->session()).edits(
		item->history()->peer->id,
		item->id);
}

std::vector<Record> Deleted(not_null<PeerData*> peer) {
	return StoreFor(&peer->session()).deleted(peer->id);
}

void ClearDeleted(not_null<PeerData*> peer) {
	StoreFor(&peer->session()).clearDeleted(peer->id);
}

void ShowEditHistory(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto edited = item->Get<HistoryMessageEdited>();
	controller->show(Box(
		EditHistoryBox,
		Edits(item),
		item->originalText().text,
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
	if (!item || !HasEdits(item)) {
		return;
	}
	const auto itemId = item->fullId();
	const auto session = &item->history()->session();
	menu->addAction(tr::lng_nox_spy_edit_history(tr::now), crl::guard(
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
	if (!peer || StoreFor(&peer->session()).deleted(peer->id).empty()) {
		return;
	}
	addAction(
		tr::lng_nox_spy_deleted_messages(tr::now),
		crl::guard(controller, [=] { ShowDeleted(controller, peer); }),
		&st::menuIconRestore);
}

} // namespace Nox::Spy
