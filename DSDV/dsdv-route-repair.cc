// Ten nodes in a Wi-Fi ad hoc network, routed by DSDV (proactive distance
// vector). DSDV's cost (metric) is the hop count: every hop adds exactly 1.
// It picks routes by two rules, in this order:
//   1. Prefer the route with the newest (highest) destination sequence number.
//   2. If two routes have the same sequence number, prefer fewer hops.
//
//   A(0,0) --- B(45,0) --- C(90,0) --- D(135,0)     short route: A-B-C-D         (3 hops)
//     |                                   |
//   E(0,-45)                          J(135,-45)
//     |                                   |
//   F(0,-90) -- G(45,-90) -- H(90,-90) -- I(135,-90) long route: A-E-F-G-H-I-J-D (7 hops)
//
// Neighbors are 45 m apart and the radio range is 60 m, so diagonal pairs
// (63.6 m) cannot hear each other. A pings D once a second. Partway through,
// B moves far away, breaking the short route, and DSDV must switch to the
// long route. The program logs every change to A's route to D.
#include "ns3/core-module.h"
#include "ns3/dsdv-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

using namespace ns3;

/// One node's route to one destination, as read from its DSDV table
struct Route
{
    bool found = false;
    std::string nextHop;
    uint32_t hops = 0;
    uint32_t seqNo = 0;
};

/// Name a 10.1.1.x address by its node letter, such as "10.1.1.2 (B)"
std::string
Label(const std::string& address)
{
    auto lastOctet = std::stoul(address.substr(address.rfind('.') + 1));
    return address + " (" + char('A' + lastOctet - 1) + ")";
}

/// Read `node`'s route to `dst`. DSDV keeps its table private, so this
/// parses the table printout (Destination Gateway Interface HopCount SeqNum ...).
Route
LookupRoute(Ptr<Node> node, Ipv4Address dst)
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
        std::string destination;
        std::string iface;
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

/// Check `node`'s route to `dst` every 100 ms and print it whenever its next
/// hop, hop count or sequence number changes
void
WatchRoute(Ptr<Node> node, Ipv4Address dst, Route previous)
{
    Route now = LookupRoute(node, dst);
    if (now.found != previous.found || now.nextHop != previous.nextHop ||
        now.hops != previous.hops || now.seqNo != previous.seqNo)
    {
        std::cout << std::setw(9) << Simulator::Now().As(Time::S) << "   ";
        if (now.found)
        {
            std::cout << std::left << std::setw(16) << Label(now.nextHop) << std::setw(6)
                      << now.hops << now.seqNo << std::right << "\n";
        }
        else
        {
            std::cout << "(no route)\n";
        }
    }
    Simulator::Schedule(MilliSeconds(100), &WatchRoute, node, dst, now);
}

void
MoveNode(Ptr<Node> node, Vector position)
{
    node->GetObject<MobilityModel>()->SetPosition(position);
    std::cout << "*** " << Simulator::Now().As(Time::S)
              << ": node B moves away, breaking the short route ***\n";
}

std::set<uint16_t> g_replies; ///< Sequence numbers of the pings that got a reply

void
PingReply(uint16_t seq, Time rtt)
{
    g_replies.insert(seq);
}

/// Print which pings got no reply, as ranges of sequence numbers
void
PrintLostPings(uint32_t count, double pingStart)
{
    std::cout << "\nPings lost (icmp_seq, sent at time pingStart + seq):";
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
        std::cout << "\n  " << seq << (last > seq ? "-" + std::to_string(last) : "") << "  (sent "
                  << pingStart + seq << "-" << pingStart + last << " s)";
        lost += last - seq + 1;
        seq = last;
    }
    std::cout << (lost ? "" : " none") << "\n";
}

