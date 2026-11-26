/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "noxgram/spy_media.h"

#include "core/file_location.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "noxgram/options.h"
#include "ui/image/image.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMimeDatabase>
#include <QtCore/QRegularExpression>

namespace Nox::Spy {
namespace {

constexpr auto kGiveUpAfter = 10 * 60 * crl::time(1000);

struct Pending {
	PeerId peer = 0;
	MsgId msg = 0;
	QString target;
	Fn<void(bool)> done;
	std::shared_ptr<Data::PhotoMedia> photo;
	DocumentData *document = nullptr;
	std::shared_ptr<Data::DocumentMedia> documentMedia;
	bool started = false;
	crl::time since = 0;
};

[[nodiscard]] bool WriteFile(const QString &path, const QByteArray &bytes) {
	auto file = QFile(path);
	return file.open(QIODevice::WriteOnly)
		&& (file.write(bytes) == bytes.size());
}

[[nodiscard]] QString Extension(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return QString();
	} else if (media->photo()) {
		return u"jpg"_q;
	}
	const auto document = media->document();
	if (!document) {
		return QString();
	}
	auto suffix = QFileInfo(document->filename()).suffix();
	suffix.remove(QRegularExpression(u"[^A-Za-z0-9]"_q));
	if (!suffix.isEmpty()) {
		return suffix.left(16);
	}
	const auto mime = QMimeDatabase().mimeTypeForName(
		document->mimeString());
	const auto preferred = mime.preferredSuffix();
	if (!preferred.isEmpty()) {
		return preferred;
	} else if (document->isVoiceMessage()) {
		return u"ogg"_q;
	} else if (document->isVideoFile() || document->isVideoMessage()) {
		return u"mp4"_q;
	}
	return u"bin"_q;
}

class Saver final {
public:
	explicit Saver(not_null<Main::Session*> session) : _session(session) {
		_session->downloaderTaskFinished(
		) | rpl::on_next([=] {
			check();
		}, _lifetime);
	}

	void add(Pending pending) {
		if (ready(pending)) {
			if (pending.done) {
				pending.done(true);
			}
			return;
		}
		start(pending);
		_pending.push_back(std::move(pending));
	}

private:
	void start(Pending &pending) {
		const auto origin = Data::FileOrigin(
			FullMsgId(pending.peer, pending.msg));
		pending.started = true;
		if (pending.photo) {
			pending.photo->owner()->load(
				Data::PhotoSize::Large,
				origin,
				LoadFromCloudOrLocal,
				true);
		} else if (const auto document = pending.document) {
			if (!document->loading()) {
				document->save(
					origin,
					pending.target,
					LoadFromCloudOrLocal,
					true);
			}
		}
	}

	[[nodiscard]] bool ready(const Pending &pending) {
		const auto done = [] {
			return true;
		};
		if (pending.document && pending.document->loading()) {
			return false;
		} else if (QFileInfo::exists(pending.target)
			&& QFileInfo(pending.target).size() > 0) {
			return done();
		}
		if (const auto &photo = pending.photo) {
			const auto bytes = photo->imageBytes(Data::PhotoSize::Large);
			if (!bytes.isEmpty() && WriteFile(pending.target, bytes)) {
				return done();
			}
			if (const auto image = photo->image(Data::PhotoSize::Large)) {
				if (image->original().save(pending.target, "JPG", 95)) {
					return done();
				}
			}
			return false;
		}
		const auto document = pending.document;
		if (!document || document->loading()) {
			return false;
		}
		if (const auto &media = pending.documentMedia) {
			const auto bytes = media->bytes();
			if (!bytes.isEmpty() && WriteFile(pending.target, bytes)) {
				return done();
			}
		}
		const auto path = document->filepath(true);
		if (!path.isEmpty()
			&& path != pending.target
			&& QFile::copy(path, pending.target)) {
			return done();
		}
		return false;
	}

	void check() {
		const auto now = crl::now();
		auto results = std::vector<std::pair<Fn<void(bool)>, bool>>();
		for (auto i = begin(_pending); i != end(_pending);) {
			const auto failed = i->document
				&& !i->document->loading()
				&& (i->document->status == FileDownloadFailed
					|| i->document->cancelled());
			if (ready(*i)) {
				results.emplace_back(std::move(i->done), true);
				i = _pending.erase(i);
			} else if (failed || (now - i->since > kGiveUpAfter)) {
				QFile::remove(i->target);
				results.emplace_back(std::move(i->done), false);
				i = _pending.erase(i);
			} else {
				if (i->document
					&& !i->document->loading()
					&& !QFileInfo::exists(i->target)) {
					start(*i);
				}
				++i;
			}
		}
		for (const auto &[done, ok] : results) {
			if (done) {
				done(ok);
			}
		}
	}

