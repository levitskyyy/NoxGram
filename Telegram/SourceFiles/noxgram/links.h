/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace qthelp {
class RegularExpressionMatch;
} // namespace qthelp

namespace Window {
class SessionController;
} // namespace Window

namespace Nox::Links {

// tg://user?id=123 and tg://openmessage?user_id=123 open the user.
bool ResolveUserById(
	Window::SessionController *controller,
	const qthelp::RegularExpressionMatch &match,
	const QVariant &context);

} // namespace Nox::Links
