// net.h - support for networking

#pragma once

extern "C" {
#include <cstddef>
#include <runtime/poll.h>
#include <runtime/tcp.h>
#include <runtime/udp.h>
#include <base/log.h>
}

#include <string>

namespace rt {

struct netaddr StringToNetaddr(const std::string &str);

std::string NetaddrToIPString(struct netaddr naddr);

class NetConn {
 public:
  virtual ~NetConn() = default;
  virtual ssize_t Read(void *buf, size_t len) = 0;
  virtual ssize_t Write(const void *buf, size_t len) = 0;
};

// UDP Connections.
class UdpConn : public NetConn {
  friend class EventLoop;

 public:
  UdpConn() = default;
  explicit UdpConn(udpconn_t *c) : c_(c) {}
  ~UdpConn() override { udp_close(c_); }

  // disable move and copy.
  UdpConn(const UdpConn &) = delete;
  UdpConn &operator=(const UdpConn &) = delete;

  // The maximum possible payload size (with the maximum MTU).
  static constexpr size_t kMaxPayloadSize = UDP_MAX_PAYLOAD_SIZE;

  int Bind(const netaddr *localAddr) {
    log_info("binding to %d : %d", localAddr->ip, localAddr->port);
    int ret = udp_bind(&c_, *localAddr);
    log_info("binding complete!");
    if (ret) return ret;
    udp_set_nonblocking(c_, nonBlocking_);
    return 0;
  }

  int Connect(const netaddr *remoteAddr) {
    udp_set_nonblocking(c_, nonBlocking_);
    return udp_connect(&c_, *remoteAddr);
  }

  // Creates a UDP connection between a local and remote address.
  static UdpConn *Dial(netaddr laddr, netaddr raddr) {
    udpconn_t *c;
    int ret = udp_dial(laddr, raddr, &c);
    if (ret)
      return nullptr;
    return new UdpConn(c);
  }

  // Creates a UDP connection that receives all packets on a local port.
  static UdpConn *Listen(netaddr laddr) {
    udpconn_t *c;
    int ret = udp_listen(laddr, &c);
    if (ret)
      return nullptr;
    return new UdpConn(c);
  }

  // Gets the MTU-limited payload size.
  static size_t PayloadSize() { return static_cast<size_t>(udp_payload_size); }

  // Gets head of event list
  struct list_head *EventList() {
    return udp_get_triggers(c_);
  }

  // Gets the local UDP address.
  [[nodiscard]] netaddr LocalAddr() const { return udp_local_addr(c_); }

  // Gets the remote UDP address.
  [[nodiscard]] netaddr RemoteAddr() const { return udp_remote_addr(c_); }

  // Adjusts the length of buffer limits.
  int SetBuffers(int read_mbufs, int write_mbufs) {
    udp_set_nonblocking(c_, nonBlocking_);
    return udp_set_buffers(c_, read_mbufs, write_mbufs);
  }

  // Reads a datagram and gets from remote address.
  ssize_t ReadFrom(void *buf, size_t len, netaddr *raddr) {
    udp_set_nonblocking(c_, nonBlocking_);
    return udp_read_from(c_, buf, len, raddr);
  }

  // Writes a datagram and sets to remote address.
  ssize_t WriteTo(const void *buf, size_t len, const netaddr *raddr) {
    udp_set_nonblocking(c_, nonBlocking_);
    return udp_write_to(c_, buf, len, raddr);
  }

  // Reads a datagram.
  ssize_t Read(void *buf, size_t len) override {
    udp_set_nonblocking(c_, nonBlocking_);
    return udp_read(c_, buf, len);
  }

  // Writes a datagram.
  ssize_t Write(const void *buf, size_t len) override {
    udp_set_nonblocking(c_, nonBlocking_);
    return udp_write(c_, buf, len);
  }

  // Shutdown the socket (no more receives).
  void Shutdown() { udp_shutdown(c_); }

  // Set the socket's nonblocking state
  void SetNonblocking(bool nonblocking) {
    nonBlocking_ = true;
  }

 private:
  udpconn_t *c_;
  bool nonBlocking_{false};
};

// TCP connections.
class TcpConn : public NetConn {
  friend class TcpQueue;

 public:
  ~TcpConn() { tcp_close(c_); }

  // Creates a TCP connection with a given affinity
  static TcpConn *DialAffinity(uint32_t affinity, netaddr raddr) {
    tcpconn_t *c;
    int ret = tcp_dial_affinity(affinity, raddr, &c);
    if (ret)
      return nullptr;
    return new TcpConn(c);
  }

