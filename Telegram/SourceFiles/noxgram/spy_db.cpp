/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "noxgram/spy_db.h"

#include "main/main_session.h"
#include "settings.h"

#include <crl/crl_queue.h>
#include <sqlite3.h>

#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <mutex>

namespace Nox::Spy {
namespace {

constexpr auto kSchema = R"(
CREATE TABLE IF NOT EXISTS deleted (
	peer INTEGER NOT NULL,
	msg INTEGER NOT NULL,
	topic INTEGER NOT NULL DEFAULT 0,
	grouped INTEGER NOT NULL DEFAULT 0,
	from_id INTEGER NOT NULL DEFAULT 0,
	from_name TEXT NOT NULL DEFAULT '',
	date INTEGER NOT NULL DEFAULT 0,
	at INTEGER NOT NULL DEFAULT 0,
	text TEXT NOT NULL DEFAULT '',
	tags BLOB,
	expired INTEGER NOT NULL DEFAULT 0,
	media INTEGER NOT NULL DEFAULT 0,
	media_path TEXT NOT NULL DEFAULT '',
	media_name TEXT NOT NULL DEFAULT '',
	media_mime TEXT NOT NULL DEFAULT '',
	media_size INTEGER NOT NULL DEFAULT 0,
	UNIQUE (peer, msg)
);
CREATE INDEX IF NOT EXISTS deleted_peer ON deleted (peer, rowid);
CREATE TABLE IF NOT EXISTS edits (
	peer INTEGER NOT NULL,
	msg INTEGER NOT NULL,
	from_id INTEGER NOT NULL DEFAULT 0,
	from_name TEXT NOT NULL DEFAULT '',
	date INTEGER NOT NULL DEFAULT 0,
	at INTEGER NOT NULL DEFAULT 0,
	text TEXT NOT NULL DEFAULT '',
	tags BLOB
);
CREATE INDEX IF NOT EXISTS edits_message ON edits (peer, msg, rowid);
)";

[[nodiscard]] QByteArray SerializeText(const TextWithEntities &text) {
	return TextUtilities::SerializeTags(
		TextUtilities::ConvertEntitiesToTextTags(text.entities));
}

[[nodiscard]] TextWithEntities DeserializeText(
		const QString &text,
		const QByteArray &tags) {
	return {
		text,
		TextUtilities::ConvertTextTagsToEntities(
			TextUtilities::DeserializeTags(tags, text.size())),
	};
}

[[nodiscard]] int64 FolderSize(const QString &path) {
	auto result = int64();
	auto i = QDirIterator(path, QDir::Files, QDirIterator::Subdirectories);
	while (i.hasNext()) {
		i.next();
		result += i.fileInfo().size();
	}
	return result;
}

} // namespace

class Database::Connection final {
public:
	explicit Connection(QString path) : _path(std::move(path)) {
	}
	~Connection() {
		if (_db) {
			sqlite3_close_v2(_db);
		}
	}

	[[nodiscard]] std::unique_lock<std::mutex> lock() {
		return std::unique_lock(_mutex);
	}

	[[nodiscard]] sqlite3 *open() {
		if (_db || _failed) {
			return _db;
		}
		const auto path = QFile::encodeName(_path);
		const auto flags = SQLITE_OPEN_READWRITE
			| SQLITE_OPEN_CREATE
			| SQLITE_OPEN_FULLMUTEX;
		if (sqlite3_open_v2(path.constData(), &_db, flags, nullptr)
			!= SQLITE_OK) {
			LOG(("NoxGram Spy Error: could not open %1: %2."
				).arg(_path
				).arg(_db ? sqlite3_errmsg(_db) : "out of memory"));
			if (_db) {
				sqlite3_close_v2(_db);
				_db = nullptr;
			}
			_failed = true;
			return nullptr;
		}
		sqlite3_busy_timeout(_db, 2000);
		exec("PRAGMA journal_mode=WAL;");
		exec("PRAGMA synchronous=NORMAL;");
		exec("PRAGMA temp_store=MEMORY;");
		exec("PRAGMA cache_size=-512;");
		if (!exec(kSchema)) {
			_failed = true;
			sqlite3_close_v2(_db);
			_db = nullptr;
		}
		return _db;
	}

