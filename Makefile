CXX = g++
CXXFLAGS = -std=c++17 -pthread -Iinclude

SERVER = build/server
CLIENT = build/client
LB = build/load_balancer

# Shared modules used by all three binaries (config parsing, framing, request
# parsing, response sending, CLI, and the LB's socket I/O + selection + proxy).
COMMON = src/config.cpp \
         src/util.cpp \
         src/request.cpp \
         src/framing.cpp \
         src/response.cpp \
         src/cli.cpp

# Part B load balancer sources (no scheduler/request_handler: those are the
# Part A server's internal scheduling engine and are not needed by the LB).
LB_COMMON = $(COMMON) \
            src/lb_io.cpp \
            src/lb_select.cpp \
            src/lb_proxy.cpp \
            src/lb_health.cpp

all: $(SERVER) $(CLIENT) $(LB)

$(SERVER): src/server.cpp $(COMMON) src/scheduler.cpp src/request_handler.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/server.cpp $(COMMON) src/scheduler.cpp src/request_handler.cpp -o $(SERVER)

$(CLIENT): src/client.cpp $(COMMON)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/client.cpp $(COMMON) -o $(CLIENT)

$(LB): src/load_balancer.cpp $(LB_COMMON)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/load_balancer.cpp $(LB_COMMON) -o $(LB)

clean:
	rm -f $(SERVER) $(CLIENT) $(LB)
