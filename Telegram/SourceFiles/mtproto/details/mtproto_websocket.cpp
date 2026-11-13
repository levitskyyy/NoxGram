/*
This file is part of NoxGram Desktop, a fork of Nagram Desktop.

The WebSocket transport follows the approach of tg-ws-proxy
(https://github.com/Flowseal/tg-ws-proxy, MIT), built into the client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_websocket.h"

#include "mtproto/connection_abstract.h"
#include "base/invoke_queued.h"
#include "base/random.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QtEndian>
#include <QtNetwork/QNetworkProxy>

namespace MTP::details {
namespace {

constexpr auto kPort = quint16(443);
constexpr auto kAttemptTimeout = 7000;
constexpr auto kMaxUpgradeResponse = 8 * 1024;
constexpr auto kMaxFramePayload = quint64(16 * 1024 * 1024);
constexpr auto kAcceptGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

constexpr auto kOpcodeContinuation = uchar(0x0);
constexpr auto kOpcodeText = uchar(0x1);
constexpr auto kOpcodeBinary = uchar(0x2);
constexpr auto kOpcodeClose = uchar(0x8);
constexpr auto kOpcodePing = uchar(0x9);
constexpr auto kOpcodePong = uchar(0xA);

struct Target {
	int dcId = 0;
	bool test = false;
	bool files = false;
};

[[nodiscard]] Target ParseTarget(int16 protocolDcId, bool protocolForFiles) {
	auto result = Target();
	const auto media = (protocolDcId < 0);
	auto dcId = media ? -int(protocolDcId) : int(protocolDcId);
	if (dcId > kTestModeDcIdShift) {
		result.test = true;
		dcId -= kTestModeDcIdShift;
	}
	result.dcId = dcId;
	result.files = media || protocolForFiles;
	return result;
}

[[nodiscard]] QString Domain(int dcId, bool files) {
	return u"kws%1%2.web.telegram.org"_q.arg(dcId).arg(files ? "-1" : "");
}

// Where these names resolved to. Used when the name does not resolve,
// with the certificate still checked against the name.
[[nodiscard]] QStringList KnownAddresses(int dcId) {
	switch (dcId) {
	case 1:
	case 3: return { u"149.154.174.100"_q };
	case 2:
	case 4: return { u"149.154.167.99"_q, u"149.154.167.220"_q };
	case 5: return { u"149.154.170.100"_q };
	}
	return {};
}

} // namespace

WebSocket::WebSocket(
	not_null<QThread*> thread,
	int16 protocolDcId,
	bool protocolForFiles)
: AbstractSocket(thread) {
	_socket.moveToThread(thread);
	_attemptTimer.moveToThread(thread);
	_attemptTimer.setSingleShot(true);
	_socket.setProxy(QNetworkProxy::NoProxy);
	if (protocolForFiles) {
		_socket.setSocketOption(
			QAbstractSocket::SendBufferSizeSocketOption,
			kFilesSendBufferSize);
		_socket.setSocketOption(
			QAbstractSocket::ReceiveBufferSizeSocketOption,
			kFilesReceiveBufferSize);
	}

	const auto target = ParseTarget(protocolDcId, protocolForFiles);
	_path = target.test ? u"/apiws_test"_q : u"/apiws"_q;
	if (target.dcId >= 1 && target.dcId <= 5) {
		auto domains = QStringList{ Domain(target.dcId, target.files) };
		if (target.files) {
			domains.push_back(Domain(target.dcId, false));
		}
		for (const auto &domain : domains) {
			_endpoints.push_back({ domain, domain });
		}
		for (const auto &domain : domains) {
			for (const auto &address : KnownAddresses(target.dcId)) {
				_endpoints.push_back({ address, domain });
			}
		}
	}

	// Signals of an attempt that was already given up must not touch the
	// next one, so each handler checks the attempt it was emitted for.
	const auto wrap = [&](auto handler) {
		return [=](auto &&...args) {
			const auto generation = _generation;
			InvokeQueued(this, [=] {
				if (generation == _generation) {
					handler(args...);
				}
			});
		};
	};
	using Error = QAbstractSocket::SocketError;
	connect(
		&_socket,
		&QSslSocket::encrypted,
		wrap([=] { handleEncrypted(); }));
	connect(
		&_socket,
		&QSslSocket::readyRead,
		wrap([=] { handleReadyRead(); }));
	connect(
		&_socket,
		&QSslSocket::disconnected,
		wrap([=] { handleDisconnected(); }));
	connect(
		&_socket,
		&QAbstractSocket::errorOccurred,
		wrap([=](Error e) { handleError(e); }));
	connect(&_attemptTimer, &QTimer::timeout, [=] {
		attemptFailed(u"timeout"_q);
	});
}

WebSocket::~WebSocket() {
	++_generation;
	_attemptTimer.stop();
	_socket.abort();
}

QString WebSocket::HostForDc(int16 protocolDcId, bool protocolForFiles) {
	const auto target = ParseTarget(protocolDcId, protocolForFiles);
	return Domain(target.dcId, target.files);
}

void WebSocket::connectToHost(const QString &address, int port) {
	// The address and port of the DC are not used: the endpoint follows
	// from the DC id the connection was created for.
	if (_state != State::Idle) {
		return;
	}
	connectNext();
}

void WebSocket::connectNext() {
	++_generation;
	_attemptTimer.stop();
	_socket.abort();
	_incoming.clear();
	if (++_endpointIndex >= int(_endpoints.size())) {
		LOG(("Socket %1 Error: no WebSocket endpoint left."
			).arg(_debugId));
		closeWithError();
		return;
	}
	const auto &endpoint = _endpoints[_endpointIndex];
	DEBUG_LOG(("Socket %1 Info: WebSocket to %2 via %3."
		).arg(_debugId, endpoint.domain, endpoint.connectTo));
	_state = State::Connecting;
	_attemptTimer.start(kAttemptTimeout);
	_socket.connectToHostEncrypted(
		endpoint.connectTo,
		kPort,
		endpoint.domain);
}

void WebSocket::attemptFailed(const QString &reason) {
	if (_state == State::Connecting || _state == State::Upgrading) {
		LOG(("Socket %1 Error: WebSocket attempt failed - %2."
			).arg(_debugId, reason));
		connectNext();
	} else if (_state == State::Connected) {
		closeWithError();
	}
}

void WebSocket::closeWithError() {
	++_generation;
	_attemptTimer.stop();
	_state = State::Closed;
	_socket.abort();
	_error.fire({});
}

void WebSocket::handleEncrypted() {
	if (_state != State::Connecting) {
		return;
	}
	_state = State::Upgrading;

	auto keyBytes = bytes::vector(16);
	bytes::set_random(keyBytes);
	_key = QByteArray(
		reinterpret_cast<const char*>(keyBytes.data()),
		int(keyBytes.size())).toBase64();

	const auto &endpoint = _endpoints[_endpointIndex];
	const auto request = "GET " + _path.toLatin1() + " HTTP/1.1\r\n"
		"Host: " + endpoint.domain.toLatin1() + "\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: " + _key + "\r\n"
		"Sec-WebSocket-Version: 13\r\n"
		"Sec-WebSocket-Protocol: binary\r\n"
		"\r\n";
	_socket.write(request);
}

bool WebSocket::readUpgradeResponse() {
	const auto end = _incoming.indexOf("\r\n\r\n");
	if (end < 0) {
		return (_incoming.size() < kMaxUpgradeResponse);
	}
	const auto lines = _incoming.left(end).split('\n');
	const auto status = lines.isEmpty() ? QByteArray() : lines[0].trimmed();
	if (!status.startsWith("HTTP/1.1 101")) {
		DEBUG_LOG(("Socket %1 Error: WebSocket upgrade refused - %2."
			).arg(_debugId, QString::fromLatin1(status)));
		return false;
	}
	const auto expected = QCryptographicHash::hash(
		_key + kAcceptGuid,
		QCryptographicHash::Sha1).toBase64();
	auto accepted = false;
	for (const auto &line : lines) {
		const auto colon = line.indexOf(':');
		if (colon > 0
			&& line.left(colon).trimmed().toLower() == "sec-websocket-accept") {
			accepted = (line.mid(colon + 1).trimmed() == expected);
			break;
		}
	}
	if (!accepted) {
		DEBUG_LOG(("Socket %1 Error: bad Sec-WebSocket-Accept."
			).arg(_debugId));
		return false;
	}
	_incoming.remove(0, end + 4);
	_attemptTimer.stop();
	_state = State::Connected;
	LOG(("Socket %1 Info: WebSocket connected to %2."
		).arg(_debugId, _endpoints[_endpointIndex].domain));
	_connected.fire({});
	return true;
}

void WebSocket::handleReadyRead() {
	if (_state == State::Upgrading) {
		_incoming.append(_socket.readAll());
		if (!readUpgradeResponse()) {
			attemptFailed(u"upgrade"_q);
			return;
		} else if (_state != State::Connected) {
			return;
		}
	} else if (_state == State::Connected) {
		_incoming.append(_socket.readAll());
	} else {
		return;
	}
	const auto before = _received.size() - _receivedOffset;
	if (!parseFrames()) {
		if (_state == State::Connected) {
			_state = State::Closed;
			++_generation;
			_attemptTimer.stop();
			_socket.abort();
			_disconnected.fire({});
		}
		return;
	}
	if (_received.size() - _receivedOffset > before) {
		_readyRead.fire({});
	}
}

bool WebSocket::parseFrames() {
	auto offset = 0;
	const auto size = int(_incoming.size());
	const auto data = reinterpret_cast<const uchar*>(_incoming.constData());
	while (size - offset >= 2) {
		const auto first = data[offset];
		const auto second = data[offset + 1];
		const auto opcode = uchar(first & 0x0F);
		const auto masked = ((second & 0x80) != 0);
		auto length = quint64(second & 0x7F);
		auto header = 2;
		if (length == 126) {
			if (size - offset < 4) {
				break;
			}
			length = qFromBigEndian<quint16>(data + offset + 2);
			header = 4;
		} else if (length == 127) {
			if (size - offset < 10) {
				break;
			}
			length = qFromBigEndian<quint64>(data + offset + 2);
			header = 10;
		}
		if (length > kMaxFramePayload) {
			DEBUG_LOG(("Socket %1 Error: WebSocket frame too large."
				).arg(_debugId));
			return false;
		}
		const auto maskAt = offset + header;
		if (masked) {
			header += 4;
		}
		if (quint64(size - offset) < quint64(header) + length) {
			break;
		}
		const auto payload = offset + header;
		const auto count = int(length);
		switch (opcode) {
		case kOpcodeContinuation:
		case kOpcodeBinary: {
			const auto from = _received.size();
			_received.append(_incoming.constData() + payload, count);
			if (masked) {
				auto to = _received.data() + from;
				for (auto i = 0; i != count; ++i) {
					to[i] = char(to[i] ^ char(data[maskAt + (i % 4)]));
				}
			}
		} break;
		case kOpcodeText:
			break;
		case kOpcodePing: {
			auto pong = QByteArray(_incoming.constData() + payload, count);
			if (masked) {
				for (auto i = 0; i != count; ++i) {
					pong[i] = char(pong[i] ^ char(data[maskAt + (i % 4)]));
				}
			}
			sendFrame(kOpcodePong, bytes::make_span(pong));
		} break;
		case kOpcodePong:
			break;
		case kOpcodeClose:
			DEBUG_LOG(("Socket %1 Info: WebSocket closed by server."
				).arg(_debugId));
			return false;
		default:
			DEBUG_LOG(("Socket %1 Error: bad WebSocket opcode %2."
				).arg(_debugId).arg(int(opcode)));
			return false;
		}
		offset += header + count;
	}
	if (offset > 0) {
		_incoming.remove(0, offset);
	}
	return true;
}

void WebSocket::sendFrame(uchar opcode, bytes::const_span payload) {
	const auto length = quint64(payload.size());
	auto frame = QByteArray();
	frame.reserve(int(length) + 14);
	frame.append(char(0x80 | opcode));
	if (length < 126) {
		frame.append(char(0x80 | length));
	} else if (length <= 0xFFFF) {
		frame.append(char(0x80 | 126));
		frame.append(char((length >> 8) & 0xFF));
		frame.append(char(length & 0xFF));
	} else {
		frame.append(char(0x80 | 127));
		for (auto i = 7; i >= 0; --i) {
			frame.append(char((length >> (i * 8)) & 0xFF));
		}
	}
	auto mask = std::array<char, 4>();
	base::RandomFill(mask.data(), mask.size());
	frame.append(mask.data(), 4);
	const auto from = frame.size();
	frame.append(reinterpret_cast<const char*>(payload.data()), int(length));
	auto to = frame.data() + from;
	for (auto i = 0; i != int(length); ++i) {
		to[i] = char(to[i] ^ mask[i % 4]);
	}
	_socket.write(frame);
}

void WebSocket::handleDisconnected() {
	if (_state == State::Connected) {
		_state = State::Closed;
		_attemptTimer.stop();
		_disconnected.fire({});
	} else {
		attemptFailed(u"disconnected"_q);
	}
}

void WebSocket::handleError(int errorCode) {
	logError(errorCode, _socket.errorString());
	attemptFailed(_socket.errorString());
}

bool WebSocket::isGoodStartNonce(bytes::const_span nonce) {
	Expects(nonce.size() >= 2 * sizeof(uint32));

	// The server reads the same obfuscated init as over plain TCP.
	const auto bytes = nonce.data();
	const auto zero = gsl::to_integer<uchar>(nonce.front());
	const auto first = qFromUnaligned<uint32>(bytes);
	const auto second = qFromUnaligned<uint32>(bytes + sizeof(uint32));
	return (zero != 0xEFU)
		&& (first != 0x44414548U)
		&& (first != 0x54534F50U)
		&& (first != 0x20544547U)
		&& (first != 0xEEEEEEEEU)
		&& (first != 0xDDDDDDDDU)
		&& (first != 0x02010316U)
		&& (second != 0x00000000U);
}

void WebSocket::timedOut() {
}

bool WebSocket::isConnected() {
	return (_state == State::Connected);
}

bool WebSocket::hasBytesAvailable() {
	return (_received.size() > _receivedOffset);
}

int64 WebSocket::read(bytes::span buffer) {
	const auto available = int64(_received.size() - _receivedOffset);
	const auto count = std::min(available, int64(buffer.size()));
	if (count <= 0) {
		return 0;
	}
	memcpy(buffer.data(), _received.constData() + _receivedOffset, count);
	_receivedOffset += int(count);
	if (_receivedOffset == _received.size()) {
		_received.clear();
		_receivedOffset = 0;
	} else if (_receivedOffset > 1024 * 1024) {
		_received.remove(0, _receivedOffset);
		_receivedOffset = 0;
	}
	return count;
}

void WebSocket::write(bytes::const_span prefix, bytes::const_span buffer) {
	Expects(!buffer.empty());

	if (_state != State::Connected) {
		DEBUG_LOG(("Socket %1 Error: write to a WebSocket not connected."
			).arg(_debugId));
		return;
	}
	// The obfuscated init goes in a message of its own, then one message
	// per transport packet, the way Telegram Web sends them.
	if (!prefix.empty()) {
		sendFrame(kOpcodeBinary, prefix);
	}
	sendFrame(kOpcodeBinary, buffer);
}

int32 WebSocket::debugState() {
	return int32(_state);
}

QString WebSocket::debugPostfix() const {
	return u"_ws"_q;
}

} // namespace MTP::details