	bool exec(const char *sql) {
		auto error = (char*)nullptr;
		if (sqlite3_exec(_db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
			LOG(("NoxGram Spy Error: %1.").arg(error ? error : "unknown"));
			sqlite3_free(error);
			return false;
		}
		return true;
	}

private:
	const QString _path;
	std::mutex _mutex;
	sqlite3 *_db = nullptr;
	bool _failed = false;

};

namespace {

class Statement final {
public:
	Statement(sqlite3 *db, const char *sql) {
		if (db && sqlite3_prepare_v2(db, sql, -1, &_stmt, nullptr)
			!= SQLITE_OK) {
			LOG(("NoxGram Spy Error: %1.").arg(sqlite3_errmsg(db)));
			_stmt = nullptr;
		}
	}
	~Statement() {
		sqlite3_finalize(_stmt);
	}

	[[nodiscard]] explicit operator bool() const {
		return _stmt != nullptr;
	}

	Statement &bind(int index, int64 value) {
		sqlite3_bind_int64(_stmt, index, value);
		return *this;
	}
	Statement &bind(int index, const QString &value) {
		const auto utf8 = value.toUtf8();
		sqlite3_bind_text(
			_stmt,
			index,
			utf8.constData(),
			utf8.size(),
			SQLITE_TRANSIENT);
		return *this;
	}
	Statement &bind(int index, const QByteArray &value) {
		sqlite3_bind_blob(
			_stmt,
			index,
			value.constData(),
			value.size(),
			SQLITE_TRANSIENT);
		return *this;
	}

	bool step() {
		return _stmt && (sqlite3_step(_stmt) == SQLITE_ROW);
	}
	void run() {
		if (_stmt) {
			sqlite3_step(_stmt);
		}
	}

	[[nodiscard]] int64 integer(int column) const {
		return sqlite3_column_int64(_stmt, column);
	}
	[[nodiscard]] QString text(int column) const {
		const auto data = reinterpret_cast<const char*>(
			sqlite3_column_text(_stmt, column));
		return QString::fromUtf8(data, sqlite3_column_bytes(_stmt, column));
	}
	[[nodiscard]] QByteArray blob(int column) const {
		const auto data = static_cast<const char*>(
			sqlite3_column_blob(_stmt, column));
		return QByteArray(data, sqlite3_column_bytes(_stmt, column));
	}

private:
	sqlite3_stmt *_stmt = nullptr;

};

[[nodiscard]] crl::queue &Queue() {
	static auto result = crl::queue();
	return result;
}

[[nodiscard]] std::optional<Record> ParseLegacy(const QByteArray &line) {
	const auto document = QJsonDocument::fromJson(line);
	if (!document.isObject()) {
		return std::nullopt;
	}
	const auto object = document.object();
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
		.text = { object.value(u"text"_q).toString() },
	};
}

void InsertDeleted(sqlite3 *db, const Record &record) {
	auto statement = Statement(db, R"(
INSERT OR IGNORE INTO deleted (peer, msg, topic, grouped, from_id, from_name,
	date, at, text, tags, expired, media, media_path, media_name, media_mime,
	media_size)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16)
)");
	statement
		.bind(1, int64(record.peer.value))
		.bind(2, int64(record.msg.bare))
		.bind(3, int64(record.topic.bare))
		.bind(4, int64(record.grouped))
		.bind(5, int64(record.from.value))
		.bind(6, record.fromName)
		.bind(7, int64(record.date))
		.bind(8, int64(record.at))
		.bind(9, record.text.text)
		.bind(10, SerializeText(record.text))
		.bind(11, int64(record.expired ? 1 : 0))
		.bind(12, int64(record.media))
		.bind(13, record.mediaPath)
		.bind(14, record.mediaName)
		.bind(15, record.mediaMime)
		.bind(16, record.mediaSize)
		.run();
}

void InsertEdit(sqlite3 *db, const Record &record) {
	auto last = Statement(db, R"(
SELECT text FROM edits WHERE peer = ?1 AND msg = ?2
ORDER BY rowid DESC LIMIT 1
)");
	last.bind(1, int64(record.peer.value)).bind(2, int64(record.msg.bare));
	if (last.step() && last.text(0) == record.text.text) {
		return;
	}
	auto statement = Statement(db, R"(
INSERT INTO edits (peer, msg, from_id, from_name, date, at, text, tags)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)
)");
	statement
		.bind(1, int64(record.peer.value))
		.bind(2, int64(record.msg.bare))
		.bind(3, int64(record.from.value))
		.bind(4, record.fromName)
		.bind(5, int64(record.date))
		.bind(6, int64(record.at))
		.bind(7, record.text.text)
		.bind(8, SerializeText(record.text))
		.run();
}

