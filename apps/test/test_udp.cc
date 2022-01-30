extern "C" {
#include <base/log.h>
}

#include "runtime.h"
#include "net.h"
#include "timer.h"

#include <memory>
#include <iostream>
#include <vector>


namespace {

const unsigned int MAX_BUF_LENGTH = 2048;

netaddr raddr;
constexpr uint64_t serverPort = 8001;

void ServerHandler(void *arg) {
  std::unique_ptr <rt::UdpConn> udpConn(rt::UdpConn::Listen({0, serverPort}));
  if (udpConn == nullptr) panic("couldn't listen for connections");

  std::string rcv;
  ssize_t ret;

  while (true) {
    do {
			std::vector<char> buffer(MAX_BUF_LENGTH);
      ret = udpConn->ReadFrom(&buffer[0], buffer.size(), &raddr);
			std::string rcv(buffer.begin(), buffer.end());
			log_info("received = %s, bytes = %ld", rcv.c_str(), ret);
    } while (ret == MAX_BUF_LENGTH);
  }

  udpConn->Shutdown();
}

void ClientHandler(void *arg) {
  std::unique_ptr <rt::UdpConn> udpConn(rt::UdpConn::Dial({0, 0}, raddr));
  if (unlikely(udpConn == nullptr)) panic("couldn't connect to raddr.");

  std::string snd(1460, 'A');

  ssize_t ret = udpConn->Write(&snd[0], snd.size());
  if (ret != static_cast<ssize_t>(snd.size())) {
    panic("write failed, ret = %ld", ret);
  }

  log_info("sent %s, bytes = %ld", snd.c_str(), ret);

  udpConn->Shutdown();
}
} // namespace

int main(int argc, char *argv[]) {
  int ret;

  if (argc < 3) {
    std::cerr << "usage: [cfg_file] [cmd] ..." << std::endl;
    return -EINVAL;
  }

  std::string cmd = argv[2];
  if (cmd.compare("server") == 0) {
    ret = runtime_init(argv[1], ServerHandler, NULL);
    if (ret) {
      printf("failed to start runtime\n");
      return ret;
    }
  } else if (cmd.compare("client") != 0) {
    std::cerr << "usage: [cfg_file] client [remote_ip]" << std::endl;
    return -EINVAL;
  }

  raddr = rt::StringToNetaddr(argv[3]);
  raddr.port = serverPort;

  ret = runtime_init(argv[1], ClientHandler, NULL);
  if (ret) {
    printf("failed to start runtime\n");
    return ret;
  }

  return 0;
}
