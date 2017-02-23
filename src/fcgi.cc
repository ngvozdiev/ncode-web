#include "fcgi.h"

#include <fcntl.h>
#include <ncode/ncode_common/logging.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iterator>

namespace nc {
namespace web {

bool FastCGIServer::ReadFromSocket(int socket) {
  size_t header_len = sizeof(FastCGIRecordHeader);

  Buffer& current_buffer = active_connections_[socket];
  bool header_seen = current_buffer.total >= header_len;
  while (true) {
    size_t& current_total = current_buffer.total;
    if (!header_seen) {
      ssize_t bytes_read =
          read(socket, current_buffer.data.data() + current_total,
               header_len - current_total);
      if (bytes_read == -1 || bytes_read == 0) {
        if (errno == EAGAIN) {
          break;
        }

        LOG(ERROR) << "Unable to read / connection closed";
        return false;
      }

      current_total += bytes_read;
      if (current_total < header_len) {
        break;
      }
    } else {
      const FastCGIRecordHeader* header =
          reinterpret_cast<FastCGIRecordHeader*>(current_buffer.data.data());
      uint16_t content_len = ntohs(header->content_len);
      uint16_t padding_len = ntohs(header->padding_len);

      if (content_len != 0 && padding_len != 0) {
        size_t total_record_len = header_len + content_len + padding_len;
        ssize_t bytes_read =
            read(socket, current_buffer.data.data() + current_total,
                 total_record_len - current_total);
        if (bytes_read == -1 || bytes_read == 0) {
          if (errno == EAGAIN) {
            break;
          }

          LOG(ERROR) << "Unable to read / connection closed";
          return false;
        }

        current_total += bytes_read;
        if (current_total < total_record_len) {
          break;
        }
      }

      // Copy the header and data into a complete record.
      FastCGIRecord record;
      record.header = *header;
      record.header.content_len = content_len;
      record.header.padding_len = padding_len;

      record.contents.resize(content_len);
      std::copy(
          std::next(current_buffer.data.begin(), header_len),
          std::next(current_buffer.data.begin(), header_len + content_len),
          record.contents.begin());

      HandleRecord(std::move(record));
      current_buffer.total = 0;
    }
  }

  return true;
}

void FastCGIServer::Loop() {
  int last_fd = tcp_socket_;
  fd_set master;
  fd_set read_fds;

  FD_ZERO(&master);
  FD_ZERO(&read_fds);

  FD_SET(tcp_socket_, &master);

  while (!to_kill_) {
    read_fds = master;

    timeval tv = {0, 0};
    tv.tv_sec = 1;

    int select_return = select(last_fd + 1, &read_fds, NULL, NULL, &tv);

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
    }
  }
}

void FastCGIServer::NewTcpConnection(int* new_socket, bool* try_again) {
  struct sockaddr_in remote_address;
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
}

void FastCGIServer::OpenSocket() {
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

}  // namespace web
}  // namespace nc