void ImportLegacy(sqlite3 *db, const QString &folder) {
	const auto import = [&](const QString &name, auto insert) {
		const auto path = folder + '/' + name;
		auto file = QFile(path);
		if (!file.open(QIODevice::ReadOnly)) {
			return;
		}
		sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr);
		while (!file.atEnd()) {
			const auto line = file.readLine().trimmed();
			if (const auto record = line.isEmpty()
					? std::nullopt
					: ParseLegacy(line)) {
				insert(db, *record);
			}
		}
		sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
		file.close();
		QFile::remove(path);
	};
	import(u"deleted.jsonl"_q, InsertDeleted);
	import(u"edits.jsonl"_q, InsertEdit);
}

[[nodiscard]] Record ReadDeleted(const Statement &row) {
	return {
		.rowId = row.integer(0),
		.peer = PeerId(PeerIdHelper(uint64(row.integer(1)))),
		.msg = MsgId(row.integer(2)),
		.topic = MsgId(row.integer(3)),
		.grouped = uint64(row.integer(4)),
		.from = PeerId(PeerIdHelper(uint64(row.integer(5)))),
		.fromName = row.text(6),
		.date = TimeId(row.integer(7)),
		.at = TimeId(row.integer(8)),
		.text = DeserializeText(row.text(9), row.blob(10)),
		.expired = (row.integer(11) != 0),
		.media = MediaKind(row.integer(12)),
		.mediaPath = row.text(13),
		.mediaName = row.text(14),
		.mediaMime = row.text(15),
		.mediaSize = row.integer(16),
	};
}

} // namespace

Database::Database(uint64 userId)
: _folder(cWorkingDir() + u"tdata/noxgram/"_q + QString::number(userId))
, _connection(std::make_shared<Connection>(_folder + u"/spy.sqlite"_q)) {
	const auto connection = _connection;
	const auto folder = _folder;
	Queue().async([=] {
		auto lock = connection->lock();
		QDir().mkpath(folder);
		if (const auto db = connection->open()) {
			ImportLegacy(db, folder);
		}
	});
}

Database::~Database() = default;

QString Database::folder() const {
	return _folder;
}

QString Database::mediaFolder() const {
	return _folder + u"/media"_q;
}

QString Database::absolute(const QString &mediaPath) const {
	return mediaPath.isEmpty() ? QString() : (_folder + '/' + mediaPath);
}

void Database::addDeleted(Record record) {
	const auto connection = _connection;
	Queue().async([=, record = std::move(record)] {
		auto lock = connection->lock();
		if (const auto db = connection->open()) {
			InsertDeleted(db, record);
		}
	});
}

void Database::addEdit(Record record) {
	const auto connection = _connection;
	Queue().async([=, record = std::move(record)] {
		auto lock = connection->lock();
		if (const auto db = connection->open()) {
			InsertEdit(db, record);
		}
	});
}

void Database::setMedia(
		PeerId peer,
		MsgId msg,
		const QString &path,
		int64 size) {
	const auto connection = _connection;
	Queue().async([=] {
		auto lock = connection->lock();
		if (const auto db = connection->open()) {
			Statement(db, R"(
UPDATE deleted SET media_path = ?3, media_size = ?4
WHERE peer = ?1 AND msg = ?2
)")
				.bind(1, int64(peer.value))
				.bind(2, int64(msg.bare))
				.bind(3, path)
				.bind(4, size)
				.run();
		}
	});
}

bool Database::hasDeleted(PeerId peer) {
	auto lock = _connection->lock();
	auto statement = Statement(
		_connection->open(),
		"SELECT 1 FROM deleted WHERE peer = ?1 LIMIT 1");
	return statement && statement.bind(1, int64(peer.value)).step();
}

bool Database::hasEdits(PeerId peer, MsgId msg) {
	auto lock = _connection->lock();
	auto statement = Statement(
		_connection->open(),
		"SELECT 1 FROM edits WHERE peer = ?1 AND msg = ?2 LIMIT 1");
	return statement
		&& statement
			.bind(1, int64(peer.value))
			.bind(2, int64(msg.bare))
			.step();
}

