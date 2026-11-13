/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common.h"

namespace Nox {

// "Ghost and spy mode" section inside the Nagram settings.
[[nodiscard]] Settings::Type GhostSpyId();

// "Connection" section: the WebSocket transport.
[[nodiscard]] Settings::Type ConnectionId();

} // namespace Nox
