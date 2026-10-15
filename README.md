# Multithreaded TCP File Server with Link Scheduling (COL724/7524 Assignment 2 - Part A)

A high-performance, multithreaded TCP file transfer server and benchmarking client in C++17 with explicit request scheduling (`FCFS`, `SJF`, `Round Robin`, `Deficit Round Robin`), nanosecond-precision metrics logging, and line-level packetization.

---

## 1. Build Instructions

### Prerequisites
* Linux / macOS environment
* `g++` with C++17 support (`-std=c++17`) and POSIX Threads (`-pthread`)
* `nlohmann/json` (vendored header or installed system package)
* `python3` (for workload generation, experiments, and plotting)

### Compilation
To compile both the server (`build/server`) and client (`build/client`):
```bash
make clean && make
```

To run the test suite:
```bash
make test
```

---

## 2. Configuration File (`config.json`)

Both the server and client read configuration parameters from `config.json`:

```json
{
  "server": {
    "ip": "127.0.0.1",
    "port": 9000,
    "server_threads": 4,
    "client_threads": 8
  },
  "load_balancer": {
    "ip": "127.0.0.1",
    "port": 8000,
    "health_interval_ms": 1000,
    "backends": [
      { "ip": "127.0.0.1", "port": 9001 },
      { "ip": "127.0.0.1", "port": 9002 },
      { "ip": "127.0.0.1", "port": 9003 },
      { "ip": "127.0.0.1", "port": 9004 }
    ]
  }
}
```

### Config Field Definitions:
* `server.ip`: IP address the server listens on (and where the client connects).
* `server.port`: TCP port for the server.
* `server.server_threads`: Number of concurrent worker threads in the server pool.
* `server.client_threads`: Number of concurrent client worker threads used during `load` benchmark runs.
* `load_balancer.ip`: IP address for the Part B load balancer.
* `load_balancer.port`: Listening port for the Part B load balancer.
* `load_balancer.health_interval_ms`: Frequency (in ms) of health check probes sent to backends.
* `load_balancer.backends`: Array of exactly 4 backend server targets (`ip` and `port`).

---

## 3. Command-Line Usage

### Server Invocation
```bash
./build/server --config <config-path> --sched <policy> --file <directory> --metrics-out <csv-path> [--quantum <Q>] [--p <N>]
```

#### Server Flags:
* `--sched <policy>` (**Required**): Scheduling policy. Must be one of `fcfs`, `sjf`, `rr`, `drr`.
* `--file <directory>` (**Required**): Directory of files the server serves (`GET`) and stores uploads into (`PUT`).
* `--quantum <Q>` (**Required for `rr` and `drr`, rejected for `fcfs` and `sjf`**): Service quantum allowance in bytes per round.
* `--config <path>` (Optional, default `config.json`): Path to configuration file.
* `--metrics-out <path>` (Optional, default `metrics.csv`): Path to write the per-request CSV upon graceful shutdown.
* `--p <N>` (Optional, default `1`): Packetization factor: gathers up to $N$ whole lines into a single socket write syscall.

---

### Client Invocation
```bash
./build/client <operation> <target> [--config <config-path>] [--requests <N>]
```

#### Client Operations:
1. **Upload a file (`PUT`)**:
   ```bash
   ./build/client put <local-file-path> [--config config.json]
   ```
2. **Download a file (`GET`)**:
   ```bash
   ./build/client get <server-file-name> [--config config.json]
   ```
3. **Run Workload Benchmark (`LOAD`)**:
   ```bash
   ./build/client load <workload-dir> --requests <N> [--config config.json]
   ```
   * Seeds the server by `PUT`ting every file in `<workload-dir>` once (seeding is uncounted in metrics).
   * Spawns `client_threads` concurrent threads issuing $N$ requests total from a shared atomic counter.
   * 50/50 uniform GET/PUT mix, operating closed-loop (each thread waits for full completion before issuing the next).
   * Silent on success (exits `0` with no stdout clutter).

---

## 4. Key Design Choices & Architecture

