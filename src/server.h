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
#include <thread>
#include <vector>
#include <deque>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <fcntl.h>

namespace nc {
namespace web {

template <typename HeaderType>
class InputChannel {
 public:
  InputChannel(int socket) : offset_(0), socket_(socket) {}

  bool ReadFromSocket() {
    const size_t header_len = sizeof(HeaderType);

    bool header_seen = offset_ >= header_len;
    while (true) {
      if (!header_seen) {
        char* header_ptr = reinterpret_cast<char*>(&header_);

        ssize_t bytes_read =
            read(socket_, header_ptr + offset_, header_len - offset_);
        if (bytes_read == -1 || bytes_read == 0) {
          if (errno == EAGAIN) {
            break;
          }

          LOG(ERROR) << "Unable to read / connection closed";
          return false;
        }

        offset_ += bytes_read;
        if (offset_ < header_len) {
          break;
        }
      } else {
        size_t message_len = MessageSizeFromHeader(header_);
        if (message_len != 0) {
          message_.resize(std::max(message_.size(), message_len));

          size_t into_message = offset_ - header_len;
          ssize_t bytes_read = read(socket_, message_.data() + into_message,
                                    message_len - into_message);
          if (bytes_read == -1 || bytes_read == 0) {
            if (errno == EAGAIN) {
              break;
            }

            LOG(ERROR) << "Unable to read / connection closed";
            return false;
          }

          offset_ += bytes_read;
          if (offset_ < message_len + header_len) {
            break;
          }
        }

        auto message = make_unique<std::vector<char>>();
        *message = std::move(message_);
        HandleMessage(header_, std::move(message));

        offset_ = 0;
      }
    }

    return true;
  }

 private:
  // Stores the header.
  HeaderType header_;

  // Stores the message.
  std::vector<char> message_;

  // A single offset into header + message.
  size_t offset_;

  // The socket.
  int socket_;

  DISALLOW_COPY_AND_ASSIGN(InputChannel);
};

template <typename HeaderType>
struct HeaderAndMessage {
  HeaderType header;
  std::vector<char> message;
};

template <typename HeaderType>
class OutputChannel {
 public:
  OutputChannel(int socket, PtrQueue<1024, HeaderAndMessage>* pending)
      : pending_(pending), offset_(0), socket_(socket) {}

  bool WriteToSocket() {
    const size_t header_len = sizeof(HeaderType);
    while (true) {
      std::unique_ptr<HeaderAndMessage<HeaderType>> message =
          pending_->ConsumeOrBlock();
      if (!message) {
        break;
      }

      if (offset_ < header_len) {
        char* header_ptr = reinterpret_cast<char*>(&message->header);
        ssize_t num_written =
            write(socket_, header_ptr + offset_, header_len - offset_);
        if (num_written == -1) {
          if (errno == EAGAIN) {
            break;
          }

          LOG(ERROR) << "Unable to write";
          return false;
        }

        offset_ += num_written;
      } else {
        size_t into_message = offset_ - header_len;
        size_t message_len = current_data.message.size();

        ssize_t num_written =
            write(socket_, current_data.message.data() + offset_,
                  message_len - into_message);
        if (num_written == -1) {
          if (errno == EAGAIN) {
            break;
          }

          LOG(ERROR) << "Unable to write";
          return false;
        }

        offset_ += num_written;
        if (offset_ == message_len + header_len) {
          offset_ = 0;
          pending_.pop_front();
        }
      }
    }

    return true;
  }

 private:
  // Messages come from here.
  PtrQueue<1024, HeaderAndMessage>* pending_;

  size_t offset_;

  // The socket.
  int socket_;

  DISALLOW_COPY_AND_ASSIGN(OutputChannel);
};

template <typename HeaderType>
class ServerConnection {
 public:
  ServerConnection(sockaddr_in address, int socket)
      : address_(address), input_channel_(socket), output_channel_(socket) {}

  void Read() { input_channel_.ReadFromSocket(); }

  void Write() { output_channel_.WriteToSocket(); }

 private:
  sockaddr_in address_;
  InputChannel<HeaderType> input_channel_;
  OutputChannel<HeaderType> output_channel_;
};

template <typename HeaderType>
class TCPServer {
 public:
  TCPServer(uint32_t port) : tcp_socket_(-1), port_(port), to_kill_(false) {}

  virtual ~TCPServer() { Terminate(); }

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

 protected:
  // Called when a complete message arrives.
  virtual void HandleMessage(const HeaderType& header,
                             std::unique_ptr<std::vector<char>> message) = 0;

  virtual void MessageSizeFromHeader(const HeaderType& header) = 0;

 private:
  struct ChannelPair {};

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

    // Set to non-blocking
    fcntl(tcp_socket_, F_SETFL, O_NONBLOCK);
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

  DISALLOW_COPY_AND_ASSIGN(TCPServer);
};

}  // namesapce web
}  // namespace nc
