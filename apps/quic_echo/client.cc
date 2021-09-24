#pragma once

#include <iostream>
#include <string>
#include <thread>

namespace quic {
class Client : public quic::QuicSocket::ConnectionCallback,
               public quic::QuicSocket::ReadCallback,
               public quic::QuicSocket::WriteCallback {
 public:
  Client(const std::string& host, uint16_t port) : host_(host), port_(port) {}
  ~Client() override = default;

  void ReadAvailable(quic::StreamId id) noexcept override {}

  void ReadError(quic::StreamId id,
                 quic::QuicErrorCode error) noexcept override {}

  void OnNewBidirectionalStream(quic::StreamId id) noexcept override {}

  void OnNewUnidirectionalStream(quic::StreamId id) noexcept override {}

  void OnStopSending(quic::StreamId id,
                     quic::ApplicationErrorCode error) noexcept override {}

  void OnConnectionEnd() noexcept override {}

  void OnConnectionError(quic::QuicErrorCode error) noexcept override {}

  void OnTransportError() noexcept override {}

  void OnStreamWriteReady(quic::StreamId id,
                          uint64_t max_to_send) noexcept override {}

  void OnStreamWriteError(quic::StreamId id,
                          quic::QuicErrorCode error) noexcept override {}

  void Start() {}

 private:
  void SendMessage(quic::StreamId id, BufQueue& data) {}

  std::string host_;
  uint16_t port_;
  std::shared_ptr<quic::QuicClientTransport> quic_client_;
  std::map<quic::StreamId, BufQueue> pending_output_;
  std::map<quic::StreamId, uint64_t> recv_offsets_;
};

}  // namespace quic
