# DSR route repair (10 nodes)

This simulation shows DSR routing in an ns-3.49 Wi-Fi ad hoc network. It uses
the same 10-node layout as `../AODV` and `../DSDV`, so you can compare all three
protocols directly. The network has a short route and a long route between two
nodes. At 15 s, the short route breaks, and DSR finds the long route.

## Files

| File | Contents |
| --- | --- |
| `dsr-route-repair.cc` | The C++ source |
| `dsr-route-repair` | The compiled C++ binary |
| `dsr_route_repair.py` | The Python version (ns-3 cppyy bindings) |

## Is DSR distance vector or link state?

**Neither. DSR is a source-routing protocol.** DSR stands for Dynamic Source
Routing. The other kinds work like this:

- **Distance vector** (RIP, DSDV, AODV): each node keeps, for every
  destination, only a distance and a next hop. A packet is forwarded one hop
  at a time, and each node looks up its own table.
- **Link state** (OLSR, OSPF): every node learns the whole topology from
  link-state advertisements that are flooded through the network. Each node
  then computes shortest paths itself.
- **Source routing** (DSR): the sender writes the complete path into each
  packet's header, such as `A, E, F, G, H, I, J, D`. Nodes along the way don't
  look anything up. They just pass the packet to the next address on the
  list.

DSR finds paths **on demand**, like AODV. When A needs a route to D, it floods
a *route request*. Each node that rebroadcasts the request adds its own
address, so the request records the path it took. D sends that full path back
in a *route reply*. A stores it in its *route cache* and puts it into every
packet it sends to D.

### Where link state comes in

ns-3's DSR has two cache types (`CacheType`):

- **`LinkCache`, the default.** It breaks every path it learns into
  individual links (A-B, B-C, ...) and runs **Dijkstra's shortest-path
  algorithm** over them. That is what link-state protocols do. But here it
  happens inside one node, using only links that appeared in paths that node
  has seen. No node advertises its links to the network, so DSR is still not
  a link-state protocol.
- **`PathCache`.** It stores whole paths and picks the shortest.

### DSR's cost is the hop count

In the link cache, every link has weight 1:

```cpp
// src/dsr/model/dsr-rcache.cc
// Here the weight is set as 1
// TODO May need to set different weight for different link here later
uint32_t weight = 1;
```

So Dijkstra finds the path with the fewest hops. The path cache also sorts by
hop count. There is no setting for per-link costs. Source routing has its own
cost, though: the path rides in every packet, so the header grows by 4 bytes
for each address. You can see this on the air in this simulation: A's route
request goes out as a 68-byte Wi-Fi frame, and by the time J rebroadcasts it,
6 hops later, the frame is 92 bytes.

### How the ns-3 protocols compare

| Protocol | Type | What a node keeps | Cost used for route choice | Per-link cost setting? |
| --- | --- | --- | --- | --- |
| RIP | Distance vector | Distance and next hop | Sum of interface metrics | Yes, `RipHelper::SetInterfaceMetric` |
| DSDV | Distance vector, proactive | Distance, next hop, sequence number | Hop count | No |
| AODV | Distance vector, on demand | Distance, next hop, sequence number | Hop count | No |
| **DSR** | **Source routing, on demand** | **Whole paths, or links plus Dijkstra** | **Hop count** | **No** |
| OLSR | Link state, proactive | The topology | Hop count | No |
| Global routing | Link state, computed centrally | The whole topology | Sum of interface metrics | Yes, `Ipv4::SetMetric` |

### DSR's settings

These are some of DSR's attributes in ns-3, with their defaults. The
simulation sets the first four explicitly, at their default values, so they are
easy to change.

| Attribute | Default | What it does |
| --- | --- | --- |
| `CacheType` | `LinkCache` | `LinkCache` (links plus Dijkstra) or `PathCache` (whole paths) |
| `LinkAcknowledgment` | true | The next hop must acknowledge each data packet |
| `LinkAckTimeout` | 100 ms | How long to wait for that acknowledgment |
| `TryLinkAcks` | 1 | How many times to resend before the link counts as broken |
| `NonPropRequestTimeout` | 30 ms | How long to wait after asking only the neighbours, before flooding |
| `DiscoveryHopLimit` | 255 | The TTL of a flooded route request |
| `RequestPeriod` / `MaxRequestPeriod` | 500 ms / 10 s | The base and maximum gap between repeated route requests |
| `RreqRetries` | 16 | The maximum number of route request retries |
| `MaxSalvageCount` | 15 | How many times nodes along the way may reroute (salvage) one packet |
| `RouteCacheTimeout` | 300 s | How long a cached route lasts |
| `MaxEntriesEachDst` | 20 | How many paths to keep per destination |
| `MaxSendBuffLen` / `MaxSendBuffTime` | 64 / 30 s | Packets held while waiting for a route |
| `BlacklistTimeout` | 3 s | How long a neighbour stays blacklisted |

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
| D | 10.1.1.4 | (135, 0) | Destination, running a UDP echo server |
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
- **Traffic:** A sends a 64-byte UDP echo packet to D once a second, 30
  times, from 1 s to 30 s. D sends each one back. ns-3's DSR doesn't carry
  ICMP, so `ping` can't be used here. UDP echo plays the same role.
