# DSDV route repair (10 nodes)

This simulation shows DSDV routing in an ns-3.49 Wi-Fi ad hoc network. It
uses the same 10-node layout as `../AODV`, so you can compare the two
protocols directly. The network has a short route and a long route between
two nodes. Partway through, the short route breaks, and DSDV switches to the
long route.

## Files

| File | Contents |
| --- | --- |
| `dsdv-route-repair.cc` | The C++ source |
| `dsdv-route-repair` | The compiled C++ binary |
| `dsdv_route_repair.py` | The Python version (ns-3 cppyy bindings) |

## Is DSDV link state? What is its cost?

**DSDV is a distance-vector protocol, not link state.** DSDV stands for
Destination-Sequenced Distance Vector. Like RIP, each node only knows, for
every destination, the distance and the neighbour to send through (the next
hop). It learns these from its neighbours' tables. A link-state protocol
works differently: every node learns the whole topology and computes the
shortest paths itself, using Dijkstra's algorithm.

**In DSDV, the "distance" is the hop count.** ns-3's DSDV has no setting for
link costs. Every hop adds exactly 1 (`GetHop() + 1` in
`src/dsdv/model/dsdv-routing-protocol.cc`), and the model documentation says
"It uses the hop count as metric in route selection". RIP is different: you
can give each interface a cost with `SetInterfaceMetric`.

**DSDV adds a sequence number to each route.** Every node stamps its own
entry with a sequence number and raises it by 2 in each periodic update
(4, 6, 8, ...). An odd number means "this destination is unreachable".
DSDV chooses routes by two rules, in this order:

1. **Newest first.** A route with a higher sequence number always wins,
   even if it has more hops.
2. **Then shortest.** If two routes have the same sequence number, the one
   with fewer hops wins.

