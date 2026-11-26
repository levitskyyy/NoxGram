/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

Forwarding from chats that restrict it is modelled on the feature of
AyuGram Desktop (https://github.com/AyuGram/AyuGramDesktop), licensed
under GPLv3.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "noxgram/forward.h"

#include "api/api_common.h"
#include "api/api_sending.h"
#include "apiwrap.h"
#include "base/timer.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_poll.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "noxgram/lang.h"
#include "noxgram/options.h"
#include "noxgram/spy_media.h"
#include "settings.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/toast/toast.h"
#include "styles/style_boxes.h"

#include <QtCore/QDir>
#include <QtCore/QFile>

namespace Nox::Forward {
namespace {

constexpr auto kSentTimeout = 10 * 60 * crl::time(1000);

[[nodiscard]] bool SendsAsText(not_null<const HistoryItem*> item) {
	const auto media = item->media();
	return !media
		|| media->webpage()
		|| media->poll()
		|| media->game()
		|| media->location()
		|| media->sharedContact()
		|| media->todolist()
		|| (!media->photo() && !media->document());
}

[[nodiscard]] TextWithEntities TextOf(not_null<const HistoryItem*> item) {
	const auto media = item->media();
	if (const auto poll = media ? media->poll() : nullptr) {
		auto result = poll->question;
		for (const auto &answer : poll->answers) {
			result.append(u"\n• "_q).append(answer.text);
		}
		return result;
	}
	return item->originalText();
}

[[nodiscard]] TextWithTags Tags(const TextWithEntities &text) {
	return {
		text.text,
		TextUtilities::ConvertEntitiesToTextTags(text.entities),
	};
}

[[nodiscard]] QString Folder() {
	return cWorkingDir() + u"tdata/noxgram/forward"_q;
}

struct Step {
	std::vector<FullMsgId> ids;
};

class Sender final : public std::enable_shared_from_this<Sender> {
public:
	Sender(
		not_null<Main::Session*> session,
		std::vector<Step> steps,
		Api::SendAction action,
		Data::ForwardOptions options)
	: _session(session)
	, _steps(std::move(steps))
	, _action(std::move(action))
	, _options(options)
	, _timeout([=] { waited(); }) {
	}

	void start() {
		QDir().mkpath(Folder());
		const auto self = shared_from_this();
		_session->data().itemIdChanged(
		) | rpl::on_next([=](const Data::Session::IdChange &change) {
			if (change.newId.peer != _action.history->peer->id) {
				return;
			} else if (_waitingSent > 0 && !--_waitingSent) {
				self->waited();
			}
		}, _lifetime);
		const auto weak = std::weak_ptr<Sender>(self);
		_session->lifetime().add([=] {
			if (const auto strong = weak.lock()) {
				strong->_lifetime.destroy();
				strong->_steps.clear();
			}
		});
		next();
	}

private:
	[[nodiscard]] bool dropCaptions() const {
		return (_options == Data::ForwardOptions::NoNamesAndCaptions);
	}

	void next() {
		if (_index >= int(_steps.size())) {
			finish();
			return;
		}
		const auto &step = _steps[_index++];
		auto items = HistoryItemsList();
		for (const auto &id : step.ids) {
			if (const auto item = _session->data().message(id)) {
				items.push_back(item);
			}
		}
		if (items.empty()) {
			continueLater();
		} else if (items.size() == 1 && SendsAsText(items.front())) {
			sendText(items.front());
		} else if (const auto sticker = StickerOf(items.front())) {
			Api::SendExistingDocument(Api::MessageToSend(_action), sticker);
			continueLater();
		} else {
			download(std::move(items));
		}
	}

	[[nodiscard]] static DocumentData *StickerOf(
			not_null<HistoryItem*> item) {
		const auto media = item->media();
		const auto document = media ? media->document() : nullptr;
		return (document && document->sticker()) ? document : nullptr;
	}

	void sendText(not_null<HistoryItem*> item) {
		const auto text = TextOf(item);
		if (!text.text.isEmpty()) {
			auto message = Api::MessageToSend(_action);
			message.textWithTags = Tags(text);
			_session->api().sendMessage(std::move(message));
		}
		continueLater();
	}

	void download(HistoryItemsList items) {
		_files.clear();
		_failed = false;
		_pending = int(items.size());
		_current = items;
		for (const auto &item : items) {
			const auto path = Folder()
				+ '/'
				+ QString::number(item->history()->peer->id.value)
				+ '_'
				+ QString::number(item->id.bare)
				+ '.'
				+ Spy::MediaExtension(item);
			_files.push_back(path);
			const auto weak = std::weak_ptr<Sender>(shared_from_this());
			const auto started = Spy::FetchMedia(item, path, [=](bool ok) {
				if (const auto strong = weak.lock()) {
					strong->downloaded(ok);
				}
			});
			if (!started) {
				downloaded(false);
			}
		}
	}

	void downloaded(bool ok) {
		if (!ok) {
			_failed = true;
		}
		if (--_pending > 0) {
			return;
		}
		const auto self = shared_from_this();
		crl::on_main(_session, [=] {
			self->sendMedia();
		});
	}

	void sendMedia() {
		auto paths = QStringList();
		auto items = HistoryItemsList();
		for (auto i = 0; i != int(_current.size()); ++i) {
			if (QFile::exists(_files[i])) {
				paths.push_back(_files[i]);
				items.push_back(_current[i]);
			}
		}
		if (paths.isEmpty()) {
			++_skipped;
			for (const auto &item : _current) {
				if (!item->originalText().text.isEmpty()) {
					sendText(item);
					return;
				}
			}
			continueLater();
			return;
		}
		const auto first = items.front();
		const auto media = first->media();
		const auto document = media ? media->document() : nullptr;
		if (items.size() == 1 && document && document->isVoiceMessage()) {
			sendVoice(first, paths.front());
			return;
		}
		auto list = Storage::PrepareMediaList(
			paths,
			st::sendMediaPreviewSize,
			_session->premium());
		if (list.error != Ui::PreparedList::Error::None
			|| list.files.empty()) {
			++_skipped;
			continueLater();
			return;
		}
		if (!dropCaptions()) {
			for (auto i = 0; i != int(list.files.size()); ++i) {
				if (i < int(items.size())) {
					list.files[i].caption = Tags(items[i]->originalText());
				}
			}
		}
		const auto photos = media && media->photo();
		const auto type = photos
			? SendMediaType::Photo
			: SendMediaType::File;
		const auto album = (list.files.size() > 1)
			? std::make_shared<SendingAlbum>()
			: nullptr;
		_waitingSent = int(list.files.size());
		_session->api().sendFiles(std::move(list), type, album, _action);
		_timeout.callOnce(kSentTimeout);
	}

	void sendVoice(not_null<HistoryItem*> item, const QString &path) {
		auto file = QFile(path);
		if (!file.open(QIODevice::ReadOnly)) {
			++_skipped;
			continueLater();
			return;
		}
		const auto document = item->media()->document();
		const auto voice = document->voice();
		_waitingSent = 1;
		_session->api().sendVoiceMessage(
			file.readAll(),
			voice ? voice->waveform : VoiceWaveform(),
			document->duration(),
			false,
			nullptr,
			_action);
		_timeout.callOnce(kSentTimeout);
	}

	void waited() {
		_timeout.cancel();
		_waitingSent = 0;
		for (const auto &path : _files) {
			QFile::remove(path);
		}
		_files.clear();
		_current.clear();
		continueLater();
	}

	void continueLater() {
		const auto self = shared_from_this();
		crl::on_main(_session, [=] {
			self->next();
		});
	}

	void finish() {
		_lifetime.destroy();
		const auto text = _skipped
			? Nox::TrNow(
				u"lng_nox_forward_done_skipped"_q,
				{ { u"count"_q, QString::number(_skipped) } })
			: Nox::TrNow(u"lng_nox_forward_done"_q);
		Ui::Toast::Show(text);
	}

	const not_null<Main::Session*> _session;
	std::vector<Step> _steps;
	const Api::SendAction _action;
	const Data::ForwardOptions _options;
	base::Timer _timeout;
	HistoryItemsList _current;
	QStringList _files;
	int _index = 0;
	int _pending = 0;
	int _waitingSent = 0;
	int _skipped = 0;
	bool _failed = false;
	rpl::lifetime _lifetime;

};

} // namespace

bool Enabled() {
	return Nagram::ForDevice().Get(kForwardProtected);
}

bool CanReupload(not_null<const HistoryItem*> item) {
	if (item->isService()) {
		return false;
	}
	const auto media = item->media();
	if (media && (media->invoice() || media->paper())) {
		return false;
	}
	return !item->originalText().text.isEmpty()
		|| (media && (media->photo() || media->document() || media->poll()));
}

bool NeedsReupload(not_null<const HistoryItem*> item) {
	if (!Enabled() || !CanReupload(item)) {
		return false;
	}
	const auto media = item->media();
	return item->forbidsForward()
		|| !item->history()->peer->allowsForwarding()
		|| item->isNoxDeleted()
		|| (media && !media->allowsForward());
}

void Send(
		const HistoryItemsList &items,
		const Api::SendAction &action,
		Data::ForwardOptions options) {
	if (items.empty()) {
		return;
	}
	auto sorted = items;
	ranges::sort(sorted, [](not_null<HistoryItem*> a, not_null<HistoryItem*> b) {
		return a->id < b->id;
	});
	auto steps = std::vector<Step>();
	auto lastGroup = MessageGroupId();
	for (const auto &item : sorted) {
		const auto group = item->groupId();
		if (group && !steps.empty() && group == lastGroup) {
			steps.back().ids.push_back(item->fullId());
		} else {
			steps.push_back({ { item->fullId() } });
		}
		lastGroup = group;
	}
	const auto session = &sorted.front()->history()->session();
	Ui::Toast::Show(Nox::TrNow(
		u"lng_nox_forward_started"_q,
		{ { u"count"_q, QString::number(sorted.size()) } }));
	std::make_shared<Sender>(
		session,
		std::move(steps),
		action,
		options)->start();
}

} // namespace Nox::Forward