- **Break:** at 15 s, B moves to (45, 1000), out of everyone's range.

### How the output is produced

The program follows every echo packet and records each node that transmits
it, in order. DSR keeps a packet's ID as it forwards it, and the echo server
sends the same packet back, so this gives the exact path of each request and
its reply. The program also logs every route request broadcast: who started
it, its TTL, and who rebroadcast it. Neither of these touches DSR's internals.

## Running

### C++

Run the binary:

```bash
~/NS3/DSR/dsr-route-repair
~/NS3/DSR/dsr-route-repair --breakTime=20.3          # break the route at a different time
~/NS3/DSR/dsr-route-repair --cacheType=PathCache     # use the path cache
```

To rebuild it after you change the source, compile it against the ns-3 build:

```bash
cd ~/NS3
B=ns-3.49/build
g++ -std=c++20 -O2 -DNS3_BUILD_PROFILE_RELEASE -I$B/include \
    DSR/dsr-route-repair.cc -o DSR/dsr-route-repair -L$B/lib \
    -l:libns3.49-core-optimized.so -l:libns3.49-network-optimized.so \
    -l:libns3.49-internet-optimized.so -l:libns3.49-applications-optimized.so \
    -l:libns3.49-mobility-optimized.so -l:libns3.49-wifi-optimized.so \
    -l:libns3.49-dsr-optimized.so -Wl,-rpath,$PWD/$B/lib
```

You can also copy the `.cc` file into `ns-3.49/scratch/` and run
`./ns3 run dsr-route-repair`.

### Python

Activate the venv first, so the ns-3 Python bindings can load:

```bash
source ~/NS3/venv/bin/activate
cd ~/NS3/ns-3.49
./ns3 run ../DSR/dsr_route_repair.py
```

To change the setup, edit `BREAK_TIME`, `COUNT` and `CACHE_TYPE` at the top of
the file.

## Results

The C++ and Python versions produce byte-for-byte identical output:

```text
  packet  sent (s)  request path              reply path                round trip
  0       1         A-B-C-D (3 hops)          D-C-B-A (3 hops)          94.3 ms
  1-13    2-14      A-B-C-D (3 hops)          D-C-B-A (3 hops)          10.3-10.9 ms
  *** 15 s: node B moves away ***
  14      15        A-A (lost)                -                         no reply
  15      16        A-E-F-G-H-I-J-D (7 hops)  D-C-C (lost)              no reply
  16-29   17-30     A-E-F-G-H-I-J-D (7 hops)  D-J-I-H-G-F-E-A (7 hops)  26.4-26.9 ms

Route discoveries (route request broadcasts):
    1.000 s  A asks its neighbours only (TTL 1)  rebroadcast by: nobody
    1.030 s  A floods the network (TTL 255)      rebroadcast by: B E C F G H I J
   15.200 s  A floods the network (TTL 255)      rebroadcast by: E F G H I J

30 echo packets sent, 28 replies received, 2 lost
```

### What happened, step by step

