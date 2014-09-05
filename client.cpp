#include "http2.hpp"

using namespace std;
namespace asio = boost::asio;
using asio::ip::tcp;

void fetch_proc(asio::yield_context yield, tcp::socket sock) {
  boost::system::error_code ec;
  bool recv_settings_ack = false, send_settings_ack = false;
  ssize_t send_size, recv_size, already;
  vector<uint8_t> recv_buffer(8192);
  vector<asio::const_buffer> send_buffer;

  // 1. SEND magic octets as Connection Preface
  std::cout << "SEND Connection Preface" << std::endl;
  async_write(sock, asio::buffer(CONNECTION_PREFACE), yield[ec]);
  if (ec) return;

  // 2. SEND empty SETTINGS frame
  std::cout << "SEND empty SETTINGS frame" << std::endl;
  Http2FrameHeader req_settings_fh(0, 0x4, 0, 0);
  req_settings_fh.print();
  async_write(sock, asio::buffer(req_settings_fh.write_to_buffer()), yield[ec]);
  if (ec) return;

  // 3,4. RECV SETTINGS frame and ACK
  std::cout << "RECV SETTINGS frame and ACK" << std::endl;
  Http2FrameHeader resp_settings_fh(recv_buffer.data(), FRAME_HEADER_LENGTH);
  recv_size = sock.async_read_some(asio::buffer(recv_buffer), yield[ec]);
  if (ec) return;

  already = 0;
  while (true) {
    if (recv_size - already <= 0) {
      recv_size = sock.async_read_some(asio::buffer(recv_buffer), yield[ec]);
      if (ec) return;
      already = 0;
    }
    resp_settings_fh.read_from_buffer(recv_buffer.data()+already, FRAME_HEADER_LENGTH);
    if (resp_settings_fh.get_type() != 0x4) return;
    resp_settings_fh.print();

    if (resp_settings_fh.get_flags() == 0x1) {
      // Detect ACK
      recv_settings_ack = true;
    } else {
      // Send ACK corresponding to SETTINGS from server.
      Http2FrameHeader ack_settings_fh(0, 0x4, 0x1, 0);
      async_write(sock, asio::buffer(ack_settings_fh.write_to_buffer()), yield[ec]);
      if (ec) return;
      ack_settings_fh.print();
      send_settings_ack = true;
    }
    already += FRAME_HEADER_LENGTH + resp_settings_fh.get_length();

    if (recv_settings_ack && send_settings_ack)
      break;
  }

  // 5. SEND HEADERS frame as request headers
  std::cout << "SEND HEADERS frame as request headers" << std::endl;
  Headers headers;
  headers.push_back(make_pair(":method",    "GET"));
  headers.push_back(make_pair(":scheme",    "http"));
  headers.push_back(make_pair(":authority", "127.0.0.1"));
  headers.push_back(make_pair(":path",      "/"));
  
  send_buffer.clear();
  vector<uint8_t> encoded_headers = get_headers_encoded_by_hpack(headers);
  Http2FrameHeader req_headers_fh(encoded_headers.size(), 0x1, 0x5, 0x1);
  req_headers_fh.print();
  vector<uint8_t> req_headers_fh_vec = req_headers_fh.write_to_buffer();
  send_buffer.push_back(asio::buffer(req_headers_fh_vec));
  send_buffer.push_back(asio::buffer(encoded_headers));
  async_write(sock, send_buffer, yield[ec]);
  if (ec) return;

  // 6. RECV HEADERS frames as response headers
  std::cout << "RECV HEADERS frame as response headers" << std::endl;
  already = 0;
  do {
    recv_size = sock.async_read_some(asio::buffer(recv_buffer), yield[ec]);
    if (ec) return;
    Http2FrameHeader resp_headers_fh(recv_buffer.data(), FRAME_HEADER_LENGTH);
    resp_headers_fh.print();

    if (resp_headers_fh.get_type() == 0x1) {
      already = FRAME_HEADER_LENGTH + resp_headers_fh.get_length();
      break;
    }
  } while (true);
  // Here, we skip a payload of response headers.
  // TODO: To interpret response header, we should implement HACK decoder. :(

  // 7. RECV DATA frame as response body
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
      cout << string(reinterpret_cast<const char*>(recv_buffer.data()+already), resp_data_fh.get_length());
      cout << "---" << endl;
    }
    already += resp_data_fh.get_length();
    if (resp_data_fh.get_flags() & 0x1) {
      break;
    }
  }

  // 8. SEND GOAWAY frame
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
  Http2FrameHeader goaway_fh(0x8, 0x7, 0x1, 0);
  goaway_fh.print();
  send_buffer.push_back(asio::buffer(goaway_fh.write_to_buffer()));
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
