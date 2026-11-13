/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "noxgram/lang.h"

#include "lang/lang_instance.h"

#include <QtCore/QFile>
#include <QtCore/QRegularExpression>

namespace Nox {
namespace {

using Texts = base::flat_map<QString, QString>;

[[nodiscard]] QString Unescape(const QString &value) {
	auto result = QString();
	result.reserve(value.size());
	for (auto i = 0; i < value.size(); ++i) {
		const auto ch = value[i];
		if (ch != u'\\' || i + 1 == value.size()) {
			result.append(ch);
			continue;
		}
		const auto next = value[++i];
		result.append((next == u'n') ? QChar(u'\n') : next);
	}
	return result;
}

[[nodiscard]] Texts Load(const QString &path) {
	auto result = Texts();
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		LOG(("NoxGram Error: could not read %1.").arg(path));
		return result;
	}
	static const auto line = QRegularExpression(
		uR"re(^\s*"([a-z0-9_]+)"\s*=\s*"((?:[^"\\]|\\.)*)"\s*;\s*$)re"_q,
		QRegularExpression::MultilineOption);
	const auto content = QString::fromUtf8(file.readAll());
	auto i = line.globalMatch(content);
	while (i.hasNext()) {
		const auto match = i.next();
		result.emplace(match.captured(1), Unescape(match.captured(2)));
	}
	return result;
}

[[nodiscard]] const Texts &English() {
	static const auto result = Load(u":/langs/nagram/noxgram_en.strings"_q);
	return result;
}

[[nodiscard]] const Texts &Russian() {
	static const auto result = Load(u":/langs/nagram/ru.strings"_q);
	return result;
}

[[nodiscard]] bool UseRussian() {
	const auto &instance = Lang::GetInstance();
	const auto russian = [](const QString &id) {
		return id.toLower().startsWith(u"ru"_q);
	};
	return russian(instance.id()) || russian(instance.baseId());
}

} // namespace

QString TrNow(const QString &key) {
	if (UseRussian()) {
		const auto &russian = Russian();
		if (const auto i = russian.find(key); i != end(russian)) {
			return i->second;
		}
	}
	const auto &english = English();
	const auto i = english.find(key);
	return (i != end(english)) ? i->second : key;
}

QString TrNow(
		const QString &key,
		std::initializer_list<std::pair<QString, QString>> values) {
	auto result = TrNow(key);
	for (const auto &[name, value] : values) {
		result.replace(u'{' + name + u'}', value);
	}
	return result;
}

rpl::producer<QString> Tr(const QString &key) {
	return rpl::single(
		TrNow(key)
	) | rpl::then(Lang::GetInstance().updated(
	) | rpl::map([=] {
		return TrNow(key);
	}));
}

} // namespace Nox
