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
