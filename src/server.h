#ifndef NCODE_WEB_SERVER_H_
#define NCODE_WEB_SERVER_H_

#include <ncode/ncode_common/common.h>
#include <ncode/ncode_common/ptr_queue.h>
#include <stddef.h>
#include <unistd.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <string.h>
#include <thread>
#include <vector>
#include <deque>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <fcntl.h>

namespace nc {
namespace web {

bool BlockingRawReadFromSocket(int sock, char* buf, uint32_t len);

template <typename HeaderType>
struct HeaderAndMessage {
  HeaderAndMessage(int socket) : socket(socket) {}

  // Socket this message should be sent to / was received on.
  int socket;

  HeaderType header;
  std::vector<char> message;
};

template <typename HeaderType>
std::unique_ptr<HeaderAndMessage<HeaderType>> BlockingReadMessageFromSocket(
    int socket) {
  auto message_ptr = make_unique<HeaderAndMessage<HeaderType>>(socket);
  HeaderType& header = message_ptr->header;

  char* header_ptr = reinterpret_cast<char*>(&header);
  if (!BlockingRawReadFromSocket(socket, header_ptr, sizeof(HeaderType))) {
    return {};
  }

  size_t message_len = HeaderType::MessageLen(header);
  message_ptr->message.resize(message_len);
  std::vector<char>& message_v = message_ptr->message;
  if (!BlockingRawReadFromSocket(socket, message_v.data(), message_len)) {
    return {};
  }

  return message_ptr;
}

template <typename HeaderType>
using MessageQueue = PtrQueue<HeaderAndMessage<HeaderType>, 1024>;

template <typename HeaderType>
class InputChannel {
 public:
  InputChannel(int socket, MessageQueue<HeaderType>* outgoing)
      : outgoing_(outgoing), socket_(socket) {
    thread_ = std::thread([this] { ReadFromSocket(); });
  }

  void ReadFromSocket() {
    while (true) {
      auto message_ptr = BlockingReadMessageFromSocket<HeaderType>(socket_);
      if (!message_ptr) {
        break;
      }

      if (!outgoing_->ProduceOrBlock(std::move(message_ptr))) {
        break;
      }
    }
  }

 private:
  // Thread that constantly tries to write to the socket.
  std::thread thread_;

  // Messages come from here.
  MessageQueue<HeaderType>* outgoing_;

  // The socket.
  int socket_;

  DISALLOW_COPY_AND_ASSIGN(InputChannel);
};

bool BlockingRawWriteToSocket(int sock, const char* buf, uint32_t len);

template <typename HeaderType>
bool BlockingWriteMessageToSocket(
    std::unique_ptr<HeaderAndMessage<HeaderType>> msg) {
  char* header_ptr = reinterpret_cast<char*>(&msg->header);
  if (!BlockingRawWriteToSocket(msg->socket, header_ptr, sizeof(HeaderType))) {
    return false;
  }

  const std::vector<char>& message_v = msg->message;
  const char* message_ptr = message_v.data();
  if (!BlockingRawWriteToSocket(msg->socket, message_ptr, message_v.size())) {
    return false;
  }

  return true;
}

template <typename HeaderType>
class ServerConnection {
 public:
  ServerConnection(sockaddr_in address, int socket,
                   MessageQueue<HeaderType>* incoming)
      : address_(address), input_channel_(socket, incoming) {}

 private:
  sockaddr_in address_;
  InputChannel<HeaderType> input_channel_;
};

template <typename HeaderType>
class TCPServer {
 public:
  TCPServer(uint32_t port, MessageQueue<HeaderType>* incoming,
            MessageQueue<HeaderType>* outgoing)
      : tcp_socket_(-1),
        port_(port),
        to_kill_(false),
        incoming_(incoming),
        outgoing_(outgoing) {}

  virtual ~TCPServer() { Terminate(); }

  // Starts the main loop.
  void StartLoop() {
    OpenSocket();
    thread_ = std::thread([this] { Loop(); });
    send_thread_ = std::thread([this] { WriteToSocket(); });
  }

