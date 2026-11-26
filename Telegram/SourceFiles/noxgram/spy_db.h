/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Nox::Spy {

enum class MediaKind : int {
	None = 0,
	Photo = 1,
	Video = 2,
	Gif = 3,
	Voice = 4,
	Round = 5,
	Audio = 6,
	File = 7,
	Sticker = 8,
	Other = 9,
};

struct Record {
	int64 rowId = 0;
	PeerId peer = 0;
	MsgId msg = 0;
	MsgId topic = 0;
	uint64 grouped = 0;
	PeerId from = 0;
	QString fromName;
	TimeId date = 0;
	TimeId at = 0;
	TextWithEntities text;
	bool expired = false;
	MediaKind media = MediaKind::None;
	QString mediaPath;
	QString mediaName;
	QString mediaMime;
	int64 mediaSize = 0;
};

struct Usage {
	int64 deleted = 0;
	int64 edits = 0;
	int64 bytes = 0;
};

// Writes go to a background queue, so nothing is kept in memory.
class Database final {
public:
	explicit Database(uint64 userId);
	~Database();

	[[nodiscard]] QString folder() const;
	[[nodiscard]] QString mediaFolder() const;
	[[nodiscard]] QString absolute(const QString &mediaPath) const;

	void addDeleted(Record record);
	void addEdit(Record record);
	void setMedia(PeerId peer, MsgId msg, const QString &path, int64 size);

	[[nodiscard]] bool hasDeleted(PeerId peer);
	[[nodiscard]] bool hasEdits(PeerId peer, MsgId msg);
	[[nodiscard]] bool isDeleted(PeerId peer, MsgId msg);

	[[nodiscard]] std::vector<Record> deleted(
		PeerId peer,
		int64 before,
		int limit);
	[[nodiscard]] std::vector<Record> edits(PeerId peer, MsgId msg);

	void clearDeleted(PeerId peer);
	void clearAll();
	void usage(Fn<void(Usage)> done);

private:
	class Connection;

	const QString _folder;
	const std::shared_ptr<Connection> _connection;

};

[[nodiscard]] Database &DatabaseFor(not_null<Main::Session*> session);

} // namespace Nox::Spy
