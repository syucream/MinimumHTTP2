#include "http2.hpp"

using namespace std;
namespace asio = boost::asio;
using asio::ip::tcp;

static const string TEST_RESPONSE_BODY = "Hello, HTTP/2!";

void page_proc(asio::yield_context yield, tcp::socket sock) {
  boost::system::error_code ec;
  size_t size;
  vector<uint8_t> recv_buffer(8192);
  vector<asio::const_buffer> send_buffer;

  // 1. RECV magic octets as Connection Preface
  std::cout << "RECV Connection Preface" << std::endl;
  size = sock.async_read_some(asio::buffer(recv_buffer), yield[ec]);
  if (ec) return;

  cout << "HTTP/2 session started!" << endl;

  // 2. RECV empty SETTINGS frame
  std::cout << "RECV empty SETTINGS frame" << std::endl;
  size = sock.async_read_some(asio::buffer(recv_buffer), yield[ec]);
  if (ec) return;

  // 3. SEND SETTINGS frame as ACK
  std::cout << "SEND SETTINGS frame as ACK" << std::endl;
  Http2FrameHeader client_settings(0, 0x4, 0x1, 0);
  async_write(sock, asio::buffer(client_settings.write_to_buffer()), yield[ec]);
  client_settings.print();
  if (ec) return;

  // 4. RECV HEADERS frame as request headers
  std::cout << "RECV HEADERS frame as request headers" << std::endl;
  size = sock.async_read_some(asio::buffer(recv_buffer), yield[ec]);
  Http2FrameHeader req_header_fh(recv_buffer.data(), recv_buffer.size());
  if (ec) return;
  req_header_fh.print();

  // 5. SEND HEADERS frames as response headers
  std::cout << "SEND HEADERS frame as response headers" << std::endl;
  send_buffer.clear();
  
  Headers headers;
  headers.push_back(make_pair(":status", "200"));
  vector<uint8_t> encoded_headers = get_headers_encoded_by_hpack(headers);
  Http2FrameHeader resp_headers_fh(encoded_headers.size(), 0x1, 0x4, 0x1);
  resp_headers_fh.print();
  vector<uint8_t> resp_headers_fh_vec = resp_headers_fh.write_to_buffer();
  send_buffer.push_back(asio::buffer(resp_headers_fh_vec));
  send_buffer.push_back(asio::buffer(encoded_headers));
  // async_write(sock, send_buffer, yield[ec]);
  // if (ec) return;

  // 6. SEND DATA frame as response body
  std::cout << "SEND DATA frame as response body" << std::endl;
  // send_buffer.clear();
  Http2FrameHeader resp_data_fh(TEST_RESPONSE_BODY.size(), 0x0, 0x1, 0x1);
  resp_data_fh.print();
  vector<uint8_t> resp_data_fh_vec = resp_headers_fh.write_to_buffer();
  send_buffer.push_back(asio::buffer(resp_data_fh_vec));
  send_buffer.push_back(asio::buffer(TEST_RESPONSE_BODY));
  async_write(sock, send_buffer, yield[ec]);
  if (ec) return;

  // 7. RECV GOAWAY frame
  std::cout << "RECV GOAWAY frame" << std::endl;
  size = sock.async_read_some(asio::buffer(recv_buffer), yield[ec]);
  if (ec) return;
  
  cout << "HTTP/2 session terminated!" << endl;
}

int main() 
{
  asio::io_service io_service;
  
  asio::spawn(io_service, [&](asio::yield_context yield) { 
    boost::system::error_code ec;

    tcp::socket sock(io_service);
    tcp::acceptor acceptor(io_service, tcp::endpoint(asio::ip::address::from_string("127.0.0.1"), 8080));

    while (true) {
      acceptor.async_accept(sock, yield[ec]);

      if (!ec) {
        asio::spawn(io_service, 
            [&](asio::yield_context yc) { page_proc(yc, std::move(sock)); });
      }
    }
  });

  io_service.run();

  return 0;
}