bool Database::isDeleted(PeerId peer, MsgId msg) {
	auto lock = _connection->lock();
	auto statement = Statement(
		_connection->open(),
		"SELECT 1 FROM deleted WHERE peer = ?1 AND msg = ?2 LIMIT 1");
	return statement
		&& statement
			.bind(1, int64(peer.value))
			.bind(2, int64(msg.bare))
			.step();
}

std::vector<Record> Database::deleted(
		PeerId peer,
		int64 before,
		int limit) {
	auto result = std::vector<Record>();
	auto lock = _connection->lock();
	auto statement = Statement(_connection->open(), R"(
SELECT rowid, peer, msg, topic, grouped, from_id, from_name, date, at, text,
	tags, expired, media, media_path, media_name, media_mime, media_size
FROM deleted WHERE peer = ?1 AND rowid < ?2
ORDER BY rowid DESC LIMIT ?3
)");
	if (!statement) {
		return result;
	}
	statement
		.bind(1, int64(peer.value))
		.bind(2, before ? before : std::numeric_limits<int64>::max())
		.bind(3, int64(limit));
	while (statement.step()) {
		result.push_back(ReadDeleted(statement));
	}
	return result;
}

std::vector<Record> Database::edits(PeerId peer, MsgId msg) {
	auto result = std::vector<Record>();
	auto lock = _connection->lock();
	auto statement = Statement(_connection->open(), R"(
SELECT rowid, from_id, from_name, date, at, text, tags
FROM edits WHERE peer = ?1 AND msg = ?2 ORDER BY rowid
)");
	if (!statement) {
		return result;
	}
	statement.bind(1, int64(peer.value)).bind(2, int64(msg.bare));
	while (statement.step()) {
		result.push_back({
			.rowId = statement.integer(0),
			.peer = peer,
			.msg = msg,
			.from = PeerId(PeerIdHelper(uint64(statement.integer(1)))),
			.fromName = statement.text(2),
			.date = TimeId(statement.integer(3)),
			.at = TimeId(statement.integer(4)),
			.text = DeserializeText(statement.text(5), statement.blob(6)),
		});
	}
	return result;
}

void Database::clearDeleted(PeerId peer) {
	const auto connection = _connection;
	const auto folder = _folder;
	Queue().async([=] {
		auto lock = connection->lock();
		const auto db = connection->open();
		if (!db) {
			return;
		}
		auto paths = QStringList();
		{
			auto statement = Statement(
				db,
				"SELECT media_path FROM deleted WHERE peer = ?1");
			statement.bind(1, int64(peer.value));
			while (statement.step()) {
				if (const auto path = statement.text(0); !path.isEmpty()) {
					paths.push_back(path);
				}
			}
		}
		for (const auto &path : paths) {
			QFile::remove(folder + '/' + path);
		}
		Statement(db, "DELETE FROM deleted WHERE peer = ?1")
			.bind(1, int64(peer.value))
			.run();
	});
}

void Database::clearAll() {
	const auto connection = _connection;
	const auto media = mediaFolder();
	Queue().async([=] {
		auto lock = connection->lock();
		QDir(media).removeRecursively();
		if (const auto db = connection->open()) {
			connection->exec("DELETE FROM deleted; DELETE FROM edits;");
			connection->exec("PRAGMA wal_checkpoint(TRUNCATE);");
			connection->exec("VACUUM;");
		}
	});
}

void Database::usage(Fn<void(Usage)> done) {
	const auto connection = _connection;
	const auto folder = _folder;
	Queue().async([=] {
		auto result = Usage();
		{
			auto lock = connection->lock();
			const auto db = connection->open();
			auto deleted = Statement(db, "SELECT COUNT(*) FROM deleted");
			if (deleted.step()) {
				result.deleted = deleted.integer(0);
			}
			auto edits = Statement(db, "SELECT COUNT(*) FROM edits");
			if (edits.step()) {
				result.edits = edits.integer(0);
			}
		}
		result.bytes = FolderSize(folder);
		crl::on_main([=] {
			done(result);
		});
	});
}

Database &DatabaseFor(not_null<Main::Session*> session) {
	static auto databases = base::flat_map<
		uint64,
		std::unique_ptr<Database>>();
	const auto id = session->userId().bare;
	auto i = databases.find(id);
	if (i == end(databases)) {
		i = databases.emplace(id, std::make_unique<Database>(id)).first;
	}
	return *i->second;
}

} // namespace Nox::Spy
