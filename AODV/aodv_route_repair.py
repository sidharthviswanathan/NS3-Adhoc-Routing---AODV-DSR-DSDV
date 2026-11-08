# Ten nodes in a Wi-Fi ad hoc network, routed by AODV (on-demand, hop count).
# AODV has no per-link costs: the cost of a route is its number of hops.
#
#   A(0,0) --- B(45,0) --- C(90,0) --- D(135,0)     short route: A-B-C-D         (3 hops)
#     |                                   |
#   E(0,-45)                          J(135,-45)
#     |                                   |
#   F(0,-90) -- G(45,-90) -- H(90,-90) -- I(135,-90) long route: A-E-F-G-H-I-J-D (7 hops)
#
# Neighbors are 45 m apart and the radio range is 60 m, so diagonal pairs
# (63.6 m) cannot hear each other. A pings D once a second. At 15 s, B moves
# far away, breaking the short route, and AODV must discover the long route.
from ns import ns

BREAK_TIME = 15.0

# C++ helpers: print one node's routing table, and move a node at a given time
ns.cppyy.cppdef("""
    using namespace ns3;
    void PrintRoutesOf(Ptr<Node> node, double seconds)
    {
        Ptr<OutputStreamWrapper> out = Create<OutputStreamWrapper>(&std::cout);
        Ipv4RoutingHelper::PrintRoutingTableAt(Seconds(seconds), node, out);
    }
    void DoMove(Ptr<Node> node, Vector position)
    {
        node->GetObject<MobilityModel>()->SetPosition(position);
        std::cout << "\\n*** " << Simulator::Now().As(Time::S)
                  << ": node B moves away, breaking the short route ***\\n\\n";
    }
    void MoveNodeAt(Ptr<Node> node, double seconds, Vector position)
    {
        Simulator::Schedule(Seconds(seconds), &DoMove, node, position);
    }
""")

# Ten nodes, A to J (addresses 10.1.1.1 to 10.1.1.10)
LAYOUT = [
    (0, 0),     # A  10.1.1.1  source
    (45, 0),    # B  10.1.1.2
    (90, 0),    # C  10.1.1.3
    (135, 0),   # D  10.1.1.4  destination
    (0, -45),   # E  10.1.1.5
    (0, -90),   # F  10.1.1.6
    (45, -90),  # G  10.1.1.7
    (90, -90),  # H  10.1.1.8
    (135, -90), # I  10.1.1.9
    (135, -45), # J  10.1.1.10
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

# Use AODV for routing
aodv = ns.AodvHelper()
internet = ns.InternetStackHelper()
internet.SetRoutingHelper(aodv)
internet.Install(nodes)

address = ns.Ipv4AddressHelper()
address.SetBase(ns.Ipv4Address("10.1.1.0"), ns.Ipv4Mask("255.255.255.0"))
interfaces = address.Assign(devices)

# A pings D once a second. AODV only looks for a route when the first ping needs one.
ping = ns.PingHelper(interfaces.GetAddress(3).ConvertTo())
ping.SetAttribute("Count", ns.UintegerValue(30))
ping.SetAttribute("Interval", ns.TimeValue(ns.Seconds(1)))
app = ping.Install(nodes.Get(0))
app.Start(ns.Seconds(1))

# Print A's routes before the break and after AODV repairs the route
a, b = nodes.Get(0), nodes.Get(1)
ns.cppyy.gbl.PrintRoutesOf(a, BREAK_TIME - 1)
ns.cppyy.gbl.MoveNodeAt(b, BREAK_TIME, ns.Vector(45, 1000, 0))
ns.cppyy.gbl.PrintRoutesOf(a, BREAK_TIME + 10)

ns.Simulator.Stop(ns.Seconds(32))
ns.Simulator.Run()
ns.Simulator.Destroy()
