#include "gtest/gtest.h"
#include "fcgi.h"

namespace nc {
namespace web {
namespace {

TEST(FastCGI, ConsumeIntOne) {
  std::vector<char> buffer;

  buffer.emplace_back(100);
  buffer.emplace_back(10);

  auto it = buffer.cbegin();
  ASSERT_EQ(100ul, FCGIConsumeInt(&it));
  ASSERT_EQ(10ul, FCGIConsumeInt(&it));
  ASSERT_EQ(it, buffer.cend());
}

TEST(FastCGI, ConsumeIntTwo) {
  std::vector<char> buffer;

  buffer.emplace_back(100);
  buffer.emplace_back(173);
  buffer.emplace_back(123);
  buffer.emplace_back(250);
  buffer.emplace_back(15);
  buffer.emplace_back(100);

  auto it = buffer.cbegin();
  ASSERT_EQ(100ul, FCGIConsumeInt(&it));
  ASSERT_EQ(763099663ul, FCGIConsumeInt(&it));
  ASSERT_EQ(100ul, FCGIConsumeInt(&it));
  ASSERT_EQ(it, buffer.cend());
}

TEST(FastCGI, ConsumeIntThree) {
  std::vector<char> buffer;

  buffer.emplace_back(204);
  buffer.emplace_back(255);
  buffer.emplace_back(23);
  buffer.emplace_back(129);
  buffer.emplace_back(100);

  auto it = buffer.cbegin();
  ASSERT_EQ(1291786113ul, FCGIConsumeInt(&it));
  ASSERT_EQ(100ul, FCGIConsumeInt(&it));
  ASSERT_EQ(it, buffer.cend());
}

TEST(FastCGI, ConsumeIntFour) {
  std::vector<char> buffer;
  buffer.emplace_back(173);
  buffer.emplace_back(123);
  buffer.emplace_back(250);
  buffer.emplace_back(15);
  buffer.emplace_back(204);
  buffer.emplace_back(255);
  buffer.emplace_back(23);
  buffer.emplace_back(129);
  buffer.emplace_back(100);

  auto it = buffer.cbegin();
  ASSERT_EQ(763099663ul, FCGIConsumeInt(&it));
  ASSERT_EQ(1291786113ul, FCGIConsumeInt(&it));
  ASSERT_EQ(100ul, FCGIConsumeInt(&it));
  ASSERT_EQ(it, buffer.cend());
}

TEST(FastCGI, ParsePairs) {
  std::vector<char> buffer;
  buffer.emplace_back(10);
  buffer.emplace_back(5);

  for (size_t i = 0; i < 10; ++i) {
    buffer.emplace_back('A');
  }

  for (size_t i = 0; i < 5; ++i) {
    buffer.emplace_back('B');
  }

  std::map<std::string, std::string> model = {{"AAAAAAAAAA", "BBBBB"}};
  ASSERT_EQ(model, FCGIParseNVPairs(buffer));
}

TEST(FastCGI, ParsePairsEmptyValue) {
  std::vector<char> buffer;
  buffer.emplace_back(10);
  buffer.emplace_back(0);

  for (size_t i = 0; i < 10; ++i) {
    buffer.emplace_back('A');
  }

  std::map<std::string, std::string> model = {{"AAAAAAAAAA", ""}};
  ASSERT_EQ(model, FCGIParseNVPairs(buffer));
}

TEST(FastCGI, ParsePairsMulti) {
  std::vector<char> buffer;

  for (size_t i = 0; i < 10; ++i) {
    buffer.emplace_back(10);
    buffer.emplace_back(5);

    for (size_t j = 0; j < 10; ++j) {
      buffer.emplace_back('A');
    }

    for (size_t j = 0; j < 5; ++j) {
      buffer.emplace_back('B');
    }
  }

  std::map<std::string, std::string> model = {{"AAAAAAAAAA", "BBBBB"}};
  ASSERT_EQ(model, FCGIParseNVPairs(buffer));
}

TEST(FastCGI, ParseStreamEmpty) {
  std::vector<std::unique_ptr<FastCGIMessage>> messages;

  auto it = messages.cbegin();
  ASSERT_DEATH(FCGIGetStream(FastCGIRecordHeader::STDOUT, &it, messages.end()),
               ".*");
}

TEST(FastCGI, ParseStreamSingleNoEnd) {
  std::vector<std::unique_ptr<FastCGIMessage>> messages;
  auto m1 = make_unique<FastCGIMessage>(10);
  m1->header.type = FastCGIRecordHeader::STDOUT;
  m1->header.content_len = htons(3);
  m1->message = {'A', 'A', 'A'};
  messages.emplace_back(std::move(m1));

  auto it = messages.cbegin();
  ASSERT_DEATH(FCGIGetStream(FastCGIRecordHeader::STDOUT, &it, messages.end()),
               ".*");
}

TEST(FastCGI, ParseStreamSingle) {
  std::vector<std::unique_ptr<FastCGIMessage>> messages;
  auto m1 = make_unique<FastCGIMessage>(10);
  m1->header.type = FastCGIRecordHeader::STDOUT;
  m1->header.content_len = htons(3);
  m1->message = {'A', 'A', 'A'};
  messages.emplace_back(std::move(m1));

  auto m2 = make_unique<FastCGIMessage>(10);
  m2->header.type = FastCGIRecordHeader::STDOUT;
  m2->header.content_len = 0;
  messages.emplace_back(std::move(m2));

  auto it = messages.cbegin();
  std::vector<char> model_stream = {'A', 'A', 'A'};
  ASSERT_EQ(model_stream,
            FCGIGetStream(FastCGIRecordHeader::STDOUT, &it, messages.end()));
}

TEST(FastCGI, ParseStreamMulti) {
  std::vector<std::unique_ptr<FastCGIMessage>> messages;
  auto m1 = make_unique<FastCGIMessage>(10);
  m1->header.type = FastCGIRecordHeader::STDOUT;
  m1->header.content_len = htons(2);
  m1->message = {'A', 'A'};
  messages.emplace_back(std::move(m1));

  auto m2 = make_unique<FastCGIMessage>(10);
  m2->header.type = FastCGIRecordHeader::STDOUT;
  m2->header.content_len = htons(1);
  m2->message = {'B'};
  messages.emplace_back(std::move(m2));

  auto m3 = make_unique<FastCGIMessage>(10);
  m3->header.type = FastCGIRecordHeader::STDOUT;
  m3->header.content_len = 0;
  messages.emplace_back(std::move(m3));

  auto it = messages.cbegin();
  std::vector<char> model_stream = {'A', 'A', 'B'};
  ASSERT_EQ(model_stream,
            FCGIGetStream(FastCGIRecordHeader::STDOUT, &it, messages.end()));
}

}  // namepsace
}  // namespace web
}  // namespace nc
