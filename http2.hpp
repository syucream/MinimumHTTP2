#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/array.hpp>
#include <string>
#include <utility>
#include <vector>

static const std::string CONNECTION_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
static const size_t FRAME_HEADER_LENGTH = 9;

typedef std::vector<std::pair<std::string, std::string> > Headers;

/**
 * 4.1.  Frame Format
 * https://tools.ietf.org/html/draft-ietf-httpbis-http2-14#section-4.1
 *
 * Represent frame header of HTTP/2 frames
 */
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
      stream_id = ntohl(*reinterpret_cast<const uint32_t*>(&buffer[5]));
    }
  }

  std::vector<uint8_t> write_to_buffer() {
    std::vector<uint8_t> buffer;

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
    std::cout << "(" <<
            "length: "      << length <<
            ", type: "      << static_cast<uint16_t>(type) <<
            ", flags: "     << static_cast<uint16_t>(flags) <<
            ", stream_id: " << stream_id <<
            ")" << std::endl;
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

std::vector<uint8_t>
get_headers_encoded_by_hpack(Headers headers);
