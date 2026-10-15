#!/usr/bin/env python3
"""
Part B helper: generate the config files needed to run the load balancer
experiment.

Generates, in --out-dir (default: backend_configs/):

  backend_<port>.json   one per backend (B3): a copy of the main config whose
                        "server" section points at that backend's own port.
                        The "load_balancer" section is carried over verbatim
                        (the Part A server binary does not use it, but the
                        shared config parser requires every field to exist).
  config_lb.json        the config the unmodified Part A client uses to talk
                        to the load balancer (B2): a copy of the main config
                        whose server.ip / server.port point at the LB.

One backend may be made deliberately slow (B13): --slow-backend N (1-based)
sets that backend's server_threads to --slow-threads (default 1).

Usage:
  python3 scripts/gen_backend_configs.py \
      --main-config config.json \
      --out-dir backend_configs \
      --slow-backend 1
"""
import argparse
import json
import os
import sys


def load_json(path):
    with open(path) as f:
        return json.load(f)


def write_json(path, obj):
    with open(path, "w") as f:
        json.dump(obj, f, indent=2)
        f.write("\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--main-config", default="config.json")
    ap.add_argument("--out-dir", default="backend_configs")
    ap.add_argument("--slow-backend", type=int, default=None,
                    help="1-based index of the deliberately slow backend (B13)")
    ap.add_argument("--slow-threads", type=int, default=1,
                    help="server_threads for the slow backend (default 1)")
    args = ap.parse_args()

    cfg = load_json(args.main_config)
    os.makedirs(args.out_dir, exist_ok=True)

    if len(cfg["load_balancer"]["backends"]) != 4:
        print("error: backends[] must hold exactly four entries", file=sys.stderr)
        return 1

    for i, be in enumerate(cfg["load_balancer"]["backends"], start=1):
        server = dict(cfg["server"])
        server["ip"] = be["ip"]
        server["port"] = be["port"]
        server["server_threads"] = args.slow_threads if (
            args.slow_backend is not None and i == args.slow_backend
        ) else cfg["server"]["server_threads"]

        backend_cfg = {
            "server": server,
            # Required by the shared parser; unused by the server binary.
            "load_balancer": cfg["load_balancer"],
        }
        path = os.path.join(args.out_dir, f"backend_{be['port']}.json")
        write_json(path, backend_cfg)
        print(f"wrote {path} (threads={server['server_threads']})")

    # LB-facing client config (B2 transparency: same client binary, new config).
    lb_client_cfg = json.loads(json.dumps(cfg))  # deep copy
    lb_client_cfg["server"]["ip"] = cfg["load_balancer"]["ip"]
    lb_client_cfg["server"]["port"] = cfg["load_balancer"]["port"]
    path = os.path.join(args.out_dir, "config_lb.json")
    write_json(path, lb_client_cfg)
    print(f"wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
