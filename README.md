MinimumHTTP2
============

Minimum HTTP/2 client/server implementation.

Usage
------

* client

```
./http2_client <server ip> <server port>
```

* server

```
./http2_server <server port>
```

Limitations
------------

* Number of available stream is limited only one.
* Stream has no state. It can only process frames ordered ideally.
* Almost of parts of HPACK encoder/decoder isn't implemented.
  - Current implementation can encode by only `Literal Header Field without Indexing`
  - Max size of Name and Value length is limited 7 bits.

Requirements
------------

* C++11
* Boost >= 1.54

References
----------

* https://github.com/http2jp/http2jp.github.io/wiki/HTTP2.0-%E6%9C%80%E9%80%9F%E5%AE%9F%E8%A3%85%E6%B3%95
* http://www.slideshare.net/KaoruMaeda/http2simple