| Time | What happened |
| --- | --- |
| 1.000 s | A has no route to D. It first asks only its neighbours (TTL 1), in case one of them has D cached. Nobody does. |
| 1.030 s | After 30 ms (`NonPropRequestTimeout`), A floods the network. The request reaches D both ways, through C and through J. |
| 1.0 to 1.1 s | D replies only to the first copy, which came the short way through C. In ns-3's DSR, D's duplicate check runs before its "am I the target?" check, so the later copy from J is dropped. A learns only the 3-hop path. The first packet took 94 ms, because it waited for this discovery. |
| 2 to 14 s | Every packet carries the source route A-B-C-D, and the replies come back D-C-B-A. That's about 10.5 ms per round trip, with no more route requests. |
| 15.000 s | B is gone. A sends packet 14 to B, and no acknowledgment comes back. |
| 15.100 s | After 100 ms (`LinkAckTimeout`), A resends it once (`TryLinkAcks` = 1). Still no acknowledgment. |
| 15.200 s | A treats the A-B link as broken and deletes every cached route that uses it. That leaves A with no route to D at all. Packet 14 is dropped, and A floods a new route request. E, F, G, H, I and J rebroadcast it, and D replies with the 7-hop path. |
| 16.0 s | Packet 15 reaches D the long way. But D still has the old route back to A through C and B. C tries B twice, at 16.06 and 16.16 s, and that reply is lost. |
| 17.0 s onward | D now uses D-J-I-H-G-F-E-A, without a route request of its own. It picked up the long path from A's packets, which carry the full route. Both directions take 7 hops, about 26.5 ms. |

These steps were checked in a separate test copy of the program, which
logged when each node sent each packet, and probed A's and D's route caches
around the break.

### DSR compared with AODV and DSDV

All three simulations use the same 10 nodes. AODV and DSR break the route at
15 s; DSDV breaks it at 60 s.

| | AODV | DSDV | DSR |
| --- | --- | --- | --- |
| Type | Distance vector, on demand | Distance vector, proactive | Source routing, on demand |
| What a packet carries | The destination only | The destination only | **The whole path** |
| Route ready before traffic? | No. The first packet took 283 ms | Yes, to every node | No. The first packet took 94 ms |
| How a break is noticed | Wi-Fi send failures and Hello messages | It isn't, until a newer sequence number arrives | No acknowledgment from the next hop within 100 ms, twice |
| Time to repair | about 2 s | 15 s | about 2 s: requests were fixed by 16 s, replies by 17 s |
| Packets lost | 2 of 30 | 16 of 90 | 2 of 30 |
| Route after the break | 7 hops via E | 7 hops via E | 7 hops via E, written into each packet |

In this run, DSR and AODV recovered about equally fast. DSR found the new
route 0.2 s after the first packet hit the broken link, but the reply
direction needed one more packet to fix itself. DSR also checks every link it
uses all the time, because each data packet needs an acknowledgment from its
next hop.

## The path cache behaves differently

With `--cacheType=PathCache`, no packets were lost, but routes were never
reused. A and D each started a fresh route discovery for every packet: 30 by
A and 30 by D, each one a neighbours-only request plus a flood. With the
default link cache, A started just two discoveries, at the start and after the
break, and D started none.

```text
  packet  sent (s)  request path              reply path                round trip
  0       1         A-B-C-D (3 hops)          D-C-B-A (3 hops)          133.3 ms
  1-13    2-14      A-B-C-D (3 hops)          D-C-B-A (3 hops)          92.7-111.6 ms
  *** 15 s: node B moves away ***
  14-29   15-30     A-E-F-G-H-I-J-D (7 hops)  D-J-I-H-G-F-E-A (7 hops)  156.6-272.0 ms
```

Nothing was lost because every packet discovered a fresh route, so none was
ever sent on the broken link. The price is a network-wide flood in each
direction for every packet. Round trips were also about 10 times longer before
the break, and 6 to 10 times longer after it. In ns-3.49, the path cache
apparently doesn't find the routes it has learned when the next packet needs
one. I didn't investigate why.

## Notes

- **No ICMP.** ns-3's DSR model doesn't carry ICMP packets, so `ping` doesn't
  work over it. The model documentation says so in "Scope and Limitations".
  Use UDP or TCP traffic.
- **DSR isn't an IP routing protocol in ns-3.** It sits between IP and
  UDP/TCP, as its own protocol (IP protocol 48). So it's installed with
  `DsrMainHelper`, after the internet stack, instead of with
  `InternetStackHelper::SetRoutingHelper`. Also, `PrintRoutingTable` shows
  nothing about DSR's routes.
- **Possibly optimistic.** The ns-3 documentation warns: "DSR uses 'direct'
  calls to neighbor nodes instead of relying on L2 information. This might
  generate overly optimistic performances."
- **Consistent results.** The C++ and Python runs match exactly. Every break
  time I tried (10, 15, 15.5 and 20.3 s) gave the same pattern: 2 lost
  packets, and a new flood 0.2 s after the first packet hit the broken link.
- **Keep links short.** In this network, links of about 47 m or less work
  reliably, even though the cutoff is 60 m. See `../AODV/README.md`.
- **Logging is off.** The `optimized` build profile disables `NS_LOG`, so the
  program reports what happened from trace sources instead.
