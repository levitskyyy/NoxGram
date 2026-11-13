/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

The WebSocket transport follows the approach of tg-ws-proxy
(https://github.com/Flowseal/tg-ws-proxy, MIT), built into the client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Nox::WsTransport {

// Main thread, before the first MTProto session is configured. Mirrors the
// options for the session threads and reconnects when they change.
void Start();

// Thread safe.
[[nodiscard]] bool Enabled();
[[nodiscard]] bool DirectFallback();

} // namespace Nox::WsTransport
