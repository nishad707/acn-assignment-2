#include "cli.hpp"

#include <iostream>
#include <string>
#include "util.hpp"

using namespace std;

bool nextIndexValid(int argc, int currentIndex);
bool loadArgValuesInServerOptions(int argc, char* argv[], ServerOptions &options, 
    bool &schedulerProvided, bool &fileProvided, bool &quantumProvided);

bool loadArgValuesInClientOptions(
    int argc,
    char* argv[],
    ClientOptions &options,
    bool &commandProvided,
    bool &pathProvided,
    bool &requestsProvided
);

bool parseServerArguments(int argc, char* argv[], ServerOptions &options) {
    bool schedulerProvided = false;
    bool fileProvided = false;
    bool quantumProvided = false;

    bool loadSuccess = loadArgValuesInServerOptions(argc, argv, options, 
        schedulerProvided, fileProvided, quantumProvided);    

    if(!loadSuccess) {
        return false;
    }

    if (!schedulerProvided) {
        cerr << "error: missing required argument " << ARG_SCHED << endl;
        return false;
    }

    if (!fileProvided) {
        cerr << "error: missing required argument " << ARG_FILE << endl;
        return false;
    }

    if (options.scheduler != SCHED_FCFS && options.scheduler != SCHED_SJF &&
            options.scheduler != SCHED_ROUND_ROBIN && options.scheduler != SCHED_DRR) {

        cerr << "Error: invalid --sched value: " << options.scheduler << endl;
        return false;
    }

    bool quantumRequired = options.scheduler == SCHED_ROUND_ROBIN 
        || options.scheduler == SCHED_DRR;

    if(quantumRequired && !quantumProvided) {
        cerr << "error: --quantum is required for "<< options.scheduler << endl;
        return false;
    }

    if(!quantumRequired && quantumProvided) {
        cerr << "error: --quantum is only valid for rr or drr" << endl;
        return false;
    }

    return true;
}  

bool loadArgValuesInServerOptions(int argc, char* argv[], ServerOptions &options, 
    bool &schedulerProvided, bool &fileProvided, bool &quantumProvided) {

    for(int i=1; i<argc; i++) {
        std::string argument = argv[i];

        if(argument == ARG_SCHED) {
            if(!nextIndexValid(argc, i)) {
                cerr << "Error: missing value for " << argument << endl;
                return false;
            }

            options.scheduler = argv[++i];
            schedulerProvided = true;

        } else if(argument == ARG_QUANTUM) {
            if(!nextIndexValid(argc, i)) {
                cerr << "Error: missing value for " << argument << endl;
                return false;
            }

            string quantumString =  argv[++i];
            int quantum;

            if(!parsePositiveInt(quantumString, quantum)) {
                cerr << "error: invalid --quantum" << endl;
                return false;
            }

            options.quantum = quantum;
            quantumProvided = true;

        } else if(argument == ARG_FILE) {
            if(!nextIndexValid(argc, i)) {
                cerr << "Error: missing value for " << argument << endl;
                return false;
            }

            options.fileDirectory = argv[++i];
            fileProvided = true;

        } else if(argument == ARG_PACKETIZATION) {
            if(!nextIndexValid(argc, i)) {
                cerr << "Error: missing value for " << argument << endl;
                return false;
            }

            string packetizationString =  argv[++i];
            int packetization;

            if(!parsePositiveInt(packetizationString, packetization)) {
                cerr << "error: invalid --p" << endl;
                return false;
            }

            options.packetization = packetization;
        } else if(argument == ARG_CONFIG) {
            if(!nextIndexValid(argc, i)) {
                cerr << "Error: missing value for " << argument << endl;
                return false;
            }

            options.configPath = argv[++i];

        } else if(argument == ARG_METRICS_OUT) {
            if(!nextIndexValid(argc, i)) {
                cerr << "Error: missing value for " << argument << endl;
                return false;
            }

            options.metricsOutput = argv[++i];

        } else {
            cerr << "Error: unknown argument " << argument << endl;
            return false;
        }
    }

    return true;
}

