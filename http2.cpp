#include "http2.hpp"

using namespace std;

vector<uint8_t>
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

