#include "DHCPClient.h"
#include <esp_netif.h>
#include <esp_err.h>

#define DHCP_MAGIC_COOKIE {0x63, 0x82, 0x53, 0x63}

// BOOTP/DHCP fixed header + magic cookie size
#define DHCP_FIXED_LEN 236
#define DHCP_COOKIE_LEN 4

DHCPClient::DHCPClient()
  : _authOpt(230), _commOpt(231), _xid(0), _started(false), _rxLen(0), _rxReady(false) {
  memset(_mac, 0, sizeof(_mac));
  memset(_rxPacket, 0, sizeof(_rxPacket));
}

void DHCPClient::setCustomOptions(uint8_t authorizedHostsOption, uint8_t communityOption) {
  _authOpt = authorizedHostsOption;
  _commOpt = communityOption;
}

bool DHCPClient::begin() {
  // Capture MAC; used in chaddr and for reservation display.
  ETH.macAddress(_mac);

  // Stop DHCP client so we can bind port 68/DHCP
  esp_netif_t* eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
  if (eth != NULL && esp_netif_dhcpc_stop(eth) != ESP_OK) {
    Serial.println("DHCP: warning - could not stop the built-in DHCP client");
  }

  // Random transaction ID.
  _xid = (uint32_t)esp_random();

  if (!_udp.listen(DHCP_CLIENT_PORT)) {
    return false;
  }

  _udp.onPacket([this](AsyncUDPPacket packet) {
    this->_onPacket(packet.data(), packet.length());
  });

  _started = true;
  return true;
}

// Fill the fixed BOOTP header shared by DISCOVER and REQUEST.
void DHCPClient::_buildHeader(uint8_t* buf) {
  memset(buf, 0, DHCP_FIXED_LEN + DHCP_COOKIE_LEN);

  buf[0] = 1;              // op: BOOTREQUEST
  buf[1] = 1;              // htype: Ethernet
  buf[2] = 6;              // hlen: 6
  buf[3] = 0;              // hops

  buf[4]  = (_xid >> 24) & 0xFF;
  buf[5]  = (_xid >> 16) & 0xFF;
  buf[6]  = (_xid >> 8)  & 0xFF;
  buf[7]  = _xid         & 0xFF;

  buf[10] = 0x80;          // flags: broadcast bit

  memcpy(buf + 28, _mac, 6); // chaddr

  // DHCP magic cookie
  uint8_t cookie[4] = DHCP_MAGIC_COOKIE;
  memcpy(buf + DHCP_FIXED_LEN, cookie, DHCP_COOKIE_LEN);
}

// Append option 55 (Parameter Request List) including the custom options.
void DHCPClient::_setRequestedList(uint8_t* buf, size_t& off) {
  static const uint8_t stdOpts[] = {
    DHCP_OPT_SUBNET_MASK, DHCP_OPT_ROUTER, DHCP_OPT_DNS, DHCP_OPT_HOSTNAME,
    DHCP_OPT_LEASE_TIME, DHCP_OPT_RENEWAL_TIME, DHCP_OPT_REBIND_TIME
  };
  const uint8_t customOpts[] = { _authOpt, _commOpt };

  buf[off++] = DHCP_OPT_PARAM_REQ;
  buf[off++] = sizeof(stdOpts) + sizeof(customOpts);
  for (unsigned i = 0; i < sizeof(stdOpts); i++) buf[off++] = stdOpts[i];
  for (unsigned i = 0; i < sizeof(customOpts); i++) buf[off++] = customOpts[i];
}

bool DHCPClient::_sendDiscover(uint8_t* buf) {
  _buildHeader(buf);
  size_t off = DHCP_FIXED_LEN + DHCP_COOKIE_LEN;

  // Message type: DISCOVER
  buf[off++] = DHCP_OPT_MSG_TYPE;
  buf[off++] = 1;
  buf[off++] = DHCP_MSG_DISCOVER;

  _setRequestedList(buf, off);

  buf[off++] = DHCP_OPT_END;

  return _udp.broadcastTo(buf, off, DHCP_SERVER_PORT, TCPIP_ADAPTER_IF_ETH) >= off;
}

