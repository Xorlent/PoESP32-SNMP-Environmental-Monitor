/*
 *    FILE: DHCPClient.h
 *  AUTHOR: PoESP32-SNMP-Environmental-Monitor
 * PURPOSE: Minimal raw-UDP DHCP client (RFC 2131/2132) used to obtain the
 *          device network configuration AND the custom provisioning options
 *          (authorized SNMP hosts + read community) bypassing the lwIP DHCP
 *          client.
 *
 *  It performs a full DISCOVER/OFFER/REQUEST/ACK exchange over UDP 67/68 and
 *  parses options 1 (subnet), 3 (router), 12 (hostname), and two configurable
 *  custom options (default 230 = authorized hosts, 231 = read community).
 *
 *  The Parameter Request List (option 55) includes the custom option numbers
 *  so a standards-compliant server will return them.
 */

#ifndef DHCPCLIENT_H
#define DHCPCLIENT_H

#include <Arduino.h>
#include <AsyncUDP.h>
#include <ETH.h>
#include <esp_random.h>

// DHCP option codes (RFC 2132)
enum {
  DHCP_OPT_PAD           = 0,
  DHCP_OPT_SUBNET_MASK   = 1,
  DHCP_OPT_ROUTER        = 3,
  DHCP_OPT_DNS           = 6,
  DHCP_OPT_HOSTNAME      = 12,
  DHCP_OPT_REQUESTED_IP  = 50,
  DHCP_OPT_LEASE_TIME    = 51,
  DHCP_OPT_MSG_TYPE      = 53,
  DHCP_OPT_SERVER_ID     = 54,
  DHCP_OPT_PARAM_REQ     = 55,
  DHCP_OPT_RENEWAL_TIME  = 58,
  DHCP_OPT_REBIND_TIME   = 59,
  DHCP_OPT_END           = 255
};

// Option 53 message type values
enum {
  DHCP_MSG_DISCOVER = 1,
  DHCP_MSG_OFFER    = 2,
  DHCP_MSG_REQUEST  = 3,
  DHCP_MSG_DECLINE  = 4,
  DHCP_MSG_ACK      = 5,
  DHCP_MSG_NAK      = 6,
  DHCP_MSG_RELEASE  = 7,
  DHCP_MSG_INFORM   = 8
};

#define DHCP_CLIENT_PORT  68
#define DHCP_SERVER_PORT  67
// RFC 2131 requires clients to accept DHCP messages up to 576 bytes. Options 230
// (authorized hosts) and 231 (community) add size, so a 300-byte buffer is too
// small (a real OFFER with several hosts + community is ~366 bytes).
#define DHCP_PACKET_SIZE  576

#define DHCP_MAX_AUTHORIZED_HOSTS 8
#define DHCP_MAX_COMMUNITY_LEN    31
#define DHCP_MAX_COMMUNITY_SECRET_LEN (2 * DHCP_MAX_COMMUNITY_LEN) // hex-encoded encrypted community
#define DHCP_MAX_HOSTNAME_LEN     63

// Everything extracted from a DHCP server response.
struct DHCPConfig {
  bool hasIP       = false;
  bool hasSubnet   = false;
  bool hasGateway  = false;
  bool hasHostname = false;
  bool hasCommunity = false;
  bool hasAuthorizedHosts = false;

  IPAddress ip;                    // yiaddr
  IPAddress subnet;                // option 1
  IPAddress gateway;               // option 3
  uint32_t leaseTime = 0;          // option 51 (seconds)

  char hostname[DHCP_MAX_HOSTNAME_LEN + 1]; // option 12
  uint8_t hostnameLen = 0;

  char community[DHCP_MAX_COMMUNITY_SECRET_LEN + 1]; // option 231 value (hex-encoded encrypted community)
  uint8_t communityLen = 0;

  IPAddress authorizedHosts[DHCP_MAX_AUTHORIZED_HOSTS]; // configured hosts option
  uint8_t authorizedHostCount = 0;

  void reset() {
    hasIP = hasSubnet = hasGateway = hasHostname = hasCommunity = hasAuthorizedHosts = false;
    ip = subnet = gateway = IPAddress(0,0,0,0);
    leaseTime = 0;
    hostnameLen = communityLen = 0;
    authorizedHostCount = 0;
  }
};

class DHCPClient {
public:
  DHCPClient();

  // Configure the option numbers carrying the authorized-host list and the
  // read-community string (default 230 / 231).
  void setCustomOptions(uint8_t authorizedHostsOption, uint8_t communityOption);

  // Bind UDP port 68 and prepare the transaction. Returns false on failure.
  bool begin();

  // Perform a full DISCOVER/OFFER/REQUEST/ACK exchange, blocking up to
  // timeoutMs. On success, cfg is populated from the ACK and true is returned.
  bool negotiate(DHCPConfig& cfg, uint32_t timeoutMs);

  // Release the current lease (best effort).
  void release(const IPAddress& serverId, const IPAddress& clientIp);

  // Human-readable MAC address for the DHCP-reservation workflow.
  static void printMAC(Stream& out);

  // Static helpers exposed for unit-style parsing.
  static bool parseOptions(const uint8_t* opts, size_t len,
                           uint8_t authOpt, uint8_t commOpt,
                           DHCPConfig& cfg, IPAddress& serverId,
                           uint8_t& msgType);

private:
  uint8_t _authOpt;
  uint8_t _commOpt;
  uint32_t _xid;
  uint8_t _mac[6];
  bool _started;

  AsyncUDP _udp;
  uint8_t _rxPacket[DHCP_PACKET_SIZE];
  volatile size_t  _rxLen;
  volatile bool    _rxReady;

  void _buildHeader(uint8_t* buf);
  void _setRequestedList(uint8_t* buf, size_t& off);
  void _onPacket(const uint8_t* data, size_t len);

  bool _sendDiscover(uint8_t* buf);
  bool _sendRequest(uint8_t* buf, const IPAddress& serverId, const IPAddress& requestedIp);
  bool _wait(uint8_t expectedType, uint32_t timeoutMs,
             DHCPConfig& cfg, IPAddress& serverId);
};

#endif // DHCPCLIENT_H
