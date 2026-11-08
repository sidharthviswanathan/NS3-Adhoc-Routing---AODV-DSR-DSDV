# Ten nodes in a Wi-Fi ad hoc network, routed by DSR (Dynamic Source Routing).
# DSR is a source-routing protocol, neither distance vector nor link state:
# the sender puts the complete path into every packet, and nodes in between
# just follow it. Routes are found on demand, by flooding a route request
# that records each node it passes. DSR's cost is the hop count.
#
#   A(0,0) --- B(45,0) --- C(90,0) --- D(135,0)     short route: A-B-C-D         (3 hops)
#     |                                   |
#   E(0,-45)                          J(135,-45)
#     |                                   |
#   F(0,-90) -- G(45,-90) -- H(90,-90) -- I(135,-90) long route: A-E-F-G-H-I-J-D (7 hops)
#
# Neighbors are 45 m apart and the radio range is 60 m, so diagonal pairs
# (63.6 m) cannot hear each other. A sends a UDP echo packet to D once a
# second (ns-3's DSR does not carry ICMP, so ping cannot be used). At 15 s,
# B moves far away, breaking the short route.
#
# The program records every node that transmits each echo packet, so it
# shows the exact path each packet took, and it logs every route request.
from ns import ns

BREAK_TIME = 15.0  # when node B moves away (s)
COUNT = 30  # number of echo packets, one per second
CACHE_TYPE = "LinkCache"  # DSR route cache: "LinkCache" or "PathCache"