bool DHCPClient::_sendRequest(uint8_t* buf, const IPAddress& serverId, const IPAddress& requestedIp) {
  _buildHeader(buf);
  size_t off = DHCP_FIXED_LEN + DHCP_COOKIE_LEN;

  // Message type: REQUEST
  buf[off++] = DHCP_OPT_MSG_TYPE;
  buf[off++] = 1;
  buf[off++] = DHCP_MSG_REQUEST;

  // Server identifier (option 54) — network byte order (octets 0..3)
  buf[off++] = DHCP_OPT_SERVER_ID;
  buf[off++] = 4;
  buf[off++] = serverId[0];
  buf[off++] = serverId[1];
  buf[off++] = serverId[2];
  buf[off++] = serverId[3];

  // Requested IP (option 50) — network byte order (octets 0..3)
  buf[off++] = DHCP_OPT_REQUESTED_IP;
  buf[off++] = 4;
  buf[off++] = requestedIp[0];
  buf[off++] = requestedIp[1];
  buf[off++] = requestedIp[2];
  buf[off++] = requestedIp[3];

  _setRequestedList(buf, off);

  buf[off++] = DHCP_OPT_END;

  return _udp.broadcastTo(buf, off, DHCP_SERVER_PORT, TCPIP_ADAPTER_IF_ETH) >= off;
}

void DHCPClient::_onPacket(const uint8_t* data, size_t len) {
  if (len < 2 || len > DHCP_PACKET_SIZE) {
    Serial.printf("DHCP: dropped packet, len=%d (max %d)\n", (int)len, (int)DHCP_PACKET_SIZE);
    return;
  }
  // Only handle BOOTREPLY
  if (data[0] != 2) return;
  // Match our transaction id
  uint32_t rxid = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                  ((uint32_t)data[6] << 8)  | (uint32_t)data[7];
  if (rxid != _xid) return;

  memcpy(_rxPacket, data, len);
  _rxLen = len;
  _rxReady = true;
}

bool DHCPClient::_wait(uint8_t expectedType, uint32_t timeoutMs,
                       DHCPConfig& cfg, IPAddress& serverId) {
  uint32_t start = millis();
  _rxReady = false;

  while ((uint32_t)(millis() - start) < timeoutMs) {
    if (_rxReady) {
      _rxReady = false;

      // yiaddr ("your IP") is a fixed BOOTP header field at offset 16..19,
      // before the option stream, so parseOptions cannot see it.
      if (_rxLen >= 20) {
        cfg.ip = IPAddress(_rxPacket[16], _rxPacket[17], _rxPacket[18], _rxPacket[19]);
        cfg.hasIP = (cfg.ip != IPAddress(0, 0, 0, 0));
      }

      uint8_t msgType = 0;
      IPAddress sid(0, 0, 0, 0);
      const uint8_t* opts = _rxPacket + DHCP_FIXED_LEN + DHCP_COOKIE_LEN;
      size_t optLen = (_rxLen >= DHCP_FIXED_LEN + DHCP_COOKIE_LEN)
                        ? (_rxLen - DHCP_FIXED_LEN - DHCP_COOKIE_LEN) : 0;

      parseOptions(opts, optLen, _authOpt, _commOpt, cfg, sid, msgType);

      if (msgType == expectedType) {
        serverId = sid;
        return true;
      }
      // We got a NAK, return false.
      if (msgType == DHCP_MSG_NAK) {
        return false;
      }
    }
    delay(25);
  }
  return false;
}

bool DHCPClient::negotiate(DHCPConfig& cfg, uint32_t timeoutMs) {
  if (!_started) return false;

  // Fresh transaction id for each negotiation.
  _xid = (uint32_t)esp_random();

  cfg.reset();
  IPAddress serverId(0, 0, 0, 0);

  uint8_t buf[DHCP_PACKET_SIZE];
  if (!_sendDiscover(buf)) return false;

  // Wait for OFFER
  if (!_wait(DHCP_MSG_OFFER, timeoutMs, cfg, serverId)) return false;
  if (!cfg.hasIP) return false;

  // OFFER is parsed into cfg already; request the offered address.
  IPAddress offered = cfg.ip;

  cfg.reset();

  if (!_sendRequest(buf, serverId, offered)) return false;

  if (!_wait(DHCP_MSG_ACK, timeoutMs, cfg, serverId)) return false;

  // ACK carries the requested address (yiaddr)
  cfg.ip = offered;
  if (!cfg.hasIP) cfg.hasIP = true;

  return true;
}

void DHCPClient::release(const IPAddress& serverId, const IPAddress& clientIp) {
  if (!_started) return;
  uint8_t buf[DHCP_PACKET_SIZE];
  _buildHeader(buf);
  size_t off = DHCP_FIXED_LEN + DHCP_COOKIE_LEN;

  buf[off++] = DHCP_OPT_MSG_TYPE;
  buf[off++] = 1;
  buf[off++] = DHCP_MSG_RELEASE;

  buf[off++] = DHCP_OPT_SERVER_ID;
  buf[off++] = 4;
  buf[off++] = serverId[0];
  buf[off++] = serverId[1];
  buf[off++] = serverId[2];
  buf[off++] = serverId[3];

  buf[off++] = DHCP_OPT_END;
  _udp.broadcastTo(buf, off, DHCP_SERVER_PORT, TCPIP_ADAPTER_IF_ETH);
}