### A. Explicit Request Queue & Admission (Rule A5)
* Incoming client connections are accepted by the main listener thread, the header line is read with a socket receive timeout (to prevent slow client DoS attacks), and the request size is validated before enqueueing.
* Requests are queued in an explicit, thread-safe queue (`schedulerQueue`).
* Worker threads pop requests from the queue in the order mandated by the active scheduling policy.

### B. Immediate Health Checks (Rule A5)
* `HEALTH\n` probe requests bypass the scheduler queue and are answered immediately out-of-band with `OK <queue_depth>\n`. Health probes are never recorded in `metrics.csv`.

### C. GET Scheduling & Line Packetization (Rules A6, A8)
* On the `GET` path, scheduling granularity is **one whole line** (up to and including `\n`).
* A round never terminates partway through a line.
* `--p N` batches up to $N$ complete lines in memory before calling `send()`, and flushes any partial batch upon round termination or EOF.

### D. Preemption State Preservation (Rule A17)
* For `RR` and `DRR`, preempted requests are requeued at the tail of the scheduling queue.
* All state is preserved:
  * Byte file offset (`bytesTransferred`)
  * Excess unconsumed socket buffer data (`extra`)
  * DRR Deficit counter (`deficit`)

### E. DRR vs RR Deficit Accounting (Rules A13, A14, A15, A16)
* **Round Robin (`rr`)**: If the next line exceeds the remaining quantum allowance, the round ends and the unused bytes are **forfeited**. If the very first line of a round is larger than $Q$, Rule A14 fires: the line is sent in full to avoid starvation.
* **Deficit Round Robin (`drr`)**: Unused allowance accumulates in the request's `deficit` counter (`deficit += Q`). Lines are only sent when `deficit >= lineBytes`. Rule A14 is omitted as deficit naturally grows until the line fits.

### F. Monotonic Instrumentation & Metrics (Rules A18 - A23)
* Uses `CLOCK_MONOTONIC` to record nanosecond timestamps:
  * `arrival_ns`: Timestamp when request header is parsed and admitted to queue.
  * `start_ns`: Timestamp when a worker thread first picks up the request.
  * `finish_ns`: Timestamp when the final byte is transferred.
* On `SIGINT`/`SIGTERM`, the server drains active requests, flushes `metrics.csv`, and prints a shutdown summary.

---

## 5. Reproducing Experiments (Rules A28 & A29)

### Automated 6-Cell Experiment Runner
To reproduce all 6 experiment configurations automatically:
```bash
# 1. Run full 6-cell benchmark suite (1,000 requests each)
python3 tests/run_experiments.py
```

This script:
1. Generates the 4 workload files in `./workload/` (`small.txt`, `medium.txt`, `large.txt`, `longline.txt`).
2. Runs Cells 1 to 6:
   * **Cell 1**: `fcfs` (4 server threads, 8 client threads)
   * **Cell 2**: `sjf` (4 server threads, 8 client threads)
   * **Cell 3**: `rr` (4 server threads, 8 client threads, $Q=4096$)
   * **Cell 4**: `drr` (4 server threads, 8 client threads, $Q=4096$)
   * **Cell 5**: `fcfs` (1 server thread, 8 client threads)
   * **Cell 6**: `rr` (1 server thread, 8 client threads, $Q=4096$)
3. Collects all CSVs into `experiment_results/metrics_cell_*.csv`.
4. Computes nearest-rank percentiles ($p_{50}, p_{99}$), throughput, size-class normalized slowdowns, forfeited bytes, A14 overrun counts, and long-line slowdowns.

### Generating Report Plots
To generate all 5 publication-ready PNG charts:
```bash
# Install matplotlib if needed
sudo apt install -y python3-matplotlib   # or: pip3 install matplotlib

# Generate all charts
python3 tests/generate_plots.py
```
Generated charts in `./plots/`:
* `waiting_time_comparison.png`
* `throughput_comparison.png`
* `slowdown_comparison.png`
* `forfeited_bytes_comparison.png`
* `longline_slowdown_comparison.png`

