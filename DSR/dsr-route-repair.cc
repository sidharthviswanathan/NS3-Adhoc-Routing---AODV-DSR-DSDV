// Ten nodes in a Wi-Fi ad hoc network, routed by DSR (Dynamic Source Routing).
// DSR is a source-routing protocol, neither distance vector nor link state:
// the sender puts the complete path into every packet, and nodes in between
// just follow it. Routes are found on demand, by flooding a route request
// that records each node it passes. DSR's cost is the hop count.
//
//   A(0,0) --- B(45,0) --- C(90,0) --- D(135,0)     short route: A-B-C-D         (3 hops)
//     |                                   |
//   E(0,-45)                          J(135,-45)
//     |                                   |
//   F(0,-90) -- G(45,-90) -- H(90,-90) -- I(135,-90) long route: A-E-F-G-H-I-J-D (7 hops)
//
// Neighbors are 45 m apart and the radio range is 60 m, so diagonal pairs
// (63.6 m) cannot hear each other. A sends a UDP echo packet to D once a
// second (ns-3's DSR does not carry ICMP, so ping cannot be used). At 15 s,
// B moves far away, breaking the short route.
//
// The program records every node that transmits each echo packet, so it
// shows the exact path each packet took, and it logs every route request.
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/dsr-module.h"
#include "ns3/iana-ieee802-numbers.h"
#include "ns3/iana-internet-protocol-numbers.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

using namespace ns3;

/// Letter of the node a trace context belongs to ("/NodeList/3/..." is D)
char
NodeOf(const std::string& context)
{
    auto start = context.find("/NodeList/") + 10;
    return char('A' + std::stoul(context.substr(start)));
}

/// Letter of a 10.1.1.x address (10.1.1.1 is A)
char
NodeOf(Ipv4Address address)
{
    return char('A' + (address.Get() & 0xff) - 1);
}

/// One echo packet's journey
struct Echo
{
    uint32_t number = 0;       ///< 0 for the first packet sent
    double sent = 0;           ///< Time A sent it (s)
    std::string transmitters;  ///< Each node that transmitted it, in order
    bool delivered = false;    ///< Whether D received the request
    double replied = -1;       ///< Time the reply reached A (s), or -1
};

/// Echo packets by packet UID. DSR keeps a packet's UID as it forwards it,
/// and the echo server sends the same packet back.
std::map<uint64_t, Echo> g_echoes;
uint32_t g_sent = 0;

void
EchoSent(Ptr<const Packet> packet)
{
    Echo echo;
    echo.number = g_sent++;
    echo.sent = Simulator::Now().GetSeconds();
    g_echoes[packet->GetUid()] = echo;
}

void
EchoArrived(Ptr<const Packet> packet, const Address& from)
{
    auto it = g_echoes.find(packet->GetUid());
    if (it != g_echoes.end())
    {
        it->second.delivered = true;
    }
}

void
ReplyArrived(Ptr<const Packet> packet)
{
    auto it = g_echoes.find(packet->GetUid());
    if (it != g_echoes.end())
    {
        it->second.replied = Simulator::Now().GetSeconds();
    }
}

/// Called whenever any node's IP layer sends a packet
void
IpSent(std::string context, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
    auto it = g_echoes.find(packet->GetUid());
    if (it != g_echoes.end())
    {
        it->second.transmitters += NodeOf(context);
    }
}

/// One route discovery: a route request started by one node
struct Discovery
{
    double time;                ///< When the originator broadcast it (s)
    char origin;                ///< Node looking for a route
    uint32_t ttl;               ///< 1 = neighbours only, 255 = whole network
    std::string rebroadcasters; ///< Nodes that passed it on
};

std::vector<Discovery> g_discoveries;

