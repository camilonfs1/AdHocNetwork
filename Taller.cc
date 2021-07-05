//Librerias
#include "ns3/command-line.h"
#include "ns3/string.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/ssid.h"
#include "ns3/mobility-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/on-off-helper.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/qos-txop.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/olsr-helper.h"
#include "ns3/csma-helper.h"
#include "ns3/animation-interface.h"

using namespace ns3;

//Nombre del componente
NS_LOG_COMPONENT_DEFINE ("MixedWireless");

//
// This function will be used below as a trace sink, if the command-line
// argument or default value "useCourseChangeCallback" is set to true
//
static void
CourseChangeCallback (std::string path, Ptr<const MobilityModel> model){
  Vector position = model->GetPosition ();
  std::cout << "CourseChange " << path << " x=" << position.x << ", y=" << position.y << ", z=" << position.z << std::endl;
}


//Clase principal
int main (int argc, char *argv[]){
  //
  // Declaracion de variables
  //
  uint32_t backboneNodes = 10;
  uint32_t infraNodes = 2;
  uint32_t stopTime = 20;
  bool useCourseChangeCallback = false;

  //
  //Variables 
  //
  Config::SetDefault ("ns3::OnOffApplication::PacketSize", StringValue ("1472"));
  Config::SetDefault ("ns3::OnOffApplication::DataRate", StringValue ("100kb/s"));

  //
  // For convenience, we add the local variables to the command line argument
  // system so that they can be overridden with flags such as
  // "--backboneNodes=20"
  //
  CommandLine cmd (__FILE__);
  cmd.AddValue ("backboneNodes", "number of backbone nodes", backboneNodes);
  cmd.AddValue ("infraNodes", "number of leaf nodes", infraNodes);
  cmd.AddValue ("stopTime", "simulation stop time (seconds)", stopTime);
  cmd.AddValue ("useCourseChangeCallback", "whether to enable course change tracing", useCourseChangeCallback);

  //
  // The system global variables and the local values added to the argument
  // system can be overridden by command line arguments by using this call.
  //
  cmd.Parse (argc, argv);

  if (stopTime < 10){
      std::cout << "Use a simulation stop time >= 10 seconds" << std::endl;
      exit (1);
  }
  ///////////////////////////////////////////////////////////////////////////
  //                                                                       //
  // Construct the backbone                                                //
  //                                                                       //
  ///////////////////////////////////////////////////////////////////////////

  //
  // Create a container to manage the nodes of the adhoc (backbone) network.
  // Later we'll create the rest of the nodes we'll need.
  //
  NodeContainer backbone;
  backbone.Create (backboneNodes);
  //
  // Create the backbone wifi net devices and install them into the nodes in our container
  //
  WifiHelper wifi;
  WifiMacHelper mac;
  mac.SetType ("ns3::AdhocWifiMac");
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager","DataMode", StringValue ("OfdmRate54Mbps"));
  YansWifiPhyHelper wifiPhy;
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default ();
  wifiPhy.SetChannel (wifiChannel.Create ());
  NetDeviceContainer backboneDevices = wifi.Install (wifiPhy, mac, backbone);

  // We enable OLSR (which will be consulted at a higher priority than
  // the global routing) on the backbone ad hoc nodes
  NS_LOG_INFO ("Enabling OLSR routing on all backbone nodes");
  OlsrHelper olsr;
  //
  // Add the IPv4 protocol stack to the nodes in our container
  //
  InternetStackHelper internet;
  internet.SetRoutingHelper (olsr); // has effect on the next Install ()
  internet.Install (backbone);

  //
  // Assign IPv4 addresses to the device drivers (actually to the associated
  // IPv4 interfaces) we just created.
  //
  Ipv4AddressHelper ipAddrs;
  ipAddrs.SetBase ("192.168.0.0", "255.255.255.0");
  ipAddrs.Assign (backboneDevices);

  //
  // The ad-hoc network nodes need a mobility model so we aggregate one to
  // each of the nodes we just finished building.
  //
  MobilityHelper mobility;
  mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
                                 "MinX", DoubleValue (20.0),
                                 "MinY", DoubleValue (20.0),
                                 "DeltaX", DoubleValue (20.0),
                                 "DeltaY", DoubleValue (20.0),
                                 "GridWidth", UintegerValue (5),
                                 "LayoutType", StringValue ("RowFirst"));

  mobility.SetMobilityModel ("ns3::RandomDirection2dMobilityModel",
                             "Bounds", RectangleValue (Rectangle (-500, 500, -500, 500)),
                             "Speed", StringValue ("ns3::ConstantRandomVariable[Constant=2]"),
                             "Pause", StringValue ("ns3::ConstantRandomVariable[Constant=0.2]"));
                             
  mobility.Install (backbone);

  ///////////////////////////////////////////////////////////////////////////
  //                                                                       //
  // Construct the mobile networks                                         //
  //                                                                       //
  ///////////////////////////////////////////////////////////////////////////
  
  ipAddrs.SetBase ("10.0.0.0", "255.255.255.0");

  for (uint32_t i = 0; i < backboneNodes; ++i)
    {
      NS_LOG_INFO ("Configuring wireless network for backbone node " << i);
      NodeContainer stas;
      stas.Create (infraNodes);
      // Now, create the container with all nodes on this link
      NodeContainer infra (backbone.Get (i), stas);
      //
      // Create an infrastructure network
      //
      WifiHelper wifiInfra;
      WifiMacHelper macInfra;
      wifiPhy.SetChannel (wifiChannel.Create ());
      // Create unique ssids for these networks
      std::string ssidString ("wifi-infra");
      std::stringstream ss;
      ss << i;
      ssidString += ss.str ();
      Ssid ssid = Ssid (ssidString);
      wifiInfra.SetRemoteStationManager ("ns3::ArfWifiManager");
      // setup stas
      macInfra.SetType ("ns3::AdhocWifiMac","Ssid", SsidValue (ssid));
      NetDeviceContainer staDevices = wifiInfra.Install (wifiPhy, macInfra, stas);
      // setup ap.
      macInfra.SetType ("ns3::ApWifiMac","Ssid", SsidValue (ssid));
      NetDeviceContainer apDevices = wifiInfra.Install (wifiPhy, macInfra, backbone.Get (i));
      // Collect all of these new devices
      NetDeviceContainer infraDevices (apDevices, staDevices);

      // Add the IPv4 protocol stack to the nodes in our container
      internet.Install (stas);

      // Assign IPv4 addresses to the device drivers (actually to the associated IPv4 interfaces) we just created.
      ipAddrs.Assign (infraDevices);
      //
      // Assign a new network prefix for each mobile network, according to the network mask initialized above
      //
      ipAddrs.NewNetwork ();
      //
      // The new wireless nodes need a mobility model so we aggregate one to each of the nodes we just finished building.
      //
      Ptr<ListPositionAllocator> subnetAlloc = CreateObject<ListPositionAllocator> ();

      for (uint32_t j = 0; j < infra.GetN (); ++j){
          subnetAlloc->Add (Vector (0.0, j, 0.0));
      }
      mobility.PushReferenceMobilityModel (backbone.Get (i));
      mobility.SetPositionAllocator (subnetAlloc);
      mobility.SetMobilityModel ("ns3::RandomDirection2dMobilityModel",
                                 "Bounds", RectangleValue (Rectangle (-10, 10, -10, 10)),
                                 "Speed", StringValue ("ns3::ConstantRandomVariable[Constant=3]"),
                                 "Pause", StringValue ("ns3::ConstantRandomVariable[Constant=0.4]"));
      mobility.Install (stas);
    }

  ///////////////////////////////////////////////////////////////////////////
  //                                                                       //
  // Application configuration                                             //
  //                                                                       //
  ///////////////////////////////////////////////////////////////////////////


  NS_LOG_INFO ("Create Applications.");
  uint16_t port = 9;   // Discard port (RFC 863)


  Ptr<Node> appSource = NodeList::GetNode (backboneNodes);
  // Create a packet sink to receive these packets
  PacketSinkHelper sink ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));

  ///////////////////////////////////////////////////////////////////////////
  //                                                                       //
  // Tracing configuration                                                 //
  //                                                                       //
  ///////////////////////////////////////////////////////////////////////////

  NS_LOG_INFO ("Configure Tracing.");
  CsmaHelper csma;

  //
  // Let's set up some ns-2-like ascii traces, using another helper class
  //
  AsciiTraceHelper ascii;
  Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("mixed-wireless.tr");
  wifiPhy.EnableAsciiAll (stream);
  csma.EnableAsciiAll (stream);
  internet.EnableAsciiIpv4All (stream);

  // Csma captures in non-promiscuous mode
  csma.EnablePcapAll ("mixed-wireless", false);
  // pcap captures on the backbone wifi devices
  wifiPhy.EnablePcap ("mixed-wireless", backboneDevices, false);


  if (useCourseChangeCallback == true){
    Config::Connect ("/NodeList/*/$ns3::MobilityModel/CourseChange", MakeCallback (&CourseChangeCallback));
  }

  AnimationInterface anim ("mixed-wireless.xml");

  // Run simulation    

  NS_LOG_INFO ("Run Simulation.");
  Simulator::Stop (Seconds (stopTime));
  Simulator::Run ();
  Simulator::Destroy ();
}