---

# Part B — Load Balancer

A concurrent TCP **load balancer** that sits in front of four independent
Part A server instances ("backends"), forwards `PUT`/`GET` transparently,
health-checks the backends, and steers each request using a selectable
algorithm. The unmodified Part A client works against the LB with only the
config file changed (B2 transparency).

## B.1 Build

`make` now builds all three binaries: `build/server`, `build/client`, and
`build/load_balancer`.

```bash
make clean && make
```

## B.2 Configuration

The LB reads the `load_balancer` section of `config.json` (ip, port,
`health_interval_ms`, and the four `backends`). The Part A client is pointed
at the LB with a *separate* config whose `server.ip`/`server.port` point at
the LB (`backend_configs/config_lb.json`, generated automatically):

```bash
./build/client get report.txt --config backend_configs/config_lb.json
```

Each backend is its own Part A server on its own port, started with its own
generated config (`backend_configs/backend_<port>.json`). The generator
carries the `load_balancer` section into each backend config verbatim (the
shared parser requires every field, though the server binary ignores it).

## B.3 Running

```bash
# 1. Start the four backends (backend #1 slow for B13; shared file store).
./scripts/start_backends.sh --sched rr --quantum 4096 --slow-backend 1

# 2. Start the load balancer (pick the algorithm, B11).
./build/load_balancer --algo probe    --config config.json --health-log health.csv
./build/load_balancer --algo inflight --config config.json --health-log health.csv
./build/load_balancer --algo roundrobin --config config.json --health-log health.csv

# 3. Use the unmodified Part A client against the LB.
./build/client load workload --requests 1000 --config backend_configs/config_lb.json

# 4. Stop the LB (Ctrl-C / SIGINT) -> graceful drain + per-backend counts.
./scripts/stop_backends.sh
```

### Selecting each algorithm (B11)
`--algo <probe|inflight|roundrobin>` selects the algorithm at startup
(default `roundrobin`). The two required algorithms differ in the *kind of
state* they read (B12):

* **`probe`** — *probe-cadence state*: the minimum last-reported scheduler
  queue depth (B8), refreshed only once per health interval.
* **`inflight`** — *real-time state*: the minimum number of requests currently
  in flight to each backend, updated on every dispatch and completion.
* **`roundrobin`** — stateless baseline (the allowed third algorithm).

## B.4 Health-check design (B7–B10)

* **One prober thread per backend** (B7), so the four probes are fully
  independent — a slow/hung probe of one backend never delays the others.
* Each cycle sends `HEALTH\n` and reads the reply (the backend's current
  scheduler queue depth, B8). A probe **fails** (`status=down`) on a
  connection error, a malformed reply, or no complete reply within one health
  interval (B7).
* Every probe is appended to the health log with columns
  `timestamp,backend,status,rtt_ms,queue_depth` (B9). Design choices:
  `timestamp` is **epoch milliseconds**; `queue_depth` is **-1** for a failed
  probe; `rtt_ms` is integer milliseconds (monotonic clock).
* A failed probe marks the backend **unhealthy** (excluded from selection); a
  later successful probe restores it to service (B10).
* On `SIGINT`/`SIGTERM` the LB stops accepting, drains in-flight exchanges,
  stops the probers, and prints the per-backend forwarded counts (B5).

## B.5 Failure semantics (B6)

* Backend failure **before** any response byte is forwarded → the LB replies
  `ERR <reason>` and closes the connection.
* Backend failure **after** response bytes have started → the LB closes the
  client connection; the client detects the failure as a **short read** (the
  body is a raw byte stream with no room for an error message).
* On a pre-response backend failure the LB does **not** retry another backend
  (a strict reading of B6); it marks the backend unhealthy so selection
  excludes it, and replies `ERR`.

## B.6 Key design choices (documented per the ground rules)

* **Shared file store:** all four backends use one `--file` directory. Since
  the LB is transparent with no sticky sessions, a `GET` can land on any
  backend, so every backend must be able to serve every file. The load
  client's seeding `PUT`s populate the shared store once.
