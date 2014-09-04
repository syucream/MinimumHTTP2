#include "http2.hpp"

using namespace std;
namespace asio = boost::asio;
using asio::ip::tcp;

void fetch_proc(asio::yield_context yield, tcp::socket sock) {
  boost::system::error_code ec;
  ssize_t send_size, recv_size;
  vector<uint8_t> recv_buffer(8192);
  vector<asio::const_buffer> send_buffer;

  // 1. Send magic octets as Connection Preface
  std::cout << "SEND Connection Preface" << std::endl;
  async_write(sock, asio::buffer(CONNECTION_PREFACE), yield[ec]);
  if (ec) return;

  // 2. Send empty SETTINGS frame
  std::cout << "SEND empty SETTINGS frame" << std::endl;
  Http2FrameHeader client_settings(0, 0x4, 0, 0);
  async_write(sock, asio::buffer(client_settings.write_to_buffer()), yield[ec]);
  if (ec) return;

  // 3. Receive SETTINGS frame as ACK
  std::cout << "RECV SETTINGS frame as ACK" << std::endl;
  recv_size = sock.async_read_some(asio::buffer(recv_buffer), yield[ec]);
  if (ec) return;
  // TODO: Check received SETTINGS frame strictly
  // TODO: If SETTINGS was send by server, we send ACK for it.

  // 4. Send HEADERS frame as request headers
  std::cout << "SEND HEADERS frame as request headers" << std::endl;
  Headers headers;
  headers.push_back(make_pair(":method",    "GET"));
  headers.push_back(make_pair(":scheme",    "http"));
  headers.push_back(make_pair(":authority", "127.0.0.1"));
  headers.push_back(make_pair(":path",      "/"));
  
  send_buffer.clear();
  vector<uint8_t> encoded_headers = get_headers_encoded_by_hpack(headers);
  Http2FrameHeader client_headers(encoded_headers.size(), 0x1, 0x5, 0x1);
  vector<uint8_t> req_headers_fh = client_headers.write_to_buffer();
  send_buffer.push_back(asio::buffer(req_headers_fh));
  send_buffer.push_back(asio::buffer(encoded_headers));
  async_write(sock, send_buffer, yield[ec]);
  if (ec) return;

  // 5. Receive HEADERS frames as response headers
  std::cout << "RECV HEADERS frame as response headers" << std::endl;
  ssize_t already = 0;
  do {
    recv_size = sock.async_read_some(asio::buffer(recv_buffer), yield[ec]);
    if (ec) return;
    Http2FrameHeader resp_headers_fh(recv_buffer.data(), recv_size);
    resp_headers_fh.print();

    if (resp_headers_fh.get_type() == 0x1) {
      already = FRAME_HEADER_LENGTH + resp_headers_fh.get_length();
      break;
    }
  } while (true);
  // Here, we skip a payload of response headers.
  // TODO: To interpret response header, we should implement HACK decoder. :(

  // 6. Receive DATA frame as response body
  std::cout << "RECV DATA frame as response body" << std::endl;
  Http2FrameHeader resp_data_fh;

  while (true) {
    if (recv_size - already > 0) {
      resp_data_fh.read_from_buffer(recv_buffer.data()+already, FRAME_HEADER_LENGTH);
      already += FRAME_HEADER_LENGTH;
      resp_data_fh.print();
    } else {
      recv_size = sock.async_read_some(asio::buffer(recv_buffer), yield[ec]);
      if (ec) return;
      resp_data_fh.read_from_buffer(recv_buffer.data(), FRAME_HEADER_LENGTH);
      already = FRAME_HEADER_LENGTH;
      resp_data_fh.print();
    }
    
    if (resp_data_fh.get_length() > 0) {
      cout << "---" << endl;
      cout << string(reinterpret_cast<const char*>(recv_buffer.data()+already), resp_data_fh.get_length()) << endl;
      cout << "---" << endl;
    }
    already += resp_data_fh.get_length();
    if (resp_data_fh.get_flags() == 0x1) {
      break;
    }
  }

  // 7. Send GOAWAY frame
  std::cout << "SEND GOAWAY frame" << std::endl;
  send_buffer.clear();
  vector<uint8_t> goaway_payload;
  // Last-Stream-ID = 1
  goaway_payload.push_back(0x0);
  goaway_payload.push_back(0x0);
  goaway_payload.push_back(0x0);
  goaway_payload.push_back(0x1);
  // Error Code = 0
  goaway_payload.push_back(0x0);
  goaway_payload.push_back(0x0);
  goaway_payload.push_back(0x0);
  goaway_payload.push_back(0x0);
  Http2FrameHeader client_goaway(0x8, 0x7, 0x1, 0);
  send_buffer.push_back(asio::buffer(client_goaway.write_to_buffer()));
  send_buffer.push_back(asio::buffer(goaway_payload));
  async_write(sock, send_buffer, yield[ec]);
  if (ec) return;
  
}

int main(int argc, char* argv[])
{
  string server_ip = "127.0.0.1";
  int server_port  = 80;
  asio::io_service io_service;

  if (argc == 3) {
    server_ip = argv[1];
    server_port = stoi(argv[2]);
  }
  
  asio::spawn(io_service, [&](asio::yield_context yield) { 
    boost::system::error_code ec;

    tcp::socket sock(io_service);
    sock.async_connect(tcp::endpoint(
        asio::ip::address::from_string(server_ip), server_port), yield[ec]);

    if (!ec) {
      asio::spawn(io_service, 
          [&](asio::yield_context yc) { fetch_proc(yc, std::move(sock)); });
    }
  });

  io_service.run();

  return 0;
}
