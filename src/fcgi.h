#ifndef NCODE_WEB_FCGI_H_
#define NCODE_WEB_FCGI_H_

#include <ncode/ncode_common/common.h>
#include <stddef.h>
#include <chrono>
#include <cstdint>
#include <string>

#include "server.h"

namespace nc {
namespace web {

struct FastCGIRecordHeader {
  enum Type : uint8_t {
    BEGIN_REQUEST = 1,
    ABORT_REQUEST = 2,
    END_REQUEST = 3,
    PARAMS = 4,
    STDIN = 5,
    STDOUT = 6,
    STDERR = 7,
    DATA = 8,
    GET_VALUES = 9,
    GET_VALUES_RESULT = 10,
    UNKNOWN_TYPE = 11
  };

  static size_t MessageSize(const FastCGIRecordHeader& header);

  std::string ToString();

  // FastCGI version number
  uint8_t version;

  // Record type
  Type type;

  // Request ID
  uint16_t id;

  // Content
  uint16_t content_len;

  // Padding
  uint8_t padding_len;

  // Reserved
  uint8_t reserved;
};

// Begin request.
struct FastCGIBeginRequestBody {
  uint16_t role;
  uint8_t flags;
  uint8_t reserved[5];
};

enum FastCGIRole : uint16_t {
  FCGI_RESPONDER = 1,
  FCGI_AUTHORIZER = 2,
  FCGI_FILTER = 3,
};

using FastCGIMessage = HeaderAndMessage<FastCGIRecordHeader>;
using FastCGIMessageQueue = TCPServer<FastCGIRecordHeader>::QueueType;
using FastCGIRecordList = std::vector<std::unique_ptr<FastCGIMessage>>;

class FastCGIServer {
 public:
  static constexpr std::chrono::milliseconds kDefaultTimeout =
      std::chrono::milliseconds(500);

  FastCGIServer(FastCGIMessageQueue* input, FastCGIMessageQueue* output)
      : to_kill_(false), input_(input), output_(output) {}

  void Start() {
    thread_ = std::thread([this] { Run(); });
  }

  void Stop() {
    to_kill_ = true;
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  ~FastCGIServer() { Stop(); }

  // Handles a message.
  std::string Handle(const std::string& input,
                     const std::map<std::string, std::string>& params) = 0;

 private:
  void Run();

  void HandleMessage(std::unique_ptr<FastCGIMessage> message);

  std::atomic<bool> to_kill_;
  std::thread thread_;

  FastCGIMessageQueue* input_;
  FastCGIMessageQueue* output_;

  // For each request id the list of messages received for this request.
  std::map<uint16_t, FastCGIRecordList> requests_;

  DISALLOW_COPY_AND_ASSIGN(FastCGIServer);
};

// Extracts a single integer encoded as in the FCGI spec.
uint32_t FCGIConsumeInt(std::vector<char>::const_iterator* it_ptr);

// Extracts key-value pairs from a stream of data.
std::map<std::string, std::string> FCGIParseNVPairs(
    const std::vector<char>& data);

// Combines a series of messages into a stream.
std::vector<char> FCGIGetStream(FastCGIRecordHeader::Type type,
                                FastCGIRecordList::const_iterator* it_ptr,
                                FastCGIRecordList::const_iterator end);

}  // namespace web
}  // namespace nc

#endif
