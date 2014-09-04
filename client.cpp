#include "http2.hpp"

using namespace std;
namespace asio = boost::asio;
using asio::ip::tcp;

void fetch_proc(asio::yield_context yield, tcp::socket sock) {
  boost::system::error_code ec;
  size_t size;
  vector<uint8_t> buffer(8192);
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
  size = sock.async_read_some(asio::buffer(buffer), yield[ec]);
  if (ec) return;
  // TODO: Check received SETTINGS frame strictly
  
  // 4. Send HEADERS frame as request headers
  std::cout << "SEND HEADERS frame as request headers" << std::endl;
  Headers headers;
  headers.push_back(make_pair(":method",    "GET"));
  headers.push_back(make_pair(":scheme",    "http"));
  headers.push_back(make_pair(":authority", "127.0.0.1"));
  headers.push_back(make_pair(":path",      "/"));
  
  buffer.clear();
  vector<uint8_t> encoded_headers = get_headers_encoded_by_hpack(headers);
  Http2FrameHeader client_headers(encoded_headers.size(), 0x1, 0x5, 0x1);
  vector<uint8_t> fh = client_headers.write_to_buffer();
  buffer.insert(buffer.end(), fh.begin(), fh.end());
  buffer.insert(buffer.end(), encoded_headers.begin(), encoded_headers.end());
  async_write(sock, asio::buffer(buffer), yield[ec]);
  if (ec) return;

  // 5. Receive HEADERS frames as response headers
  std::cout << "RECV HEADERS frame as response headers" << std::endl;
  ssize_t resp_size = sock.async_read_some(asio::buffer(buffer), yield[ec]);
  if (ec) return;
  Http2FrameHeader resp_headers_fh(buffer.data(), size);
  // Here, we skip a payload of response headers.
  // TODO: To interpret response header, we should implement HACK decoder. :(
  ssize_t already = FRAME_HEADER_LENGTH + resp_headers_fh.get_length();

  // 6. Receive DATA frame as response body
  std::cout << "RECV DATA frame as response body" << std::endl;
  Http2FrameHeader resp_data_fh;
  if (resp_size - already > 0) {
    resp_data_fh.read_from_buffer(buffer.data()+already, FRAME_HEADER_LENGTH);

    cout << "---" << endl;
    cout << string(reinterpret_cast<const char*>(buffer.data()+already+FRAME_HEADER_LENGTH), resp_size-already-FRAME_HEADER_LENGTH) << endl;
    cout << "---" << endl;
  } else {
    resp_size = sock.async_read_some(asio::buffer(buffer), yield[ec]);
    if (ec) return;
    resp_data_fh.read_from_buffer(buffer.data(), FRAME_HEADER_LENGTH);
    
    cout << "---" << endl;
    cout << string(reinterpret_cast<const char*>(buffer.data()+FRAME_HEADER_LENGTH), resp_size-FRAME_HEADER_LENGTH) << endl;
    cout << "---" << endl;
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

int main() 
{
  asio::io_service io_service;
  
  asio::spawn(io_service, [&](asio::yield_context yield) { 
    boost::system::error_code ec;

    tcp::socket sock(io_service);
    sock.async_connect(tcp::endpoint(
        // asio::ip::address::from_string("106.186.112.116"), 80), yield[ec]);
        asio::ip::address::from_string("127.0.0.1"), 8080), yield[ec]);

    if (!ec) {
      asio::spawn(io_service, 
          [&](asio::yield_context yc) { fetch_proc(yc, std::move(sock)); });
    }
  });

  io_service.run();

  return 0;
}