# C++ helpers: follow every echo packet and route request, and print results
ns.cppyy.cppdef("""
    #include "ns3/iana-ieee802-numbers.h"
    #include "ns3/iana-internet-protocol-numbers.h"
    #include <iomanip>
    #include <map>
    #include <sstream>
    using namespace ns3;

    // Letter of the node a trace context belongs to ("/NodeList/3/..." is D)
    char NodeOfContext(const std::string& context)
    {
        auto start = context.find("/NodeList/") + 10;
        return char('A' + std::stoul(context.substr(start)));
    }

    // Letter of a 10.1.1.x address (10.1.1.1 is A)
    char NodeOfAddress(Ipv4Address address)
    {
        return char('A' + (address.Get() & 0xff) - 1);
    }

    // One echo packet's journey
    struct Echo
    {
        uint32_t number = 0;       // 0 for the first packet sent
        double sent = 0;           // time A sent it (s)
        std::string transmitters;  // each node that transmitted it, in order
        bool delivered = false;    // whether D received the request
        double replied = -1;       // time the reply reached A (s), or -1
    };

    // Echo packets by packet UID. DSR keeps a packet's UID as it forwards it,
    // and the echo server sends the same packet back.
    std::map<uint64_t, Echo> g_echoes;
    uint32_t g_sent = 0;

    void EchoSent(Ptr<const Packet> packet)
    {
        Echo echo;
        echo.number = g_sent++;
        echo.sent = Simulator::Now().GetSeconds();
        g_echoes[packet->GetUid()] = echo;
    }

    void EchoArrived(Ptr<const Packet> packet, const Address& from)
    {
        auto it = g_echoes.find(packet->GetUid());
        if (it != g_echoes.end())
        {
            it->second.delivered = true;
        }
    }

    void ReplyArrived(Ptr<const Packet> packet)
    {
        auto it = g_echoes.find(packet->GetUid());
        if (it != g_echoes.end())
        {
            it->second.replied = Simulator::Now().GetSeconds();
        }
    }

    // Called whenever any node's IP layer sends a packet
    void IpSent(std::string context, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
    {
        auto it = g_echoes.find(packet->GetUid());
        if (it != g_echoes.end())
        {
            it->second.transmitters += NodeOfContext(context);
        }
    }

    // One route discovery: a route request started by one node
    struct Discovery
    {
        double time;                // when the originator broadcast it (s)
        char origin;                // node looking for a route
        uint32_t ttl;               // 1 = neighbours only, 255 = whole network
        std::string rebroadcasters; // nodes that passed it on
    };

    std::vector<Discovery> g_discoveries;

    // Called whenever any node's radio starts sending a frame. DSR route
    // requests are the only IP broadcasts in this simulation (IP protocol 48).
    void WifiSent(std::string context, Ptr<const Packet> frame, double txPowerW)
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
        char sender = NodeOfContext(context);
        char origin = NodeOfAddress(ip.GetSource()); // a request keeps its originator's address
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

    void StartTracking(Ptr<Application> client, Ptr<Application> server)
    {
        client->TraceConnectWithoutContext("Tx", MakeCallback(&EchoSent));
        client->TraceConnectWithoutContext("Rx", MakeCallback(&ReplyArrived));
        server->TraceConnectWithoutContext("Rx", MakeCallback(&EchoArrived));
        Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/Tx", MakeCallback(&IpSent));
        Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/PhyTxBegin",
                        MakeCallback(&WifiSent));
    }

    void DoMove(Ptr<Node> node, Vector position)
    {
        node->GetObject<MobilityModel>()->SetPosition(position);
    }

    void MoveNodeAt(Ptr<Node> node, double seconds, Vector position)
    {
        Simulator::Schedule(Seconds(seconds), &DoMove, node, position);
    }

    // "ABC" -> "A-B-C"
    std::string Dashed(const std::string& letters)
    {
        std::string out;
        for (char c : letters)
        {
            out += out.empty() ? std::string(1, c) : std::string("-") + c;
        }
        return out;
    }

    // Hops in a path, not counting retries ("AABCD" is 3 hops)
    uint32_t Hops(const std::string& letters)
    {
        uint32_t hops = 0;
        for (size_t i = 1; i < letters.size(); ++i)
        {
            hops += letters[i] != letters[i - 1];
        }
        return hops;
    }

    std::string PathLabel(const std::string& path, bool arrived)
    {
        if (path.empty())
        {
            return "-";
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
    }

    // Request and reply paths of one echo packet, as printed
    std::pair<std::string, std::string> Paths(const Echo& echo)
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
        return {PathLabel(request, echo.delivered), PathLabel(reply, echo.replied >= 0)};
    }

    void PrintResults(double breakTime)
    {
        std::vector<Echo> echoes(g_echoes.size());
        for (const auto& [uid, echo] : g_echoes)
        {
            echoes[echo.number] = echo;
        }

        std::cout << "Echo packets from A to D, one per second. A path lists the nodes that\\n"
                  << "sent the packet, in order; a repeated letter is a retry.\\n\\n"
                  << "  packet  sent (s)  request path              reply path                "
                     "round trip\\n";
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
                std::cout << "  *** " << breakTime << " s: node B moves away ***\\n";
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
                      << times.str() << std::setw(26) << paths.first << std::setw(26)
                      << paths.second << rtt.str() << std::right << "\\n";
            first = last + 1;
        }

        std::cout << "\\nRoute discoveries (route request broadcasts):\\n";
        for (const auto& d : g_discoveries)
        {
            std::string what = d.ttl == 1 ? "asks its neighbours only (TTL 1)"
                                          : "floods the network (TTL " + std::to_string(d.ttl) + ")";
            std::string others = d.rebroadcasters.empty() ? "nobody" : d.rebroadcasters;
            others.erase(others.find_last_not_of(' ') + 1);
            std::cout << "  " << std::fixed << std::setprecision(3) << std::setw(7) << d.time
                      << std::defaultfloat << " s  " << d.origin << " " << std::left
                      << std::setw(34) << what << std::right << "rebroadcast by: " << others
                      << "\\n";
        }
        std::cout << "\\n"
                  << g_sent << " echo packets sent, " << replies << " replies received, "
                  << g_sent - replies << " lost\\n";
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

# Install the internet stack, then DSR. DSR is not an IP routing protocol in
# ns-3: it sits between IP and UDP/TCP and adds its own header. These are its
# defaults, set here so they are easy to find.
internet = ns.InternetStackHelper()
internet.Install(nodes)
dsr = ns.DsrHelper()
dsr.Set("CacheType", ns.StringValue(CACHE_TYPE))  # LinkCache runs Dijkstra on known links
dsr.Set("LinkAcknowledgment", ns.BooleanValue(True))  # next hop must confirm each packet
dsr.Set("LinkAckTimeout", ns.TimeValue(ns.MilliSeconds(100)))
dsr.Set("TryLinkAcks", ns.UintegerValue(1))  # retries before a link counts as broken
dsr_main = ns.DsrMainHelper()
dsr_main.Install(dsr, nodes)

address = ns.Ipv4AddressHelper()
address.SetBase(ns.Ipv4Address("10.1.1.0"), ns.Ipv4Mask("255.255.255.0"))
interfaces = address.Assign(devices)

# A sends a 64-byte UDP echo packet to D once a second
server = ns.UdpEchoServerHelper(9)
server_app = server.Install(nodes.Get(3))
client = ns.UdpEchoClientHelper(interfaces.GetAddress(3).ConvertTo(), 9)
client.SetAttribute("MaxPackets", ns.UintegerValue(COUNT))
client.SetAttribute("Interval", ns.TimeValue(ns.Seconds(1)))
client.SetAttribute("PacketSize", ns.UintegerValue(64))
client_app = client.Install(nodes.Get(0))
client_app.Start(ns.Seconds(1))

# Follow every echo packet and route request
cpp.StartTracking(client_app.Get(0), server_app.Get(0))
cpp.MoveNodeAt(nodes.Get(1), BREAK_TIME, ns.Vector(45, 1000, 0))

ns.Simulator.Stop(ns.Seconds(COUNT + 5))
ns.Simulator.Run()
cpp.PrintResults(BREAK_TIME)
ns.Simulator.Destroy()
