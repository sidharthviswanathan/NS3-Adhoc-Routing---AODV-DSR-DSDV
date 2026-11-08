# Ten nodes in a Wi-Fi ad hoc network, routed by DSDV (proactive distance
# vector). DSDV's cost (metric) is the hop count: every hop adds exactly 1.
# It picks routes by two rules, in this order:
#   1. Prefer the route with the newest (highest) destination sequence number.
#   2. If two routes have the same sequence number, prefer fewer hops.
#
#   A(0,0) --- B(45,0) --- C(90,0) --- D(135,0)     short route: A-B-C-D         (3 hops)
#     |                                   |
#   E(0,-45)                          J(135,-45)
#     |                                   |
#   F(0,-90) -- G(45,-90) -- H(90,-90) -- I(135,-90) long route: A-E-F-G-H-I-J-D (7 hops)
#
# Neighbors are 45 m apart and the radio range is 60 m, so diagonal pairs
# (63.6 m) cannot hear each other. A pings D once a second. Partway through,
# B moves far away, breaking the short route, and DSDV must switch to the
# long route. The program logs every change to A's route to D.
from ns import ns

BREAK_TIME = 60.0  # when node B moves away (s)
PING_START = 30.0  # time of the first ping (s)
PING_COUNT = 90  # number of pings, one per second

# C++ helpers: watch a route, track ping replies, and move a node
ns.cppyy.cppdef("""
    #include <iomanip>
    #include <set>
    #include <sstream>
    using namespace ns3;

    struct Route
    {
        bool found = false;
        std::string nextHop;
        uint32_t hops = 0;
        uint32_t seqNo = 0;
    };

    // Name a 10.1.1.x address by its node letter, such as "10.1.1.2 (B)"
    std::string Label(const std::string& address)
    {
        auto lastOctet = std::stoul(address.substr(address.rfind('.') + 1));
        return address + " (" + char('A' + lastOctet - 1) + ")";
    }

    // Read a node's route to dst. DSDV keeps its table private, so this
    // parses the table printout (Destination Gateway Interface HopCount SeqNum ...).
    Route LookupRoute(Ptr<Node> node, Ipv4Address dst)
    {
        std::ostringstream table;
        node->GetObject<Ipv4>()->GetRoutingProtocol()->PrintRoutingTable(
            Create<OutputStreamWrapper>(&table));
        std::ostringstream wanted;
        wanted << dst;
        std::istringstream lines(table.str());
        std::string line;
        while (std::getline(lines, line))
        {
            std::istringstream fields(line);
            std::string destination, iface;
            Route route;
            if (fields >> destination >> route.nextHop >> iface >> route.hops >> route.seqNo &&
                destination == wanted.str())
            {
                route.found = true;
                return route;
            }
        }
        return Route{};
    }

    // Check the route every 100 ms and print it whenever it changes
    void WatchRoute(Ptr<Node> node, Ipv4Address dst, Route previous)
    {
        Route now = LookupRoute(node, dst);
        if (now.found != previous.found || now.nextHop != previous.nextHop ||
            now.hops != previous.hops || now.seqNo != previous.seqNo)
        {
            std::cout << std::setw(9) << Simulator::Now().As(Time::S) << "   ";
            if (now.found)
            {
                std::cout << std::left << std::setw(16) << Label(now.nextHop) << std::setw(6)
                          << now.hops << now.seqNo << std::right << "\\n";
            }
            else
            {
                std::cout << "(no route)\\n";
            }
        }
        Simulator::Schedule(MilliSeconds(100), &WatchRoute, node, dst, now);
    }

    void StartWatching(Ptr<Node> node, Ipv4Address dst)
    {
        Simulator::Schedule(Seconds(0), &WatchRoute, node, dst, Route{});
    }

    void DoMove(Ptr<Node> node, Vector position)
    {
        node->GetObject<MobilityModel>()->SetPosition(position);
        std::cout << "*** " << Simulator::Now().As(Time::S)
                  << ": node B moves away, breaking the short route ***\\n";
    }

    void MoveNodeAt(Ptr<Node> node, double seconds, Vector position)
    {
        Simulator::Schedule(Seconds(seconds), &DoMove, node, position);
    }

    std::set<uint16_t> g_replies; // sequence numbers of the pings that got a reply

    void PingReply(uint16_t seq, Time rtt)
    {
        g_replies.insert(seq);
    }

    void TrackPings(Ptr<Application> ping)
    {
        ping->TraceConnectWithoutContext("Rtt", MakeCallback(&PingReply));
    }

    // Print which pings got no reply, as ranges of sequence numbers
    void PrintLostPings(uint32_t count, double pingStart)
    {
        std::cout << "\\nPings lost (icmp_seq, sent at time pingStart + seq):";
        uint32_t lost = 0;
        for (uint32_t seq = 0; seq < count; ++seq)
        {
            if (g_replies.count(seq))
            {
                continue;
            }
            uint32_t last = seq;
            while (last + 1 < count && !g_replies.count(last + 1))
            {
                ++last;
            }
            std::cout << "\\n  " << seq << (last > seq ? "-" + std::to_string(last) : "")
                      << "  (sent " << pingStart + seq << "-" << pingStart + last << " s)";
            lost += last - seq + 1;
            seq = last;
        }
        std::cout << (lost ? "" : " none") << "\\n";
    }

    void PrintLostPingsAt(double seconds, uint32_t count, double pingStart)
    {
        Simulator::Schedule(Seconds(seconds), &PrintLostPings, count, pingStart);
    }

    void PrintRoutesOf(Ptr<Node> node, double seconds)
    {
        Ptr<OutputStreamWrapper> out = Create<OutputStreamWrapper>(&std::cout);
        Ipv4RoutingHelper::PrintRoutingTableAt(Seconds(seconds), node, out);
    }
""")
cpp = ns.cppyy.gbl

