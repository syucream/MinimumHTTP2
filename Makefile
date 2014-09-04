.PHONY: all clean

LDFLAGS = -g -lboost_coroutine -lboost_context -lboost_system
CLIENT_SRC = client.cpp
CLIENT_BIN = http2_client
SERVER_SRC = server.cpp
SERVER_BIN = http2_server

ifeq ($(CXX),g++)
	STDFLAGS = -std=c++0x
else
	STDFLAGS = -std=c++11
endif


all: server client

server:
	$(CXX) $(STDFLAGS) $(SERVER_SRC) $(LDFLAGS) -o $(SERVER_BIN)

client:
	$(CXX) $(STDFLAGS) $(CLIENT_SRC) $(LDFLAGS) -o $(CLIENT_BIN)

clean:
	rm $(OUTPUT);
	find . -name "*.o" -exec rm {} \;