void DHCPClient::printMAC(Stream& out) {
  uint8_t m[6];
  ETH.macAddress(m);
  out.print("MAC: ");
  for (int i = 0; i < 6; i++) {
    if (m[i] < 0x10) out.print('0');
    out.print(m[i], HEX);
    if (i < 5) out.print(':');
  }
  out.println();
}

// Parse a DHCP option stream after the magic cookie.
// Extracts message type, server id, and all fields we care about.
bool DHCPClient::parseOptions(const uint8_t* opts, size_t len,
                              uint8_t authOpt, uint8_t commOpt,
                              DHCPConfig& cfg, IPAddress& serverId,
                              uint8_t& msgType) {
  msgType = 0;
  serverId = IPAddress(0, 0, 0, 0);

  size_t i = 0;
  while (i + 1 < len) {
    uint8_t code = opts[i];
    if (code == DHCP_OPT_END) break;
    if (code == DHCP_OPT_PAD) { i++; continue; }

    uint8_t optLen = opts[i + 1];
    i += 2;

    // Bounds check: option data must fit inside the remaining buffer.
    if (optLen > (len - i)) break;

    const uint8_t* val = opts + i;

    switch (code) {
      case DHCP_OPT_MSG_TYPE:
        if (optLen >= 1) msgType = val[0];
        break;

      case DHCP_OPT_SERVER_ID:
        if (optLen == 4) serverId = IPAddress(val[0], val[1], val[2], val[3]);
        break;

      case DHCP_OPT_SUBNET_MASK:
        if (optLen == 4) {
          cfg.subnet = IPAddress(val[0], val[1], val[2], val[3]);
          cfg.hasSubnet = (cfg.subnet != IPAddress(0, 0, 0, 0));
        }
        break;

      case DHCP_OPT_ROUTER:
        if (optLen >= 4) {
          cfg.gateway = IPAddress(val[0], val[1], val[2], val[3]);
          cfg.hasGateway = (cfg.gateway != IPAddress(0, 0, 0, 0));
        }
        break;

      case DHCP_OPT_LEASE_TIME:
        if (optLen == 4) {
          cfg.leaseTime = ((uint32_t)val[0] << 24) | ((uint32_t)val[1] << 16) |
                          ((uint32_t)val[2] << 8) | (uint32_t)val[3];
        }
        break;

      case DHCP_OPT_HOSTNAME:
        if (optLen > 0) {
          // Trim any trailing NUL the server may append to the string.
          uint8_t n = 0;
          while (n < optLen && val[n] != '\0') n++;
          if (n > DHCP_MAX_HOSTNAME_LEN) n = DHCP_MAX_HOSTNAME_LEN;
          memcpy(cfg.hostname, val, n);
          cfg.hostname[n] = '\0';
          cfg.hostnameLen = n;
          cfg.hasHostname = (n > 0);
        }
        break;

      default:
        // Custom options (authorized hosts / community).
        if (code == authOpt) {
          if ((optLen % 4) == 0) {
            uint8_t count = optLen / 4;
            if (count > DHCP_MAX_AUTHORIZED_HOSTS) count = DHCP_MAX_AUTHORIZED_HOSTS;
            for (uint8_t k = 0; k < count; k++) {
              cfg.authorizedHosts[k] = IPAddress(val[k*4], val[k*4+1], val[k*4+2], val[k*4+3]);
            }
            cfg.authorizedHostCount = count;
            cfg.hasAuthorizedHosts = (count > 0);
          }
        } else if (code == commOpt) {
          if (optLen > 0) {
            // Trim any trailing NUL the server may append to the string.
            uint8_t n = 0;
            while (n < optLen && val[n] != '\0') n++;
            if (n > DHCP_MAX_COMMUNITY_SECRET_LEN) n = DHCP_MAX_COMMUNITY_SECRET_LEN;
            memcpy(cfg.community, val, n);
            cfg.community[n] = '\0';
            cfg.communityLen = n;
            cfg.hasCommunity = (n > 0);
          }
          // Empty community = "not present" (leave current).
        }
        break;
    }

    i += optLen;
  }

  return true;
}