  // Creates a TCP connection between a local and remote address.
  static TcpConn *Dial(netaddr laddr, netaddr raddr) {
    tcpconn_t *c;
    int ret = tcp_dial(laddr, raddr, &c);
    if (ret)
      return nullptr;
    return new TcpConn(c);
  }

  // Creates a new TCP connection with matching affinity
  TcpConn *DialAffinity(netaddr raddr) {
    tcpconn_t *c;
    int ret = tcp_dial_conn_affinity(c_, raddr, &c);
    if (ret)
      return nullptr;
    return new TcpConn(c);
  }

  // Gets the local TCP address.
  netaddr LocalAddr() const { return tcp_local_addr(c_); }
  // Gets the remote TCP address.
  netaddr RemoteAddr() const { return tcp_remote_addr(c_); }

  // Reads from the TCP stream.
  ssize_t Read(void *buf, size_t len) { return tcp_read(c_, buf, len); };
  // Writes to the TCP stream.
  ssize_t Write(const void *buf, size_t len) { return tcp_write(c_, buf, len); }
  // Reads a vector from the TCP stream.
  ssize_t Readv(const iovec *iov, int iovcnt) {
    return tcp_readv(c_, iov, iovcnt);
  }
  // Writes a vector to the TCP stream.
  ssize_t Writev(const iovec *iov, int iovcnt) {
    return tcp_writev(c_, iov, iovcnt);
  }

  // Reads exactly @len bytes from the TCP stream.
  ssize_t ReadFull(void *buf, size_t len) {
    char *pos = reinterpret_cast<char *>(buf);
    size_t n = 0;
    while (n < len) {
      ssize_t ret = Read(pos + n, len - n);
      if (ret <= 0)
        return ret;
      n += ret;
    }
    sh_assert(n == len);
    return n;
  }

  // Writes exactly @len bytes to the TCP stream.
  ssize_t WriteFull(const void *buf, size_t len) {
    const char *pos = reinterpret_cast<const char *>(buf);
    size_t n = 0;
    while (n < len) {
      ssize_t ret = Write(pos + n, len - n);
      if (ret < 0)
        return ret;
      sh_assert(ret > 0);
      n += ret;
    }
    sh_assert(n == len);
    return n;
  }

  // Reads exactly a vector of bytes from the TCP stream.
  ssize_t ReadvFull(const iovec *iov, int iovcnt) {
    if (__builtin_constant_p(iovcnt)) {
      if (iovcnt == 1)
        return ReadFull(iov[0].iov_base, iov[0].iov_len);
    }
    return ReadvFullRaw(iov, iovcnt);
  }

  // Writes exactly a vector of bytes to the TCP stream.
  ssize_t WritevFull(const iovec *iov, int iovcnt) {
    if (__builtin_constant_p(iovcnt)) {
      if (iovcnt == 1)
        return WriteFull(iov[0].iov_base, iov[0].iov_len);
    }
    return WritevFullRaw(iov, iovcnt);
  }

  // Gracefully shutdown the TCP connection.
  int Shutdown(int how) { return tcp_shutdown(c_, how); }
  // Ungracefully force the TCP connection to shutdown.
  void Abort() { tcp_abort(c_); }

 private:
  TcpConn(tcpconn_t *c) : c_(c) {}

  // disable move and copy.
  TcpConn(const TcpConn &) = delete;
  TcpConn &operator=(const TcpConn &) = delete;

  ssize_t WritevFullRaw(const iovec *iov, int iovcnt);
  ssize_t ReadvFullRaw(const iovec *iov, int iovcnt);

  tcpconn_t *c_;
};

// TCP listener queues.
class TcpQueue {
 public:
  ~TcpQueue() { tcp_qclose(q_); }

  // Creates a TCP listener queue.
  static TcpQueue *Listen(netaddr laddr, int backlog) {
    tcpqueue_t *q;
    int ret = tcp_listen(laddr, backlog, &q);
    if (ret)
      return nullptr;
    return new TcpQueue(q);
  }

  // Accept a connection from the listener queue.
  TcpConn *Accept() {
    tcpconn_t *c;
    int ret = tcp_accept(q_, &c);
    if (ret)
      return nullptr;
    return new TcpConn(c);
  }

  // Shutdown the listener queue; any blocked Accept() returns a nullptr.
  void Shutdown() { tcp_qshutdown(q_); }

 private:
  TcpQueue(tcpqueue_t *q) : q_(q) {}

  // disable move and copy.
  TcpQueue(const TcpQueue &) = delete;
  TcpQueue &operator=(const TcpQueue &) = delete;

  tcpqueue_t *q_;
};

} // namespace rt
