#ifndef NCODE_WEB_FCGI_H_
#define NCODE_WEB_FCGI_H_

#include <ncode/ncode_common/common.h>
#include <stddef.h>
#include <unistd.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace nc {
namespace web {

enum RecordType : uint8_t {
  BEGIN_REQUEST = 1,
  ABORT_REQUEST = 2,
  END_REQUEST = 3,
  PARAMS = 4,
  IN = 5,
  OUT = 6,
  ERR = 7,
  DATA = 8,
  GET_VALUES = 9,
  GET_VALUES_RESULT = 10,
  UNKNOWN_TYPE = 11
};

struct FastCGIRecordHeader {
  // FastCGI version number
  uint8_t version;

  // Record type
  RecordType type;

  // Request ID
  uint16_t id;

  // Content
  uint16_t content_len;

  // Padding
  uint8_t padding_len;

  // Reserved
  uint8_t reserved;
};

class FastCGIRequest {
 public:
  FastCGIRequest(const std::map<std::string, std::string>& params,
                 const std::string& content)
      : params_(params), content_(content) {}

  const std::map<std::string, std::string>& params() const { return params_; }

  const std::string& content() const { return content_; }

 private:
  std::map<std::string, std::string> params_;
  std::string content_;
};

class FastCGIServer {
 public:
  FastCGIServer(uint32_t port)
      : tcp_socket_(-1), port_(port), to_kill_(false) {}

  ~FastCGIServer() { Terminate(); }

  // Starts the main loop.
  void StartLoop() {
    OpenSocket();
    thread_ = std::thread([this] { Loop(); });
  }

  // Kills the server.
  void Terminate() {
    to_kill_ = true;

    if (thread_.joinable()) {
      thread_.join();
    }

    close(tcp_socket_);
  }

  void Join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  // This is the max number of bytes a complete record can have (rounded up to
  // 4K multiplier).
  struct Buffer {
    Buffer() : total(0) {}

    std::array<char, 68000ul> data;
    size_t total;
  };

  // A complete CGIRecord.
  struct FastCGIRecord {
    FastCGIRecordHeader header;
    std::vector<char> contents;
  };

  // Handles a complete record.
  void HandleRecord(FastCGIRecord&& record);

  // Reads from a socket into its buffer in active_connections_. If the read
  // completes a record it will be constructed and HandleRecord called.
  bool ReadFromSocket(int socket);

  // Opens the socket for listening.
  void OpenSocket();

  // Runs the main server loop. Will block.
  void Loop();

  // Called when a new TCP connection is established with the server. Accepts
  // the connection and populates new_socket with  the new socket. Will also set
  // try_again to true if EWOULDBLOCK is returned by accept.
  void NewTcpConnection(int* new_socket, bool* try_again);

  // Currently active connections. Map from socket to a buffer that holds the
  // FastCGIRecord currently being received.
  std::map<int, Buffer> active_connections_;

  // The socket the server listens on.
  int tcp_socket_;

  // The port the server should listen to.
  const uint32_t port_;

  // Set to true when the server needs to exit.
  std::atomic<bool> to_kill_;

  // The server's main thread.
  std::thread thread_;

  DISALLOW_COPY_AND_ASSIGN(FastCGIServer);
};

}  // namespace web
}  // namespace nc

#endif