	const not_null<Main::Session*> _session;
	std::vector<Pending> _pending;
	rpl::lifetime _lifetime;

};

[[nodiscard]] Saver &SaverFor(not_null<Main::Session*> session) {
	static auto savers = base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<Saver>>();
	auto i = savers.find(session);
	if (i == end(savers)) {
		i = savers.emplace(session, std::make_unique<Saver>(session)).first;
		session->lifetime().add([=] {
			savers.remove(session);
		});
	}
	return *i->second;
}

} // namespace

MediaKind KindOf(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return MediaKind::None;
	} else if (media->photo()) {
		return MediaKind::Photo;
	}
	const auto document = media->document();
	if (!document) {
		return MediaKind::Other;
	} else if (document->sticker()) {
		return MediaKind::Sticker;
	} else if (document->isVoiceMessage()) {
		return MediaKind::Voice;
	} else if (document->isVideoMessage()) {
		return MediaKind::Round;
	} else if (document->isAnimation()) {
		return MediaKind::Gif;
	} else if (document->isVideoFile()) {
		return MediaKind::Video;
	} else if (document->isAudioFile()) {
		return MediaKind::Audio;
	}
	return MediaKind::File;
}

QString MediaName(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	return document ? document->filename() : QString();
}

QString MediaMime(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return QString();
	} else if (media->photo()) {
		return u"image/jpeg"_q;
	}
	const auto document = media->document();
	return document ? document->mimeString() : QString();
}

int64 MediaSize(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	return document ? document->size : 0;
}

int64 MediaLimitBytes(int index) {
	constexpr auto kMb = int64(1024 * 1024);
	switch (index) {
	case 0: return 10 * kMb;
	case 1: return 50 * kMb;
	case 2: return 200 * kMb;
	case 3: return 1024 * kMb;
	}
	return std::numeric_limits<int64>::max();
}

bool FetchMedia(
		not_null<HistoryItem*> item,
		const QString &target,
		Fn<void(bool)> done) {
	const auto media = item->media();
	const auto photo = media ? media->photo() : nullptr;
	const auto document = media ? media->document() : nullptr;
	if (!photo && !document) {
		return false;
	}
	auto pending = Pending{
		.peer = item->history()->peer->id,
		.msg = item->id,
		.target = target,
		.done = std::move(done),
		.since = crl::now(),
	};
	if (photo) {
		pending.photo = photo->createMediaView();
	} else {
		pending.document = document;
		pending.documentMedia = document->createMediaView();
	}
	SaverFor(&item->history()->session()).add(std::move(pending));
	return true;
}

QString MediaExtension(not_null<HistoryItem*> item) {
	return Extension(item);
}

void SaveMedia(not_null<HistoryItem*> item) {
	auto &options = Nagram::ForDevice();
	if (!options.Get(kSaveDeletedMedia)) {
		return;
	}
	const auto kind = KindOf(item);
	if (kind == MediaKind::None || kind == MediaKind::Other) {
		return;
	}
	const auto document = item->media()->document();
	if (document
		&& document->size > MediaLimitBytes(
			options.Get(kDeletedMediaLimit))) {
		return;
	}
	const auto session = &item->history()->session();
	auto &database = DatabaseFor(session);
	const auto peer = item->history()->peer->id;
	const auto msg = item->id;
	const auto folder = QString::number(peer.value);
	QDir().mkpath(database.mediaFolder() + '/' + folder);
	const auto relative = u"media/"_q
		+ folder
		+ '/'
		+ QString::number(msg.bare)
		+ '.'
		+ Extension(item);
	const auto target = database.absolute(relative);
	const auto weak = base::make_weak(session);
	FetchMedia(item, target, [=](bool ok) {
		if (const auto strong = weak.get(); strong && ok) {
			DatabaseFor(strong).setMedia(
				peer,
				msg,
				relative,
				QFileInfo(target).size());
		}
	});
}

} // namespace Nox::Spy
