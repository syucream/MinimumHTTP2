#include <utility>
#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/asio/spawn.hpp>
#include <vector>
#include <string>

using namespace std;
namespace asio = boost::asio;
using asio::ip::tcp;

static const string CONNECTION_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
static const size_t FRAME_HEADER_LENGTH = 9;
static const string TEST_RESPONSE_BODY = "Hello, HTTP/2!";

typedef vector<pair<string, string> > Headers;

static vector<uint8_t>
get_headers_encoded_by_hpack(Headers headers)
{
  vector<uint8_t> buffer;

  for(auto field : headers) {
    // 7.2.2.  Literal Header Field without Indexing
    buffer.push_back(0);

    // Name Length (without huffman coding)
    buffer.push_back(field.first.length() & 0x7f);

    // Name String
    buffer.insert(buffer.end(), field.first.begin(), field.first.end());
    
    // Value Length (without huffman coding)
    buffer.push_back(field.second.length() & 0x7f);

    // Value String
    buffer.insert(buffer.end(), field.second.begin(), field.second.end());
  }

  return buffer;
}

class Http2FrameHeader {
public:
  Http2FrameHeader()
    : length(0), type(0), flags(0), stream_id(0) {}

  Http2FrameHeader(uint32_t l, uint8_t t, uint8_t f, uint32_t sid)
    : length(l), type(t), flags(f), stream_id(sid) {}
  
  Http2FrameHeader(const uint8_t* buffer, size_t length)
    { read_from_buffer(buffer, length); }

  void read_from_buffer(const uint8_t* buffer, size_t buflen) {
    if (buflen >= FRAME_HEADER_LENGTH) {
      length = (buffer[0] << 16) + (buffer[1] << 8) + buffer[2];
      type = buffer[3];
      flags = buffer[4];
      stream_id = ntohl(static_cast<uint32_t>(buffer[5]));
    }
  }

  vector<uint8_t> write_to_buffer() {
    vector<uint8_t> buffer;

    buffer.push_back((length >> 16) & 0xff);
    buffer.push_back((length >> 8) & 0xff);
    buffer.push_back(length & 0xff);
    buffer.push_back(type);
    buffer.push_back(flags);
    buffer.push_back((stream_id >> 24) & 0xff);
    buffer.push_back((stream_id >> 16) & 0xff);
    buffer.push_back((stream_id >> 8) & 0xff);
    buffer.push_back(stream_id & 0xff);

    return buffer;
  }

  void print() {
    cout << "(length: " << length <<
            ", type: " << static_cast<uint16_t>(type) <<
            ", flags: "  << static_cast<uint16_t>(flags) <<
            ", stream_id: " << stream_id << ")" << endl;
  }

  uint32_t get_length() { return length; }
  uint8_t get_type() { return type; }
  uint8_t get_flags() { return flags; }
  uint32_t get_stream_id() { return stream_id; }

private:
  uint32_t length;
  uint8_t type;
  uint8_t flags;
  uint32_t stream_id;
};

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