bool parseClientArguments(int argc, char* argv[], ClientOptions &options)
{
    bool commandProvided = false;
    bool pathProvided = false;
    bool requestsProvided = false;

    bool loadSuccess = loadArgValuesInClientOptions(
        argc,
        argv,
        options,
        commandProvided,
        pathProvided,
        requestsProvided
    );

    if (!loadSuccess) {
        return false;
    }

    if (!commandProvided) {
        cerr << "error: missing command" << endl;
        return false;
    }

    if (options.command != "put" &&
        options.command != "get" &&
        options.command != "load") {
        cerr << "error: invalid command: " << options.command << endl;
        return false;
    }

    if (!pathProvided) {
        cerr << "error: missing path/name for " << options.command << endl;
        return false;
    }

    if (options.command == "load" && !requestsProvided) {
        cerr << "error: --requests is required for load" << endl;
        return false;
    }

    if (options.command != "load" && requestsProvided) {
        cerr << "error: --requests is only valid for load" << endl;
        return false;
    }

    if (requestsProvided && options.requests <= 0) {
        cerr << "error: --requests must be positive" << endl;
        return false;
    }

    return true;
}

bool loadArgValuesInClientOptions(
    int argc,
    char* argv[],
    ClientOptions &options,
    bool &commandProvided,
    bool &pathProvided,
    bool &requestsProvided)
{
    if (argc < 2) {
        cerr << "Error: missing command" << endl;
        return false;
    }

    // First positional argument is the command.
    options.command = argv[1];
    commandProvided = true;

    // Second positional argument is the path/name.
    if (argc >= 3 && argv[2][0] != '-') {
        options.path = argv[2];
        pathProvided = true;
    }

    for (int i = 3; i < argc; i++) {
        std::string argument = argv[i];

        if (argument == CLIENT_ARG_CONFIG) {
            if (!nextIndexValid(argc, i)) {
                cerr << "Error: missing value for " << argument << endl;
                return false;
            }

            options.configPath = argv[++i];

        } else if (argument == CLIENT_ARG_REQUESTS) {
            if (!nextIndexValid(argc, i)) {
                cerr << "Error: missing value for " << argument << endl;
                return false;
            }

            std::string requestsString = argv[++i];
            int requests;

            if (!parsePositiveInt(requestsString, requests)) {
                cerr << "error: invalid --requests" << endl;
                return false;
            }

            options.requests = requests;
            requestsProvided = true;

        } else {
            cerr << "Error: unknown argument " << argument << endl;
            return false;
        }
    }

    return true;
}

bool nextIndexValid(int argc, int currentIndex) {
    return (currentIndex + 1) < argc;
}

/**
 * Part B: parse the load balancer command line (B11).
 *
 * Flags:
 *   --algo <probe|inflight|roundrobin>   selection algorithm (default: roundrobin)
 *   --config <path>                      config file (default config.json)
 *   --health-log <path>                  health log file (default health_log.csv)
 *
 * Returns true on success; prints a clear error and returns false otherwise.
 */
bool parseLoadBalancerArguments(int argc, char* argv[], LoadBalancerOptions &options) {
    for (int i = 1; i < argc; i++) {
        std::string argument = argv[i];

        if (argument == LB_ARG_ALGO) {
            if (!nextIndexValid(argc, i)) {
                cerr << "error: missing value for " << argument << endl;
                return false;
            }
            options.algorithm = argv[++i];
            // Validate the algorithm name up front (B11: selectable at startup).
            if (options.algorithm != LB_ALGO_ROUNDRR) {
                cerr << "error: invalid --algo value: " << options.algorithm
                     << " (expected roundrobin)" << endl;
                return false;
            }
        } else if (argument == LB_ARG_CONFIG) {
            if (!nextIndexValid(argc, i)) {
                cerr << "error: missing value for " << argument << endl;
                return false;
            }
            options.configPath = argv[++i];
        } else if (argument == LB_ARG_HEALTH_LOG) {
            if (!nextIndexValid(argc, i)) {
                cerr << "error: missing value for " << argument << endl;
                return false;
            }
            options.healthLogPath = argv[++i];
        } else {
            cerr << "error: unknown argument " << argument << endl;
            return false;
        }
    }
    return true;
}