# Ten nodes, A to J (addresses 10.1.1.1 to 10.1.1.10)
LAYOUT = [
    (0, 0),  # A  10.1.1.1  source
    (45, 0),  # B  10.1.1.2
    (90, 0),  # C  10.1.1.3
    (135, 0),  # D  10.1.1.4  destination
    (0, -45),  # E  10.1.1.5
    (0, -90),  # F  10.1.1.6
    (45, -90),  # G  10.1.1.7
    (90, -90),  # H  10.1.1.8
    (135, -90),  # I  10.1.1.9
    (135, -45),  # J  10.1.1.10
]
nodes = ns.NodeContainer()
nodes.Create(len(LAYOUT))

mobility = ns.MobilityHelper()
positions = ns.CreateObject[ns.ListPositionAllocator]()
for x, y in LAYOUT:
    positions.Add(ns.Vector(x, y, 0))
mobility.SetPositionAllocator(positions)
mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel")
mobility.Install(nodes)

# Wi-Fi radios in ad hoc mode, with a 60 m range
wifi = ns.WifiHelper()
wifi.SetStandard(ns.WIFI_STANDARD_80211b)
wifi.SetRemoteStationManager(
    "ns3::ConstantRateWifiManager",
    "DataMode", ns.StringValue("DsssRate1Mbps"),
    "ControlMode", ns.StringValue("DsssRate1Mbps"),
)
channel = ns.YansWifiChannelHelper.Default()
channel.AddPropagationLoss("ns3::RangePropagationLossModel", "MaxRange", ns.DoubleValue(60.0))
phy = ns.YansWifiPhyHelper()
phy.SetChannel(channel.Create())
mac = ns.WifiMacHelper()
mac.SetType("ns3::AdhocWifiMac")
devices = wifi.Install(phy, mac, nodes)

# Use DSDV for routing. These are its defaults, set here so they are easy
# to find and change.
dsdv = ns.DsdvHelper()
dsdv.Set("PeriodicUpdateInterval", ns.TimeValue(ns.Seconds(15)))  # full table broadcast
dsdv.Set("SettlingTime", ns.TimeValue(ns.Seconds(5)))  # wait before advertising a new metric
dsdv.Set("Holdtimes", ns.UintegerValue(3))  # purge after 3 x 15 s of silence
internet = ns.InternetStackHelper()
internet.SetRoutingHelper(dsdv)
internet.Install(nodes)

address = ns.Ipv4AddressHelper()
address.SetBase(ns.Ipv4Address("10.1.1.0"), ns.Ipv4Mask("255.255.255.0"))
interfaces = address.Assign(devices)

# A pings D once a second
ping = ns.PingHelper(interfaces.GetAddress(3).ConvertTo())
ping.SetAttribute("Count", ns.UintegerValue(PING_COUNT))
ping.SetAttribute("Interval", ns.TimeValue(ns.Seconds(1)))
ping.SetAttribute("VerboseMode", ns.StringValue("Quiet"))
app = ping.Install(nodes.Get(0))
cpp.TrackPings(app.Get(0))
app.Start(ns.Seconds(PING_START))

# Log A's route to D whenever it changes. Print A's whole table just before
# the first ping (DSDV is proactive, so A already has a route to every node)
# and again at the end.
print("A's route to D (10.1.1.4), printed whenever it changes:")
print("     time   next hop        hops  seq no", flush=True)
a, b = nodes.Get(0), nodes.Get(1)
cpp.StartWatching(a, interfaces.GetAddress(3))
cpp.PrintRoutesOf(a, PING_START - 1)
cpp.MoveNodeAt(b, BREAK_TIME, ns.Vector(45, 1000, 0))

stop_time = PING_START + PING_COUNT + 1
cpp.PrintRoutesOf(a, stop_time - 1)
cpp.PrintLostPingsAt(stop_time - 0.5, PING_COUNT, PING_START)

ns.Simulator.Stop(ns.Seconds(stop_time))
ns.Simulator.Run()
ns.Simulator.Destroy()
