/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

The WebSocket transport follows the approach of tg-ws-proxy
(https://github.com/Flowseal/tg-ws-proxy, MIT), built into the client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/details/mtproto_abstract_socket.h"

#include <QtCore/QTimer>
#include <QtNetwork/QSslSocket>

namespace MTP::details {

// Carries the obfuscated MTProto stream, the one a TcpConnection writes,
// inside a WebSocket over TLS to Telegram's own web endpoints
// (wss://kwsN.web.telegram.org/apiws, the transport of Telegram Web).
// The certificate of web.telegram.org is verified as usual, and the
// MTProto encryption inside is unchanged: the TLS layer only makes the
// traffic look like ordinary HTTPS to the network in between.
class WebSocket final : public AbstractSocket {
public:
	WebSocket(
		not_null<QThread*> thread,
		int16 protocolDcId,
		bool protocolForFiles);
	~WebSocket();

	// The host a connection to this DC talks to, for the debug id.
	[[nodiscard]] static QString HostForDc(
		int16 protocolDcId,
		bool protocolForFiles);

	void connectToHost(const QString &address, int port) override;
	bool isGoodStartNonce(bytes::const_span nonce) override;
	void timedOut() override;
	bool isConnected() override;
	bool hasBytesAvailable() override;
	int64 read(bytes::span buffer) override;
	void write(bytes::const_span prefix, bytes::const_span buffer) override;

	int32 debugState() override;
	QString debugPostfix() const override;

private:
	enum class State {
		Idle,
		Connecting,
		Upgrading,
		Connected,
		Closed,
	};
	struct Endpoint {
		QString connectTo;
		QString domain;
	};

	void connectNext();
	void attemptFailed(const QString &reason);
	void handleEncrypted();
	void handleReadyRead();
	void handleDisconnected();
	void handleError(int errorCode);
	void closeWithError();
	[[nodiscard]] bool readUpgradeResponse();
	[[nodiscard]] bool parseFrames();
	void sendFrame(uchar opcode, bytes::const_span payload);

	QSslSocket _socket;
	QTimer _attemptTimer;
	std::vector<Endpoint> _endpoints;
	int _endpointIndex = -1;
	int _generation = 0;
	QString _path;
	QByteArray _key;
	QByteArray _incoming;
	QByteArray _received;
	int _receivedOffset = 0;
	State _state = State::Idle;

};

} // namespace MTP::details
