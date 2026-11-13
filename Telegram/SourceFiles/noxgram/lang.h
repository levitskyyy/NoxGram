/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

namespace Nox {

// NoxGram's own texts, from Resources/langs/nagram/noxgram*.strings.
// They are read from the resources at run time instead of the generated
// lang_auto.h, which almost every file includes: adding or changing one
// rebuilds only the resource bundle and the files that use it.
// An unknown key comes back as the key itself.
[[nodiscard]] QString TrNow(const QString &key);
[[nodiscard]] rpl::producer<QString> Tr(const QString &key);

// Like TrNow, with every {name} replaced by its value.
[[nodiscard]] QString TrNow(
	const QString &key,
	std::initializer_list<std::pair<QString, QString>> values);

} // namespace Nox