The sequence number is not a cost. It measures how fresh the information is,
and it is what stops DSDV from forming routing loops the way plain distance
vector can. This simulation shows rule 1 in action. See
[Why the repair took 15 s](#why-the-repair-took-15-s).

### How the ns-3 protocols compare

| Protocol | Type | Cost used for route choice | Per-link cost setting? |
| --- | --- | --- | --- |
| RIP | Distance vector | Sum of interface metrics | Yes, `RipHelper::SetInterfaceMetric` |
| DSDV | Distance vector, proactive | Hop count, then sequence number | No |
| AODV | Distance vector, on demand | Hop count, then sequence number | No |
| OLSR | **Link state**, proactive | Hop count | No |
| Global routing | **Link state** (OSPF-style Dijkstra), computed centrally | Sum of interface metrics | Yes, `Ipv4::SetMetric` |

Being link state doesn't automatically give a protocol more cost parameters.
ns-3's OLSR is link state, but it still counts hops. Its main settings are
timers and `Willingness`, which controls how willing a node is to relay
traffic for other nodes. In ns-3, the link-state option that uses per-link
costs is global routing. It computes every route centrally from a complete
view of the topology, so it's meant for fixed networks, typically wired ones.

### DSDV's settings

These are DSDV's attributes in ns-3. They control timing and buffering, not
cost. The simulation sets the first three explicitly, at their default values,
so they are easy to change.

| Attribute | Default | What it does |
| --- | --- | --- |
| `PeriodicUpdateInterval` | 15 s | How often each node broadcasts its whole table |
| `SettlingTime` | 5 s | How long to wait before advertising a route whose hop count changed |
| `Holdtimes` | 3 | Drop a route after this many update intervals of silence (3 x 15 s = 45 s) |
| `EnableWST` | true | Use a weighted settling time: 0.875 x the old settling time + 0.125 x the time since the last update |
| `WeightedFactor` | 0.875 | The weight in that formula |
| `EnableBuffering` | true | Hold packets that have no route yet |
| `MaxQueuedPacketsPerDst` | 5 | How many held packets to keep per destination |
| `MaxQueueTime` | 30 s | How long to hold a packet |
| `MaxQueueLen` | 500 | The maximum number of held packets in total |
| `EnableRouteAggregation` | false | Collect changes for `RouteAggregationTime` (1 s) before sending them |

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
| B | 10.1.1.2 | (45, 0) | Short route. Moves away at 60 s |
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
- **Traffic:** A pings D once a second, 90 times, from 30 s to 119 s.
- **Break:** at 60 s, B moves to (45, 1000), out of everyone's range.
- **Output:** the program logs every change to A's route to D, checking every
  100 ms. It prints A's whole table at 29 s, before the first ping, and at
  120 s. It finishes with a list of the pings that got no reply.

The timing is longer than in the AODV simulation, because DSDV works in
15-second update rounds.

## Running

### C++

Run the binary:

```bash
~/NS3/DSDV/dsdv-route-repair
~/NS3/DSDV/dsdv-route-repair --breakTime=63      # break the route at a different time
~/NS3/DSDV/dsdv-route-repair --pingStart=20 --pingCount=120
```

To rebuild it after you change the source, compile it against the ns-3 build:

```bash
cd ~/NS3
B=ns-3.49/build
g++ -std=c++20 -O2 -DNS3_BUILD_PROFILE_RELEASE -I$B/include \
    DSDV/dsdv-route-repair.cc -o DSDV/dsdv-route-repair -L$B/lib \
    -l:libns3.49-core-optimized.so -l:libns3.49-network-optimized.so \
    -l:libns3.49-internet-optimized.so -l:libns3.49-internet-apps-optimized.so \
    -l:libns3.49-mobility-optimized.so -l:libns3.49-wifi-optimized.so \
    -l:libns3.49-dsdv-optimized.so -Wl,-rpath,$PWD/$B/lib
```

You can also copy the `.cc` file into `ns-3.49/scratch/` and run
`./ns3 run dsdv-route-repair`.

### Python

Activate the venv first, so the ns-3 Python bindings can load:

```bash
source ~/NS3/venv/bin/activate
cd ~/NS3/ns-3.49
./ns3 run ../DSDV/dsdv_route_repair.py
```

To change the timing, edit `BREAK_TIME`, `PING_START` and `PING_COUNT` at the
top of the file.

## Results

The C++ and Python versions produce byte-for-byte identical output.

### Before any traffic, A already has a route to every node

DSDV is proactive: it builds routes before anyone needs them. At 29 s,
before the first ping, A's table already lists all 9 other nodes:

```text
Destination     Gateway         Interface       HopCount        SeqNum
10.1.1.2        10.1.1.2        10.1.1.1        1               4         B, direct
10.1.1.3        10.1.1.2        10.1.1.1        2               4         C, via B
10.1.1.4        10.1.1.2        10.1.1.1        3               4         D, via B (short route)
10.1.1.5        10.1.1.5        10.1.1.1        1               4         E, direct
10.1.1.6        10.1.1.5        10.1.1.1        2               4         F, via E
10.1.1.7        10.1.1.5        10.1.1.1        3               4         G, via E
10.1.1.8        10.1.1.5        10.1.1.1        4               4         H, via E
10.1.1.9        10.1.1.2        10.1.1.1        5               4         I, via B (tie: 5 hops either way)
10.1.1.10       10.1.1.2        10.1.1.1        4               4         J, via B
```

Every hop count is the true minimum. AODV is different: it only looks for a
route when the first packet needs one.

### A's route to D over time

```text
     time   next hop        hops  seq no
    +15.1s   10.1.1.2 (B)    3     4
    +30.1s   10.1.1.2 (B)    3     6
    +45.1s   10.1.1.2 (B)    3     8
*** +60s: node B moves away, breaking the short route ***
    +75.1s   10.1.1.5 (E)    7     12
    +90.1s   10.1.1.5 (E)    7     14
   +105.1s   10.1.1.5 (E)    7     16
   +120.2s   10.1.1.5 (E)    7     18
```

- **Every 15 s:** D sends its next even sequence number, and A refreshes its
  route.
- **After the break:** A kept sending through B for 15 s. It switched to the
  7-hop route at 75.1 s, when sequence number 12 arrived through E.
- **Missing number:** sequence number 10 never reached A.

### Pings

```text
Pings lost (icmp_seq, sent at time pingStart + seq):
  30-45  (sent 60-75 s)

--- 10.1.1.4 ping statistics ---
90 packets transmitted, 74 received, 17% packet loss, time 91000ms
```

| | Before the break | After the repair |
| --- | --- | --- |
| A's route to D | via B, 3 hops | via E, 7 hops |
| Round-trip time | about 9 ms | about 21 ms |

### DSDV compared with AODV

The two simulations use the same 10 nodes. AODV's break happens at 15 s
instead of 60 s, but that doesn't affect the comparison.

| | AODV | DSDV |
| --- | --- | --- |
| Routes built | On demand, when the first packet needs one | In advance, to every node |
| First ping | 283 ms, waiting for route discovery | Route already there. This run's first ping still took 1 s (see the notes) |
| Time to repair the break | About 2 s | 15 s |
| Pings lost | 2 of 30 | 16 of 90 |
| How a break is noticed | Watches for Wi-Fi send failures and missed Hello messages | It isn't. Here, a newer sequence number arrived another way |

## Why the repair took 15 s

ns-3's DSDV has no way to notice that a neighbour has gone. It doesn't send
Hello messages, and it doesn't react when a Wi-Fi transmission fails. AODV
does both. So DSDV only changes a route in two cases:

1. **A newer sequence number arrives by another path** (rule 1 above).
2. **Silence:** nothing is heard from the next hop for 45 s
   (`Holdtimes` x `PeriodicUpdateInterval`).

Here, case 1 happened, but only on the second update round after the break:

| Time | What happened |
| --- | --- |
| 60.0 s | B moves away. A keeps sending pings to B, which are lost. |
| 60.0 to 60.1 s | D sends sequence number 10. It passes D -> J -> I -> H, then is lost between H and G. At that moment, G dropped two of H's update broadcasts because it was already receiving other frames (collisions). G, F, E and A never receive sequence number 10. |
| 75.05 s | D's sequence number 12 reaches E, 6 hops away through F. |
| 75.06 s | E's full-table broadcast carries the new route to A: 7 hops, sequence number 12. |
| 75.1 s | A switches, because 12 is newer than its 8 via B. The newer number wins even though 7 hops is more than 3. |

DSDV updates are Wi-Fi broadcasts, which get no acknowledgement and no retry.
Every node also broadcasts at about the same moment every 15 s. So
collisions are common: in this run, between 13 and 34 frames were dropped in
each update round. That's why one update can disappear along one path.

### Settling time

When a route's hop count changes, DSDV waits before advertising it to
neighbours (`SettlingTime`, weighted by `EnableWST`). This keeps routes from
flapping. In the default run, A switched at once anyway: E's regular 15-second
full-table broadcast went out 11 ms after E learned the new route. In ns-3,
the settling time only holds back the small in-between (triggered) updates.

With `--breakTime=63`, A only switched at 81.7 s, about 6.6 s after D's
update. That matches E holding the new route back for its settling time.

### Changing the break time

These results are from this simulation:

| `--breakTime` | A switches to the 7-hop route | Outage | Pings lost |
| --- | --- | --- | --- |
| 50 | 75.1 s | 25 s | 26 |
| 60 (default) | 75.1 s | 15 s | 16 |
| 63 | 81.7 s, after E's settling time | 19 s | 19 |

The outage depends on when the break happens relative to the 15-second
update rounds, and on which updates survive collisions.

## Things in the output that look odd

**B is still listed after it has gone.** At 120 s, A's table shows
`10.1.1.2 via 10.1.1.5, 3 hops, seq 8`. B left at 60 s, and no node can reach
it. E, in turn, lists B via A, so the two form a loop. This is how it
happened:

1. At 97 s, after 45 s of silence, A dropped its route to B. It broadcast once
   that B was unreachable (sequence number 9, which is odd).
2. That one broadcast collided at E with a ping that F was forwarding. A and F
   are 90 m apart, so they can't hear each other (hidden terminals). E kept
   its old route to B.
3. At 105 s, E advertised its old route, with sequence number 8. A had
   deleted its entry, so it no longer remembered number 9. It accepted the
   old route as new.
4. A and E now keep refreshing each other's entry, so it doesn't expire. In
   a 200 s test run, it was still there at the end.

Full DSDV would keep the "unreachable" entry with sequence number 9 and reject
the older 8. ns-3's model deletes it, which is why the old route can come
back. The loop only affects traffic to B, which has gone anyway.

**The first ping took 1038 ms.** The first ping was sent at 30 s, during an
update round. C's address lookup (ARP) for D collided with the update
broadcasts. ARP waits 1 s before trying again, so the first reply took about
1 s.

**Hop counts can tie.** A reaches I in 5 hops either way (A-B-C-D-J-I or
A-E-F-G-H-I). For each new sequence number, DSDV keeps whichever route
delivers it first, and ignores a later copy with the same number and the same
hop count.

## Notes

- **Keep links short.** In this network, links of about 47 m or less work
  reliably, even though the cutoff is 60 m. With the default
  `LogDistancePropagationLossModel`, a 56 m link was too weak to use. See
  `../AODV/README.md`.
- **Logging is off.** The `optimized` build profile disables `NS_LOG`, so the
  program prints its results with routing-table dumps, the route log and ping
  traces instead. The explanations above were checked in a separate test copy
  of the program, which logged every node's route to D and the Wi-Fi
  `PhyTxBegin`, `PhyRxEnd` and `PhyRxDrop` traces.
- **How the route log works.** DSDV keeps its routing table private, so
  `LookupRoute()` reads A's route by parsing the table printout every 100 ms.
  The printout has no side effects: `PrintRoutingTable` is `const`.