  // Kills the server.
  void Terminate() {
    LOG(INFO) << "Closing socket and terminating server.";
    close(tcp_socket_);
    to_kill_ = true;

    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void Join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  // Opens the socket for listening.
  void OpenSocket() {
    sockaddr_in address;
    memset(reinterpret_cast<char*>(&address), 0, sizeof(address));

    if ((tcp_socket_ = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
      LOG(FATAL) << "Unable to get socket";
    }

    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    address.sin_addr.s_addr = INADDR_ANY;

    int yes = 1;
    if (setsockopt(tcp_socket_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) ==
        -1) {
      LOG(FATAL) << "Unable to set REUSEADDR";
    }

    if (bind(tcp_socket_, reinterpret_cast<sockaddr*>(&address),
             sizeof(sockaddr)) == -1) {
      LOG(FATAL) << "Unable to bind: " + std::string(strerror(errno));
    }

    if (listen(tcp_socket_, 10) == -1) {
      LOG(FATAL) << "Unable to listen";
    }
  }

  // Called when a new TCP connection is established with the server. Accepts
  // the connection and populates new_socket with  the new socket. Will also set
  // try_again to true if EWOULDBLOCK is returned by accept.
  void NewTcpConnection(int* new_socket, bool* try_again) {
    sockaddr_in remote_address;
    socklen_t address_len = sizeof(remote_address);

    *try_again = false;
    int socket;
    if ((socket = accept(tcp_socket_,
                         reinterpret_cast<struct sockaddr*>(&remote_address),
                         &address_len)) == -1) {
      if (errno != EWOULDBLOCK) {
        LOG(FATAL) << "Unable to accept";
      }

      *try_again = true;
      return;
    }

    fcntl(socket, F_SETFL, O_NONBLOCK);
    *new_socket = socket;

    active_connections_.emplace(std::piecewise_construct,
                                std::forward_as_tuple(socket),
                                std::forward_as_tuple(remote_address, socket));
  }

  // Runs the main server loop. Will block.
  void Loop() {
    int last_fd = tcp_socket_;
    fd_set master;
    fd_set read_fds;
    fd_set write_fds;

    FD_ZERO(&master);
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);

    FD_SET(tcp_socket_, &master);

    while (!to_kill_) {
      read_fds = master;
      write_fds = master;

      timeval tv = {0, 0};
      tv.tv_sec = 1;

      int select_return = select(last_fd + 1, &read_fds, &write_fds, NULL, &tv);

      if (select_return < 0) {
        LOG(FATAL) << "Unable to select";
      }

      if (select_return == 0) {
        continue;  // Timed out
      }

      for (int i = 0; i <= last_fd; i++) {
        if (FD_ISSET(i, &read_fds)) {
          if (i == tcp_socket_) {
            int new_socket;
            bool try_again;

            NewTcpConnection(&new_socket, &try_again);
            if (try_again) {
              break;
            }

            FD_SET(new_socket, &master);
            if (new_socket > last_fd) {
              last_fd = new_socket;
            }
          } else {
            if (!ReadFromSocket(i)) {
              LOG(INFO) << "Error in connection";
              active_connections_.erase(i);
            }
          }
        }

        if (FD_ISSET(i, &write_fds)) {
          WriteToSocket(i);
        }
      }
    }

    while (!to_kill_) {
      sockaddr_in remote_address;
      socklen_t address_len = sizeof(remote_address);

      int socket = accept(tcp_socket_,
                          reinterpret_cast<struct sockaddr*>(&remote_address),
                          &address_len);
      if (socket) {
        LOG(FATAL) << "Unable to accept: " + std::string(strerror(errno));
        break;
      }

      active_connections_.emplace(
          std::piecewise_construct, std::forward_as_tuple(socket),
          std::forward_as_tuple(remote_address, socket, incoming_, outgoing_));
    }
  }

  void WriteToSocket() {
    while (true) {
      std::unique_ptr<HeaderAndMessage<HeaderType>> message =
          outgoing_->ConsumeOrBlock();
      if (!message) {
        break;
      }

      if (!BlockingWriteMessageToSocket(std::move(message))) {
        break;
      }
    }
  }

  // Currently active connections.
  std::map<int, ServerConnection<HeaderType>> active_connections_;

  // The socket the server listens on.
  int tcp_socket_;

  // The port the server should listen to.
  const uint32_t port_;

  // Set to true when the server needs to exit.
  std::atomic<bool> to_kill_;

  // The server's main thread.
  std::thread thread_;

  // A thread whose job it is to constantly try to send messages.
  std::thread send_thread_;

  // Queues for messages leaving out/coming in.
  MessageQueue<HeaderType>* incoming_;
  MessageQueue<HeaderType>* outgoing_;

  DISALLOW_COPY_AND_ASSIGN(TCPServer);
};

// Simple wrapper around a blocking socket that makes it easier to
// connect and send / receive messages.
template <typename HeaderType>
class ClientConnection {
 public:
  static void ResolveHostName(const std::string& hostname, in_addr* addr);

  static std::unique_ptr<ClientConnection> Connect(
      const std::string& destination_address, uint32_t port) {
    sockaddr_in address;
    memset(&address, 0, sizeof(address));

    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    ClientConnection::ResolveHostName(destination_address, &(address.sin_addr));
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (::connect(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) !=
        0) {
      LOG(FATAL) << "Unable to connect";
    }

    return std::unique_ptr<ClientConnection>(new ClientConnection(s));
  }

  // Writes a message
  void WriteToSocket(std::unique_ptr<HeaderAndMessage<HeaderType>> msg) const {
    BlockingWriteMessageToSocket(std::move(msg));
  }

  // Reads a message
  std::unique_ptr<HeaderAndMessage<HeaderType>> ReadFromSocket() const {
    return BlockingReadMessageFromSocket<HeaderType>(tcp_socket_);
  }

  // Closes the socket. After this call this ClientConnection will not be able
  // to send/receive any messages.
  void Close() const { ::close(tcp_socket_); }

 private:
  ClientConnection(int tcp_socket) : tcp_socket_(tcp_socket) {}

  // The socket.
  const int tcp_socket_;
};

}  // namesapce web
}  // namespace nc

#endif
