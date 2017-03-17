#include "fcgi.h"

#include <ncode/ncode_common/logging.h>
#include <ncode/ncode_common/substitute.h>
#include <ncode/ncode_common/ptr_queue.h>
#include <netinet/in.h>
#include <memory>

namespace nc {
namespace web {

size_t FastCGIRecordHeader::MessageSize(const FastCGIRecordHeader& header) {
  return ntohs(header.content_len) + header.padding_len;
}

std::string FastCGIRecordHeader::ToString() {
  return Substitute(
      "Version: $0, type: $1, id: $2, content_len: $3, padding_len: $4",
      version, type, ntohs(id), ntohs(content_len), padding_len);
}

void FastCGIServer::Run() {
  while (!to_kill_) {
    bool timed_out;
    std::unique_ptr<FastCGIMessage> message =
        input_->ConsumeOrBlockWithTimeout(kDefaultTimeout, &timed_out);
    if (timed_out) {
      continue;
    }

    if (!message) {
      break;
    }

    LOG(INFO) << message->header.ToString();
    HandleMessage(std::move(message));
  }
}

uint32_t FCGIConsumeInt(std::vector<char>::const_iterator* it_ptr) {
  std::vector<char>::const_iterator& it = *it_ptr;

  uint8_t first_byte = *it;
  if (first_byte >> 7 == 0) {
    ++it;
    return first_byte;
  }

  uint8_t b3 = first_byte;
  uint8_t b2 = *(++it);
  uint8_t b1 = *(++it);
  uint8_t b0 = *(++it);
  ++it;
  return ((b3 & 0x7f) << 24) + (b2 << 16) + (b1 << 8) + b0;
}

std::map<std::string, std::string> FCGIParseNVPairs(
    const std::vector<char>& data) {
  std::map<std::string, std::string> out;

  auto it = data.begin();
  while (it != data.end()) {
    uint32_t name_len = FCGIConsumeInt(&it);
    uint32_t value_len = FCGIConsumeInt(&it);

    std::string key(it, std::next(it, name_len));
    std::advance(it, name_len);
    std::string value(it, std::next(it, value_len));
    std::advance(it, value_len);
    out[key] = value;
  }

  return out;
}

std::vector<char> FCGIGetStream(FastCGIRecordHeader::Type type,
                                FastCGIRecordList::const_iterator* it_ptr,
                                FastCGIRecordList::const_iterator end) {
  std::vector<char> out;

  bool found_end = false;
  while (*it_ptr != end) {
    FastCGIRecordList::const_iterator& it = *it_ptr;

    const std::vector<char>& contents = (*it)->message;
    const FastCGIRecordHeader& header = (*it)->header;
    ++it;

    CHECK(header.type == type);
    if (header.content_len == 0) {
      found_end = true;
      break;
    }

    out.insert(out.end(), contents.begin(), contents.end());
  }

  CHECK(found_end);
  return out;
}

static FastCGIRecordList StreamToMessages(const std::string& out) {
  size_t max_len = std::numeric_limits<uint16_t>::max();
}

void FastCGIServer::HandleMessage(std::unique_ptr<FastCGIMessage> message) {
  const FastCGIRecordHeader& header = message->header;
  uint16_t content_len = ntohs(header.content_len);
  bool is_last = header.type == FastCGIRecordHeader::STDIN && content_len == 0;
  uint16_t id = header.id;

  FastCGIRecordList& records = requests_[id];
  records.emplace_back(std::move(message));
  if (!is_last) {
    return;
  }

  // Record is the last from a request. Will pack it up and offload it, but
  // first check that it is complete.
  FastCGIRecordList::const_iterator it = records.begin();
  const FastCGIMessage& begin_record = *(it->get());
  CHECK(begin_record.header.type == FastCGIRecordHeader::BEGIN_REQUEST);
  CHECK(ntohs(begin_record.header.content_len) ==
        sizeof(FastCGIBeginRequestBody));

  const FastCGIBeginRequestBody* begin_request_body =
      reinterpret_cast<const FastCGIBeginRequestBody*>(
          begin_record.message.data());
  uint16_t role = ntohs(begin_request_body->role);
  CHECK(role == FastCGIRole::FCGI_RESPONDER);

  // Start parsing, this should be the first of the params records.
  ++it;

  std::vector<char> params =
      FCGIGetStream(FastCGIRecordHeader::PARAMS, &it, records.end());
  std::vector<char> contents =
      FCGIGetStream(FastCGIRecordHeader::STDIN, &it, records.end());

  std::map<std::string, std::string> nv_pairs = FCGIParseNVPairs(params);
  for (const auto& key_and_value : nv_pairs) {
    LOG(INFO) << key_and_value.first << " --> " << key_and_value.second;
  }

  std::string contents_str(contents.begin(), contents.end());
  std::string out = Handle(contents_str, nv_pairs);
}

}  // namespace web
}  // namespace nc
