/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "noxgram/links.h"

#include "apiwrap.h"
#include "base/qthelp_regex.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "noxgram/lang.h"
#include "window/window_session_controller.h"

namespace Nox::Links {

bool ResolveUserById(
		Window::SessionController *controller,
		const qthelp::RegularExpressionMatch &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	auto ok = false;
	const auto id = match->captured(1).toULongLong(&ok);
	if (!ok || !id) {
		return false;
	}
	const auto session = &controller->session();
	const auto open = [=](not_null<PeerData*> peer) {
		controller->showPeerInfo(peer);
	};
	if (const auto user = session->data().userLoaded(UserId(id))) {
		open(user);
		return true;
	}
	const auto weak = base::make_weak(controller);
	session->api().request(MTPusers_GetUsers(
		MTP_vector<MTPInputUser>(
			1,
			MTP_inputUser(MTP_long(id), MTP_long(0)))
	)).done([=](const MTPVector<MTPUser> &result) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		strong->session().data().processUsers(result);
		if (const auto user = strong->session().data().userLoaded(
				UserId(id))) {
			strong->showPeerInfo(user);
		} else {
			strong->showToast(Nox::TrNow(u"lng_nox_user_not_found"_q));
		}
	}).fail([=] {
		if (const auto strong = weak.get()) {
			strong->showToast(Nox::TrNow(u"lng_nox_user_not_found"_q));
		}
	}).send();
	return true;
}

} // namespace Nox::Links
