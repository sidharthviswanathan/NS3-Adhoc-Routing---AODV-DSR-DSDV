// Ten nodes in a Wi-Fi ad hoc network, routed by AODV (on-demand, hop count).
// AODV has no per-link costs: the cost of a route is its number of hops.
//
//   A(0,0) --- B(45,0) --- C(90,0) --- D(135,0)     short route: A-B-C-D         (3 hops)
//     |                                   |
//   E(0,-45)                          J(135,-45)
//     |                                   |
//   F(0,-90) -- G(45,-90) -- H(90,-90) -- I(135,-90) long route: A-E-F-G-H-I-J-D (7 hops)
//
// Neighbors are 45 m apart and the radio range is 60 m, so diagonal pairs
// (63.6 m) cannot hear each other. A pings D once a second. At 15 s, B moves
// far away, breaking the short route, and AODV must discover the long route.
#include "ns3/aodv-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <iostream>

using namespace ns3;

void
MoveNode(Ptr<Node> node, Vector position)
{
    node->GetObject<MobilityModel>()->SetPosition(position);
    std::cout << "\n*** " << Simulator::Now().As(Time::S) << ": node B moves away, "
              << "breaking the short route ***\n\n";
}

int
main(int argc, char* argv[])
{
    double breakTime = 15.0;
    CommandLine cmd;
    cmd.AddValue("breakTime", "Time (s) at which node B moves away", breakTime);
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

    // 3. Use AODV as the routing protocol
    AodvHelper aodv;
    InternetStackHelper internet;
    internet.SetRoutingHelper(aodv);
    internet.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    address.Assign(devices);

    // 4. A pings D (10.1.1.4) once a second. AODV only looks for a route
    // when the first ping needs one.
    PingHelper ping(Ipv4Address("10.1.1.4"));
    ping.SetAttribute("Count", UintegerValue(30));
    ping.SetAttribute("Interval", TimeValue(Seconds(1)));
    ping.SetAttribute("VerboseMode", EnumValue(Ping::VerboseMode::VERBOSE));
    ApplicationContainer app = ping.Install(nodes.Get(0));
    app.Start(Seconds(1));

    // 5. Print A's routes before the break (via B, 3 hops) and after AODV
    // repairs the route (via E, 7 hops)
    Ptr<OutputStreamWrapper> out = Create<OutputStreamWrapper>(&std::cout);
    Ipv4RoutingHelper::PrintRoutingTableAt(Seconds(breakTime - 1), nodes.Get(0), out);
    Simulator::Schedule(Seconds(breakTime), &MoveNode, nodes.Get(1), Vector(45, 1000, 0));
    Ipv4RoutingHelper::PrintRoutingTableAt(Seconds(breakTime + 10), nodes.Get(0), out);

    Simulator::Stop(Seconds(32));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