/// Called whenever any node's radio starts sending a frame. DSR route
/// requests are the only IP broadcasts in this simulation (IP protocol 48).
void
WifiSent(std::string context, Ptr<const Packet> frame, double txPowerW)
{
    Ptr<Packet> packet = frame->Copy();
    WifiMacHeader mac;
    packet->RemoveHeader(mac);
    if (!mac.IsData() || !mac.GetAddr1().IsBroadcast())
    {
        return;
    }
    LlcSnapHeader llc;
    packet->RemoveHeader(llc);
    if (llc.GetType() != iana::ieee802numbers::IPV4) // ARP lookups are broadcasts too
    {
        return;
    }
    Ipv4Header ip;
    packet->RemoveHeader(ip);
    if (ip.GetProtocol() != iana::internetprotocolnumbers::DSR)
    {
        return;
    }

    char sender = NodeOf(context);
    char origin = NodeOf(ip.GetSource()); // a request keeps its originator's address
    if (sender == origin)
    {
        g_discoveries.push_back(Discovery{Simulator::Now().GetSeconds(), origin, ip.GetTtl(), ""});
        return;
    }
    for (auto it = g_discoveries.rbegin(); it != g_discoveries.rend(); ++it)
    {
        if (it->origin == origin)
        {
            it->rebroadcasters += sender;
            it->rebroadcasters += ' ';
            break;
        }
    }
}

void
MoveNode(Ptr<Node> node, Vector position)
{
    node->GetObject<MobilityModel>()->SetPosition(position);
}

/// "ABC" -> "A-B-C"
std::string
Dashed(const std::string& letters)
{
    std::string out;
    for (char c : letters)
    {
        out += out.empty() ? std::string(1, c) : std::string("-") + c;
    }
    return out;
}

/// Hops in a path, not counting retries ("AABCD" is 3 hops)
uint32_t
Hops(const std::string& letters)
{
    uint32_t hops = 0;
    for (size_t i = 1; i < letters.size(); ++i)
    {
        hops += letters[i] != letters[i - 1];
    }
    return hops;
}

/// Request and reply paths of one echo packet, as printed
std::pair<std::string, std::string>
Paths(const Echo& echo)
{
    auto atD = echo.transmitters.find('D');
    std::string request = echo.transmitters.substr(0, atD);
    std::string reply = atD == std::string::npos ? "" : echo.transmitters.substr(atD);
    if (echo.delivered)
    {
        request += 'D';
    }
    if (echo.replied >= 0)
    {
        reply += 'A';
    }
    auto label = [](const std::string& path, bool arrived) {
        if (path.empty())
        {
            return std::string("-");
        }
        std::ostringstream os;
        os << Dashed(path);
        if (arrived)
        {
            os << " (" << Hops(path) << " hops)";
        }
        else
        {
            os << " (lost)";
        }
        return os.str();
    };
    return {label(request, echo.delivered), label(reply, echo.replied >= 0)};
}

void
PrintResults(double breakTime)
{
    std::vector<Echo> echoes(g_echoes.size());
    for (const auto& [uid, echo] : g_echoes)
    {
        echoes[echo.number] = echo;
    }

    std::cout << "Echo packets from A to D, one per second. A path lists the nodes that\n"
              << "sent the packet, in order; a repeated letter is a retry.\n\n"
              << "  packet  sent (s)  request path              reply path                "
                 "round trip\n";
    bool breakShown = false;
    uint32_t replies = 0;
    for (size_t first = 0; first < echoes.size();)
    {
        // Group consecutive packets that took the same paths. The first packet
        // stands alone, because it waits for the first route discovery.
        auto paths = Paths(echoes[first]);
        size_t last = first;
        while (first > 0 && last + 1 < echoes.size() &&
               (echoes[last + 1].sent < breakTime) == (echoes[first].sent < breakTime) &&
               Paths(echoes[last + 1]) == paths)
        {
            ++last;
        }
        if (!breakShown && echoes[first].sent >= breakTime)
        {
            std::cout << "  *** " << breakTime << " s: node B moves away ***\n";
            breakShown = true;
        }
        double minRtt = 1e9;
        double maxRtt = -1;
        for (size_t i = first; i <= last; ++i)
        {
            if (echoes[i].replied >= 0)
            {
                double rtt = (echoes[i].replied - echoes[i].sent) * 1000;
                minRtt = std::min(minRtt, rtt);
                maxRtt = std::max(maxRtt, rtt);
                ++replies;
            }
        }
        std::ostringstream numbers;
        std::ostringstream times;
        numbers << first;
        times << echoes[first].sent;
        if (last > first)
        {
            numbers << "-" << last;
            times << "-" << echoes[last].sent;
        }
        std::ostringstream rtt;
        rtt << std::fixed << std::setprecision(1);
        if (maxRtt < 0)
        {
            rtt << "no reply";
        }
        else if (maxRtt - minRtt < 0.05)
        {
            rtt << minRtt << " ms";
        }
        else
        {
            rtt << minRtt << "-" << maxRtt << " ms";
        }
        std::cout << "  " << std::left << std::setw(8) << numbers.str() << std::setw(10)
                  << times.str() << std::setw(26) << paths.first << std::setw(26) << paths.second
                  << rtt.str() << std::right << "\n";
        first = last + 1;
    }

    std::cout << "\nRoute discoveries (route request broadcasts):\n";
    for (const auto& d : g_discoveries)
    {
        std::string what = d.ttl == 1 ? "asks its neighbours only (TTL 1)"
                                      : "floods the network (TTL " + std::to_string(d.ttl) + ")";
        std::string others = d.rebroadcasters.empty() ? "nobody" : d.rebroadcasters;
        others.erase(others.find_last_not_of(' ') + 1);
        std::cout << "  " << std::fixed << std::setprecision(3) << std::setw(7) << d.time
                  << std::defaultfloat << " s  " << d.origin << " " << std::left << std::setw(34)
                  << what << std::right << "rebroadcast by: " << others << "\n";
    }
    std::cout << "\n"
              << g_sent << " echo packets sent, " << replies << " replies received, "
              << g_sent - replies << " lost\n";
}

