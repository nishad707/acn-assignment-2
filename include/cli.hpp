#pragma once

#include <string>

#include "util.hpp"  // for LB_ALGO_* default values below

struct ServerOptions {
    std::string scheduler;
    int quantum = 0;
    std::string fileDirectory;
    int packetization = 1;
    std::string configPath = "config.json";
    std::string metricsOutput = "metrics.csv";
};

struct ClientOptions {
    std::string command;
    std::string path;
    std::string configPath = "config.json";
    int requests = -1;
};

/**
 * Part B: command-line options for the load balancer.
 *
 *   --algo <name>      selection algorithm: "probe", "inflight" or
 *                      "roundrobin" (B11). Defaults to "roundrobin" (the
 *                      stateless baseline); the two required algorithms of
 *                      B12 are selected explicitly.
 *   --config <path>    config file (default config.json); the LB reads the
 *                      "load_balancer" section only.
 *   --health-log <p>   where to append the per-probe health log (B9),
 *                      default health_log.csv.
 */
struct LoadBalancerOptions {
    std::string algorithm = LB_ALGO_ROUNDRR;
    std::string configPath = "config.json";
    std::string healthLogPath = "health_log.csv";
};

bool parseServerArguments(int argc, char* argv[], ServerOptions &options);
bool parseClientArguments(int argc, char* argv[], ClientOptions &options);
bool parseLoadBalancerArguments(int argc, char* argv[], LoadBalancerOptions &options);