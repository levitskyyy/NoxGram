/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

The WebSocket transport follows the approach of tg-ws-proxy
(https://github.com/Flowseal/tg-ws-proxy, MIT), built into the client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "noxgram/ws_transport.h"

#include "noxgram/options.h"
#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "mtproto/mtp_instance.h"

#include <atomic>

namespace Nox::WsTransport {
namespace {

std::atomic<bool> EnabledValue = false;
std::atomic<bool> FallbackValue = true;

void Reconnect() {
	if (!Core::App().domain().started()) {
		return;
	}
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		account->mtp().restart();
	}
}

} // namespace

void Start() {
	static auto started = false;
	if (started) {
		return;
	}
	started = true;
	auto &options = Nagram::ForDevice();
	EnabledValue = options.Get(kWsTransport);
	FallbackValue = options.Get(kWsDirectFallback);
	static auto lifetime = rpl::lifetime();
	options.Value(
		kWsTransport
	) | rpl::skip(1) | rpl::on_next([](bool value) {
		EnabledValue = value;
		Reconnect();
	}, lifetime);
	options.Value(
		kWsDirectFallback
	) | rpl::skip(1) | rpl::on_next([](bool value) {
		FallbackValue = value;
		if (EnabledValue) {
			Reconnect();
		}
	}, lifetime);
}

bool Enabled() {
	return EnabledValue;
}

bool DirectFallback() {
	return FallbackValue;
}

} // namespace Nox::WsTransport
