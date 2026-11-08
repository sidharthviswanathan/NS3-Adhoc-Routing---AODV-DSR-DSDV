# AODV route repair (10 nodes)

This simulation shows AODV routing in an ns-3.49 Wi-Fi ad hoc network. The
network has a short route and a long route between two nodes. Partway
through, the short route breaks, and AODV finds the long route.

## Files

| File | Contents |
| --- | --- |
| `aodv-route-repair.cc` | The C++ source |
| `aodv-route-repair` | The compiled C++ binary |
| `aodv_route_repair.py` | The Python version (ns-3 cppyy bindings) |

## Costs in AODV

AODV has no setting for per-link costs, unlike RIP's `SetInterfaceMetric`.
The only cost it uses is the number of hops. So this simulation offers two
routes with different hop counts, then breaks the cheaper one.

## Topology

```text
A --- B --- C --- D        short route: A-B-C-D          3 hops
|                 |
E                 J
|                 |
F --- G --- H --- I        long route:  A-E-F-G-H-I-J-D  7 hops
```

| Node | Address | Position (m) | Role |
| --- | --- | --- | --- |
| A | 10.1.1.1 | (0, 0) | Source |
| B | 10.1.1.2 | (45, 0) | Short route. Moves away at 15 s |
| C | 10.1.1.3 | (90, 0) | Short route |
| D | 10.1.1.4 | (135, 0) | Destination |
| E | 10.1.1.5 | (0, -45) | Long route |
| F | 10.1.1.6 | (0, -90) | Long route |
| G | 10.1.1.7 | (45, -90) | Long route |
| H | 10.1.1.8 | (90, -90) | Long route |
| I | 10.1.1.9 | (135, -90) | Long route |
| J | 10.1.1.10 | (135, -45) | Long route |

- **Radio:** 802.11b ad hoc at a fixed 1 Mbps, with a 60 m range set by
  `RangePropagationLossModel`.
- **Links:** neighbouring nodes are 45 m apart, so they can hear each other.
  Diagonal pairs are 63.6 m apart, so they can't.
- **Traffic:** A pings D once a second for 30 seconds, starting at 1 s.
- **Break:** at 15 s, B moves to (45, 1000), out of everyone's range.
- **Output:** the program prints A's routing table at 14 s, before the break,
  and at 25 s, after the repair.

## Running

### C++

Run the binary:

```bash
~/NS3/AODV/aodv-route-repair
~/NS3/AODV/aodv-route-repair --breakTime=10     # break the route at a different time
```

To rebuild it after you change the source, compile it against the ns-3 build:

```bash
cd ~/NS3
B=ns-3.49/build
g++ -std=c++20 -O2 -DNS3_BUILD_PROFILE_RELEASE -I$B/include \
    AODV/aodv-route-repair.cc -o AODV/aodv-route-repair -L$B/lib \
    -l:libns3.49-core-optimized.so -l:libns3.49-network-optimized.so \
    -l:libns3.49-internet-optimized.so -l:libns3.49-internet-apps-optimized.so \
    -l:libns3.49-mobility-optimized.so -l:libns3.49-wifi-optimized.so \
    -l:libns3.49-aodv-optimized.so -Wl,-rpath,$PWD/$B/lib
```

You can also copy the `.cc` file into `ns-3.49/scratch/` and run
`./ns3 run aodv-route-repair`.

### Python

Activate the venv first, so the ns-3 Python bindings can load:

```bash
source ~/NS3/venv/bin/activate
cd ~/NS3/ns-3.49
./ns3 run ../AODV/aodv_route_repair.py
```

## Results

The C++ and Python versions give identical results:

| | Before the break (14 s) | After the break (25 s) |
| --- | --- | --- |
| A's route to D (10.1.1.4) | via B (10.1.1.2), **3 hops** | via E (10.1.1.5), **7 hops** |
| Ping TTL | 62 | 58 (4 more hops) |
| Round-trip time | about 9 ms | about 21 ms |

```text
--- 10.1.1.4 ping statistics ---
30 packets transmitted, 28 received, 6% packet loss, time 29566ms
```

- **Route discovery:** the first ping took 283 ms because no route existed
  yet. AODV only searches for a route when traffic needs one.
- **Route repair:** pings 14 and 15 were lost while the short route was
  broken. AODV found the 7-hop route by ping 16. After the break, A's entry
  for B shows `DOWN`.

## Notes

- **Keep links short.** In an earlier layout, some links were 56 m long. They
  were inside the 60 m cutoff, but the default `LogDistancePropagationLossModel`
  made the signal too weak, so the backup route never formed. Links of about
  47 m or less work reliably.
- **Logging is off.** The `optimized` build profile disables `NS_LOG`, so the
  program prints its results with routing-table dumps and ping output instead.
