#include <gflags/gflags.h>
#include "fcgi.h"

DEFINE_uint64(port_num, 9000, "Port number to listen on");

using namespace nc::web;

int main(int argc, char *argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  FastCGIMessageQueue input;
  FastCGIMessageQueue output;

  TCPServer<FastCGIRecordHeader> tcp_server(FLAGS_port_num, &input, &output);
  FastCGIServer fcgi_server(&input, &output);

  tcp_server.Start();
  fcgi_server.Start();

  tcp_server.Join();
}
