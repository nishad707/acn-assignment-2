#pragma once

#include <string>
#include <string_view>
#include <cstdint>

constexpr const char* ARG_SCHED = "--sched";
constexpr const char* ARG_QUANTUM = "--quantum";
constexpr const char* ARG_FILE = "--file";
constexpr const char* ARG_PACKETIZATION = "--p";
constexpr const char* ARG_CONFIG = "--config";
constexpr const char* ARG_METRICS_OUT = "--metrics-out";

constexpr const char* CLIENT_ARG_CONFIG = "--config";
constexpr const char* CLIENT_ARG_REQUESTS = "--requests";

constexpr const char* SCHED_FCFS = "fcfs";
constexpr const char* SCHED_SJF = "sjf";
constexpr const char* SCHED_ROUND_ROBIN = "rr";
constexpr const char* SCHED_DRR = "drr";

// Load balancer CLI flags (Part B, B11: algorithms selectable at startup).
constexpr const char* LB_ARG_ALGO = "--algo";
constexpr const char* LB_ARG_CONFIG = "--config";
constexpr const char* LB_ARG_HEALTH_LOG = "--health-log";

// The three selectable load-balancing algorithms (B11/B12).
// "probe"    : probe-cadence state (last reported queue depth, refreshed once
//              per health interval) — the B12 "probe-cadence" arm.
// "inflight" : real-time state observed at the LB itself (requests currently
//              in flight to each backend, updated on every dispatch/completion)
//              — the B12 "real-time" arm.
// "roundrobin": baseline that reads no state (allowed as a third algorithm).
constexpr const char* LB_ALGO_PROBE = "probe";
constexpr const char* LB_ALGO_INFLIGHT = "inflight";
constexpr const char* LB_ALGO_ROUNDRR = "roundrobin";

constexpr const char* REQUEST_GET = "GET";
constexpr const char* REQUEST_PUT = "PUT";
constexpr const char* REQUEST_HEALTH = "HEALTH";

constexpr const char* RESPONSE_OK = "OK";
constexpr const char* RESPONSE_ERROR = "ERROR";

constexpr const char* STATUS_OK = "OK";
constexpr const char* STATUS_ERROR = "ERROR";

std::string toUpper(std::string_view sv);
bool setReceiveTimeout(int socket, int seconds);
bool parsePositiveInt(const std::string &valueString, int &result);
bool getFileSize(const std::string &filePath, size_t &fileSize);
uint64_t timespecToNanoseconds(const timespec& time);