int
main(int argc, char* argv[])
{
    double breakTime = 60.0;
    double pingStart = 30.0;
    uint32_t pingCount = 90;
    CommandLine cmd;
    cmd.AddValue("breakTime", "Time (s) at which node B moves away", breakTime);
    cmd.AddValue("pingStart", "Time (s) of the first ping", pingStart);
    cmd.AddValue("pingCount", "Number of pings, one per second", pingCount);
    cmd.Parse(argc, argv);

    // 1. Create ten nodes, A to J (addresses 10.1.1.1 to 10.1.1.10)
    const std::vector<Vector> layout = {
        {0, 0, 0},     // A  10.1.1.1  source
        {45, 0, 0},    // B  10.1.1.2
        {90, 0, 0},    // C  10.1.1.3
        {135, 0, 0},   // D  10.1.1.4  destination
        {0, -45, 0},   // E  10.1.1.5
        {0, -90, 0},   // F  10.1.1.6
        {45, -90, 0},  // G  10.1.1.7
        {90, -90, 0},  // H  10.1.1.8
        {135, -90, 0}, // I  10.1.1.9
        {135, -45, 0}, // J  10.1.1.10
    };
    NodeContainer nodes;
    nodes.Create(layout.size());

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positions = CreateObject<ListPositionAllocator>();
    for (const auto& position : layout)
    {
        positions->Add(position);
    }
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    // 2. Wi-Fi radios in ad hoc mode, with a 60 m range
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("DsssRate1Mbps"),
                                 "ControlMode", StringValue("DsssRate1Mbps"));
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    channel.AddPropagationLoss("ns3::RangePropagationLossModel",
                               "MaxRange", DoubleValue(60));
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    // 3. Use DSDV as the routing protocol. These are its defaults, set here so
    // they are easy to find and change.
    DsdvHelper dsdv;
    dsdv.Set("PeriodicUpdateInterval", TimeValue(Seconds(15))); // full table broadcast
    dsdv.Set("SettlingTime", TimeValue(Seconds(5)));  // wait before advertising a new metric
    dsdv.Set("Holdtimes", UintegerValue(3));           // purge after 3 x 15 s of silence
    InternetStackHelper internet;
    internet.SetRoutingHelper(dsdv);
    internet.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    address.Assign(devices);

    // 4. A pings D (10.1.1.4) once a second
    PingHelper ping(Ipv4Address("10.1.1.4"));
    ping.SetAttribute("Count", UintegerValue(pingCount));
    ping.SetAttribute("Interval", TimeValue(Seconds(1)));
    ping.SetAttribute("VerboseMode", EnumValue(Ping::VerboseMode::QUIET));
    ApplicationContainer app = ping.Install(nodes.Get(0));
    app.Get(0)->TraceConnectWithoutContext("Rtt", MakeCallback(&PingReply));
    app.Start(Seconds(pingStart));

    // 5. Log A's route to D whenever it changes. Print A's whole table just
    // before the first ping (DSDV is proactive, so A already has a route to
    // every node) and again at the end.
    std::cout << "A's route to D (10.1.1.4), printed whenever it changes:\n"
              << "     time   next hop        hops  seq no\n";
    Simulator::Schedule(Seconds(0), &WatchRoute, nodes.Get(0), Ipv4Address("10.1.1.4"), Route{});

    Ptr<OutputStreamWrapper> out = Create<OutputStreamWrapper>(&std::cout);
    Ipv4RoutingHelper::PrintRoutingTableAt(Seconds(pingStart - 1), nodes.Get(0), out);
    Simulator::Schedule(Seconds(breakTime), &MoveNode, nodes.Get(1), Vector(45, 1000, 0));

    double stopTime = pingStart + pingCount + 1;
    Ipv4RoutingHelper::PrintRoutingTableAt(Seconds(stopTime - 1), nodes.Get(0), out);
    Simulator::Schedule(Seconds(stopTime - 0.5), &PrintLostPings, pingCount, pingStart);

    Simulator::Stop(Seconds(stopTime));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