* **No pre-response retry** (see B.5 above).
* **Least-X selection tie-break:** among backends sharing the minimum metric,
  a round-robin scan spreads ties fairly (a deterministic lowest-index
  tie-break would structurally starve higher-index backends). Selection is a
  single pass, which avoids a TOCTOU race (a two-phase "compute min, then
  pick among the minima" can wrongly report no backend when the in-flight
  count changes between phases).

## B.7 Reproducing the Part B experiments

```bash
# B13: per-backend distribution, one deliberately slow backend (backend #1).
./tests/lb/run_b13.sh probe        part_b_results/b13_probe     --requests 1000
./tests/lb/run_b13.sh inflight     part_b_results/b13_inflight  --requests 1000
./tests/lb/run_b13.sh roundrobin   part_b_results/b13_rr        --requests 1000
python3 tests/lb/analyze_b13.py \
    --runs probe:part_b_results/b13_probe \
           inflight:part_b_results/b13_inflight \
           roundrobin:part_b_results/b13_rr \
    --out-plot plots/b13_distribution.png \
    --out-table part_b_results/b13_table.txt

# B14: fault injection (kill -9 one backend mid-load, then restart it).
./tests/lb/run_b14.sh part_b_results/b14_inflight --requests 5000 --algo inflight
python3 tests/lb/analyze_b14.py part_b_results/b14_inflight
```

Experiment artifacts (per-run health logs, per-backend metrics CSVs, LB
summaries, client logs, and the B13 table/plot) live in `part_b_results/`.

---

## 6. Project Structure
```text
.
├── Makefile                     # Build targets for server, client, load_balancer
├── README.md                    # Project documentation & reproduction guide
├── config.json                  # Reference configuration file
├── include/                     # C++ header files
│   ├── cli.hpp
│   ├── config.hpp
│   ├── framing.hpp
│   ├── lb_io.hpp                # (Part B) buffered socket + connect helper
│   ├── load_balancer.hpp        # (Part B) Backend state + shared LB globals
│   ├── nlohmann/json.hpp        # vendored single-header JSON parser
│   ├── request.hpp
│   ├── request_handler.hpp
│   ├── response.hpp
│   ├── scheduler.hpp
│   └── util.hpp
├── src/                         # C++ implementation files
│   ├── cli.cpp
│   ├── client.cpp
│   ├── config.cpp
│   ├── framing.cpp
│   ├── lb_health.cpp            # (Part B) independent per-backend probers (B7-B10)
│   ├── lb_io.cpp                # (Part B) buffered socket implementation
│   ├── lb_proxy.cpp             # (Part B) per-connection transparent proxy (B1/B2/B6)
│   ├── lb_select.cpp            # (Part B) selection algorithms (B11/B12)
│   ├── load_balancer.cpp        # (Part B) LB main: accept loop, shutdown (B4/B5)
│   ├── request.cpp
│   ├── request_handler.cpp
│   ├── response.cpp
│   ├── scheduler.cpp
│   ├── server.cpp
│   └── util.cpp
├── scripts/                     # (Part B) helper scripts
│   ├── gen_backend_configs.py   # generate backend + LB-client configs
│   ├── start_backends.sh        # start the four backends
│   └── stop_backends.sh         # stop the backends gracefully
├── tests/                       # Unit tests & experiment scripts
│   ├── analyze_metrics.py
│   ├── generate_plots.py
│   ├── generate_workload.py
│   ├── run_experiments.py
│   └── lb/                      # (Part B) experiments
│       ├── analyze_b13.py       # per-backend distribution table + plot
│       ├── analyze_b14.py       # fault-injection analysis
│       ├── run_b13.sh           # B13 distribution experiment runner
│       └── run_b14.sh           # B14 fault-injection runner
├── workload/                    # Generated workload text files
├── experiment_results/          # (Part A) metrics CSVs and summary JSON
├── part_b_results/              # (Part B) B13/B14 logs, metrics, summaries
└── plots/                       # Generated comparison charts (Part A + B13)
```