int
main(int argc, char* argv[])
{
    double breakTime = 15.0;
    uint32_t count = 30;
    std::string cacheType = "LinkCache";
    CommandLine cmd;
    cmd.AddValue("breakTime", "Time (s) at which node B moves away", breakTime);
    cmd.AddValue("count", "Number of echo packets, one per second", count);
    cmd.AddValue("cacheType", "DSR route cache: LinkCache or PathCache", cacheType);
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

    // 3. Install the internet stack, then DSR. DSR is not an IP routing
    // protocol in ns-3: it sits between IP and UDP/TCP and adds its own
    // header. These are its defaults, set here so they are easy to find.
    InternetStackHelper internet;
    internet.Install(nodes);
    DsrHelper dsr;
    dsr.Set("CacheType", StringValue(cacheType));       // LinkCache runs Dijkstra on known links
    dsr.Set("LinkAcknowledgment", BooleanValue(true));  // next hop must confirm each packet
    dsr.Set("LinkAckTimeout", TimeValue(MilliSeconds(100)));
    dsr.Set("TryLinkAcks", UintegerValue(1));           // retries before a link counts as broken
    DsrMainHelper dsrMain;
    dsrMain.Install(dsr, nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    // 4. A sends a 64-byte UDP echo packet to D (10.1.1.4) once a second
    UdpEchoServerHelper server(9);
    ApplicationContainer serverApp = server.Install(nodes.Get(3));
    UdpEchoClientHelper client(interfaces.GetAddress(3), 9);
    client.SetAttribute("MaxPackets", UintegerValue(count));
    client.SetAttribute("Interval", TimeValue(Seconds(1)));
    client.SetAttribute("PacketSize", UintegerValue(64));
    ApplicationContainer clientApp = client.Install(nodes.Get(0));
    clientApp.Start(Seconds(1));

    // 5. Follow every echo packet and route request
    clientApp.Get(0)->TraceConnectWithoutContext("Tx", MakeCallback(&EchoSent));
    clientApp.Get(0)->TraceConnectWithoutContext("Rx", MakeCallback(&ReplyArrived));
    serverApp.Get(0)->TraceConnectWithoutContext("Rx", MakeCallback(&EchoArrived));
    Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/Tx", MakeCallback(&IpSent));
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/PhyTxBegin",
                    MakeCallback(&WifiSent));

    Simulator::Schedule(Seconds(breakTime), &MoveNode, nodes.Get(1), Vector(45, 1000, 0));
    Simulator::Stop(Seconds(count + 5));
    Simulator::Run();
    PrintResults(breakTime);
    Simulator::Destroy();
    return 0;
}
