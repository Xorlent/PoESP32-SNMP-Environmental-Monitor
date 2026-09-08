/*
GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor

Libraries and supporting code incorporates other licenses, see https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/LICENSE-3RD-PARTY.md
*/
////////------------------------------------------- CONFIGURATION SETTINGS AREA --------------------------------------------////////

// DHCP provisioning mode
//   DHCP_NEVER        = no DHCP; use compiled defaults only
//   DHCP_IFAVAILABLE  = DHCP may override individual settings; a network change
//                       saves prior config -> applies -> reboots, then runs a
//                       configurable revert window (see revertWindowMinutes).
//   DHCP_ALWAYS       = require DHCP; block until all required options arrive
enum DHCPMode { DHCP_NEVER, DHCP_IFAVAILABLE, DHCP_ALWAYS };
const DHCPMode DHCPControl = DHCP_IFAVAILABLE;

// Seconds between DHCP re-queries. Set to 0 to respect the server lease only
const uint32_t DHCPQueryInterval = 120;

// Revert window (DHCP_IFAVAILABLE): minutes to wait for a valid SNMP request
// after a DHCP-driven network change before reverting to the prior network
// settings and rebooting.
const uint16_t revertWindowMinutes = 3;

// DHCP option numbers carrying our custom provisioning data
const uint8_t authorizedSNMPOption = 230; // authorized SNMP hosts (packed IPv4)
const uint8_t readCommunityOption  = 231; // SNMP read community string

// ---- Compiled default settings (fallback when DHCPControl == DHCP_IFAVAILABLE, static when DHCPControl == DHCP_NEVER) ----

const char DEFAULT_HOSTNAME[] = "PoESP32-Unit"; // Hostname

IPAddress defaultIP(192, 168, 1, 99);       // Device IP address
IPAddress defaultGateway(192, 168, 1, 1);   // Default gateway
IPAddress defaultSubnet(255, 255, 255, 0);  // Subnet mask

const char DEFAULT_COMMUNITY[] = "readonly"; // SNMP read community (cleartext; if not using DHCP option 231)

// Shared key that XOR-encrypts the read community for DHCP option 231. Keep it
// the SAME on every device so the derived value is fleet-wide. Replace this
// placeholder; 16 character minimum.
const char COMMUNITY_KEY[] = "CHANGE-ME-to-a-long-random-key";

// Authorized SNMP query hosts (IPs allowed to query this device).
const IPAddress DEFAULT_AUTHORIZED_HOSTS[] = { IPAddress(192,168,1,5) };

/* Valid OIDs (Must query one OID per request):
### Uptime
1.3.6.1.2.1.1.3.0
### sysName
1.3.6.1.2.1.1.5.0
### Hostname (same as sysName)
1.3.6.1.4.1.119.2.1.3.0
### Temperature (.1 degrees C)
1.3.6.1.4.1.119.5.1.2.1.5.1
### Temperature (degrees F)
1.3.6.1.4.1.119.5.1.2.1.5.2
### Humidity (%)
1.3.6.1.4.1.119.5.1.2.1.6.1
*/

////////--------------------------------------- DO NOT EDIT ANYTHING BELOW THIS LINE ---------------------------------------////////

// Auto-derived constants (do not edit)
const uint8_t DEFAULT_AUTHORIZED_HOSTS_QTY = sizeof(DEFAULT_AUTHORIZED_HOSTS)/sizeof(IPAddress);

#include <ETH.h>
#include <AsyncUDP.h>
#include <SHT4x.h>
#include <arduino-timer.h>
#include <Preferences.h>
#include "DHCPClient.h"

#define SHT4X_DEBUG               false   // true enables heated measurement / equilibrium debug serial output
#define EQUILIBRIUM_WINDOW_SIZE   8       // 8 samples = 2 seconds @ 250ms intervals
#define DEFAULT_EQUILIBRIUM_TIMEOUT 60000 // ms
#define DEFAULT_DT_THRESHOLD      0.034   // °C/s max rate of change to declare equilibrium (adjusted from 0.030 used with the now end-of-life ENV IV module)

// Board-specific configuration
#define ETH_ADDR        1

#if defined(CONFIG_IDF_TARGET_ESP32P4)
// ESP32-P4 (Unit-PoE-P4) configuration
#define ETH_POWER_PIN   51
#define ETH_TYPE        ETH_PHY_TLK110
#define ETH_PHY_MDC     31
#define ETH_PHY_MDIO    52
#define ETH_CLK_MODE    EMAC_CLK_EXT_IN
#define I2C_SDA         53
#define I2C_SCL         54
#else
// ESP32 (default Unit-PoE) configuration
#define ETH_POWER_PIN   5
#define ETH_TYPE        ETH_PHY_IP101
#define ETH_PHY_MDC     23
#define ETH_PHY_MDIO    18
#define ETH_CLK_MODE    ETH_CLOCK_GPIO0_IN
#define I2C_SDA         16
#define I2C_SCL         17
#endif

////////---------------------------------------        Create runtime objects        ---------------------------------------////////

// ---- Runtime configuration limits ----
const uint8_t HOSTNAME_MAX_LEN      = 63;
const uint8_t COMMUNITY_MAX_LEN     = 31;
const uint8_t MAX_AUTHORIZED_HOSTS  = 8;

// Retry cadence for DHCP_ALWAYS (ms). The revert "prove it" window is derived
// from the revertWindowMinutes setting above and expressed here in ms.
const uint32_t DHCP_RETRY_INTERVAL = 10000;
const uint32_t REVERT_TIMEOUT      = (uint32_t)revertWindowMinutes * 60000UL;

// The revert window must be at least one minute
static_assert(revertWindowMinutes > 0, "revertWindowMinutes must be greater than 0");

// Custom DHCP option codes must fall in the usable range.
static_assert(authorizedSNMPOption >= 4 && authorizedSNMPOption <= 254,
              "authorizedSNMPOption must be between 4 and 254");
static_assert(readCommunityOption >= 4 && readCommunityOption <= 254,
              "readCommunityOption must be between 4 and 254");

// The community-encryption key must be long enough to be meaningful.
static_assert(sizeof(COMMUNITY_KEY) - 1 >= 16,
              "COMMUNITY_KEY must be at least 16 characters long");

// ---- Runtime configuration (populated from defaults, then NVS, then DHCP) ----

char hostname[HOSTNAME_MAX_LEN + 1];
uint8_t hostnameLen = 0;

char community[COMMUNITY_MAX_LEN + 1];
uint8_t communityLen = 0;

IPAddress authorizedHosts[MAX_AUTHORIZED_HOSTS];
uint8_t authorizedHostCount = 0;

IPAddress deviceIP, deviceGateway, deviceSubnet;   // active network config
IPAddress priorIP, priorGateway, priorSubnet;      // last-known-good network config

// Revert-monitoring state (DHCP_IFAVAILABLE mode)
volatile bool snmpServiced = false;   // a valid SNMP request was answered this boot
bool revertMonitoring = false;         // active 30-min revert window
uint32_t revertWindowStartMs = 0;

// Asynchronous UDP object
AsyncUDP udp;

// Byte strings for managing SNMP packet data:
static const uint8_t SNMP_ASN1_0[1] = {0x30};
static const uint8_t SNMP_VER1_2[3] = {0x02,0x01,0x00};
static const uint8_t SNMP_VER2_2[3] = {0x02,0x01,0x01};
static const uint8_t SNMP_READCOMMUNITY_5[1] = {0x04};
static const uint8_t SNMP_GETREQUEST_7_LEN[1] = {0xa0};

uint8_t SNMP_GETREQUEST_DATA0[7] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00}; // ASN.1, length (calc at runtime), ver int, ver len, ver value, community, community len
uint8_t SNMP_GETREQUEST_DATA1[2] = {0xa2,0x00}; // request_type (0xa2), length (calc at runtime)
uint8_t SNMP_GETREQUEST_DATA2a[8] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}; // Request ID int, RID length, RID ID[0], RID ID[1], RID ID[2], RID ID[3], RID ID[4], RID ID[5]
uint8_t SNMP_GETREQUEST_DATA2b[6] = {0x00,0x00,0x00,0x00,0x00,0x00}; // err int, err len, err value, index int, index len, index value
uint8_t SNMP_GETREQUEST_DATA3[6] = {0x00,0x00,0x00,0x00,0x00,0x00}; // Varbind list type, Varbind list len, Varbind type, Varbind len, Object ID, Object ID len

static const uint8_t SNMP_GETUPTIME[10] = {0x2b,0x06,0x01,0x02,0x01,0x01,0x03,0x00,0x05,0x00};
static const uint8_t SNMP_GETSYSNAME[10] = {0x2b,0x06,0x01,0x02,0x01,0x01,0x05,0x00,0x05,0x00}; // sysName -> returns hostname
static const uint8_t SNMP_GETHOST[12] = {0x2b,0x06,0x01,0x04,0x01,0x77,0x02,0x01,0x03,0x00,0x05,0x00};
static const uint8_t SNMP_GETTEMPC[14] = {0x2b,0x06,0x01,0x04,0x01,0x77,0x05,0x01,0x02,0x01,0x05,0x01,0x05,0x00};
static const uint8_t SNMP_GETTEMPF[14] = {0x2b,0x06,0x01,0x04,0x01,0x77,0x05,0x01,0x02,0x01,0x05,0x02,0x05,0x00};
static const uint8_t SNMP_GETHUMIDITY[14] = {0x2b,0x06,0x01,0x04,0x01,0x77,0x05,0x01,0x02,0x01,0x06,0x01,0x05,0x00};

// Blocking flag to avoid packet processing contention in case we're flooded with requests
volatile bool blocking = false;

// Sampling result flag to indicate a problem with the SHT40 sensor
volatile bool sampleError = false;

// I2C SHT4x sensor object (RobTillaart library)
SHT4x sht;

// Forward declarations for functions in SHT4x_advancedFunctions.ino
bool requestAuto(measType initialMeasurement = SHT4x_MEASUREMENT_MEDIUM,
                 uint16_t timeout = DEFAULT_EQUILIBRIUM_TIMEOUT,
                 float threshold = DEFAULT_DT_THRESHOLD);
bool autoReady();
float getAutoTemperature();
float getAutoHumidity();
extern bool needsHeating;

// Periodic temp & humidity sampling timer
auto timer = timer_create_default();

// Flag: a requestAuto() cycle is in flight
volatile bool measurementInProgress = false;

// Holds current sample values
volatile uint8_t pHumidity = 0;
volatile int16_t fTemp = 0;
volatile int16_t cTemp = 0;

////////---------------------------------------     End create runtime objects     ---------------------------------------////////

////////---------------------------------------       Function declarations        ---------------------------------------////////

// SHT4x sensor sampling function, used by the non-blocking timer and executed every 30 seconds
bool sample(void *);
// Celcius to farenheit conversion
int ctof(float x);
// Verifies authorized caller by checking request IP
bool authRequest(IPAddress callerIP);
// Parses incoming SNMP messages received from valid authorized IP addresses
int parseRequest(uint8_t *payload, size_t length);
// Constructs and sends the response message for valid OID getRequests
void sendGetResponse(int request, IPAddress caller, uint16_t port);

////////---------------------------------------     End function declarations      ---------------------------------------////////

////////---------------------------------------   NVS configuration persistence   ---------------------------------------////////

Preferences prefs;
const char* NVS_NS = "poesp32";

const char* NVS_HOSTNAME      = "hostname";
const char* NVS_COMMUNITY     = "community";
const char* NVS_AUTHHOSTS     = "authhosts";
const char* NVS_IP            = "ip";
const char* NVS_SUBNET        = "subnet";
const char* NVS_GATEWAY       = "gateway";
const char* NVS_PRIOR_IP      = "prior_ip";
const char* NVS_PRIOR_SUBNET  = "prior_subnet";
const char* NVS_PRIOR_GATEWAY = "prior_gateway";
const char* NVS_REVERT_PENDING = "revert_pending";

// CRC32 (IEEE 802.3, reflected polynomial 0xEDB88320)
uint32_t crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320u;
      else         crc >>= 1;
    }
  }
  return ~crc;
}

uint32_t ipToU32(const IPAddress& ip) { return (uint32_t)ip; }
IPAddress u32ToIp(uint32_t v) { return IPAddress(v); }

// CRC-validated persistence helpers. Each value is stored under `key` and its
// CRC32 under `key_crc`; reads fail closed (return false) on missing/corrupt data.
bool nvsPutU32(const char* key, uint32_t v) {
  char ckey[48];
  snprintf(ckey, sizeof(ckey), "%s_crc", key);
  prefs.putUInt(key, v);
  prefs.putUInt(ckey, crc32((const uint8_t*)&v, 4));
  return true;
}

bool nvsGetU32(const char* key, uint32_t& out) {
  char ckey[48];
  snprintf(ckey, sizeof(ckey), "%s_crc", key);
  if (!prefs.isKey(key) || !prefs.isKey(ckey)) return false;
  uint32_t v = prefs.getUInt(key);
  if (prefs.getUInt(ckey) != crc32((const uint8_t*)&v, 4)) return false;
  out = v;
  return true;
}

bool nvsPutString(const char* key, const char* s, size_t len) {
  char ckey[48];
  snprintf(ckey, sizeof(ckey), "%s_crc", key);
  prefs.putString(key, String(s, len));
  prefs.putUInt(ckey, crc32((const uint8_t*)s, len));
  return true;
}

bool nvsGetString(const char* key, char* out, size_t maxLen) {
  char ckey[48];
  snprintf(ckey, sizeof(ckey), "%s_crc", key);
  if (!prefs.isKey(key) || !prefs.isKey(ckey)) return false;
  String s = prefs.getString(key);
  size_t n = s.length();
  if (n > maxLen) n = maxLen;
  if (prefs.getUInt(ckey) != crc32((const uint8_t*)s.c_str(), s.length())) return false;
  memcpy(out, s.c_str(), n);
  out[n] = '\0';
  return true;
}

bool nvsPutBytes(const char* key, const uint8_t* data, size_t len) {
  char ckey[48];
  snprintf(ckey, sizeof(ckey), "%s_crc", key);
  prefs.putBytes(key, data, len);
  prefs.putUInt(ckey, crc32(data, len));
  return true;
}

bool nvsGetBytes(const char* key, uint8_t* out, size_t maxLen, size_t& outLen) {
  char ckey[48];
  snprintf(ckey, sizeof(ckey), "%s_crc", key);
  if (!prefs.isKey(key) || !prefs.isKey(ckey)) return false;
  size_t n = prefs.getBytesLength(key);
  uint8_t tmp[64];
  if (n > 64) n = 64;
  if (prefs.getBytes(key, tmp, n) != n) return false;
  if (prefs.getUInt(ckey) != crc32(tmp, n)) return false;
  if (n > maxLen) n = maxLen;
  memcpy(out, tmp, n);
  outLen = n;
  return true;
}

////////---------------------------------------   Config load / save   ---------------------------------------////////

// Load configuration: compiled defaults first, then overlay any valid NVS values.
void loadConfig() {
  prefs.begin(NVS_NS, false);

  hostnameLen = strlen(DEFAULT_HOSTNAME);
  memcpy(hostname, DEFAULT_HOSTNAME, hostnameLen);
  hostname[hostnameLen] = '\0';

  communityLen = strlen(DEFAULT_COMMUNITY);
  memcpy(community, DEFAULT_COMMUNITY, communityLen);
  community[communityLen] = '\0';

  deviceIP = defaultIP;
  deviceGateway = defaultGateway;
  deviceSubnet = defaultSubnet;

  authorizedHostCount = DEFAULT_AUTHORIZED_HOSTS_QTY;
  for (uint8_t i = 0; i < authorizedHostCount && i < MAX_AUTHORIZED_HOSTS; i++) {
    authorizedHosts[i] = DEFAULT_AUTHORIZED_HOSTS[i];
  }

  // Prior network slots default to compiled values (revert is non-NEVER only).
  priorIP = defaultIP;
  priorSubnet = defaultSubnet;
  priorGateway = defaultGateway;

  // In DHCP_NEVER mode the compiled defaults are authoritative - never overlay
  // persisted (possibly DHCP-written) configuration from NVS.
  if (DHCPControl == DHCP_NEVER) {
    prefs.end();
    return;
  }

  // Overlay valid NVS values.
  char tmp[64];
  if (nvsGetString(NVS_HOSTNAME, tmp, HOSTNAME_MAX_LEN)) {
    uint8_t n = strlen(tmp);
    memcpy(hostname, tmp, n);
    hostname[n] = '\0';
    hostnameLen = n;
  }
  if (nvsGetString(NVS_COMMUNITY, tmp, COMMUNITY_MAX_LEN)) {
    uint8_t n = strlen(tmp);
    memcpy(community, tmp, n);
    community[n] = '\0';
    communityLen = n;
  }

  uint32_t v;
  if (nvsGetU32(NVS_IP, v)) deviceIP = u32ToIp(v);
  if (nvsGetU32(NVS_SUBNET, v)) deviceSubnet = u32ToIp(v);
  if (nvsGetU32(NVS_GATEWAY, v)) deviceGateway = u32ToIp(v);

  uint8_t blob[64];
  size_t blen = 0;
  if (nvsGetBytes(NVS_AUTHHOSTS, blob, sizeof(blob), blen) && blen >= 1) {
    uint8_t count = blob[0];
    if (count > MAX_AUTHORIZED_HOSTS) count = MAX_AUTHORIZED_HOSTS;
    for (uint8_t i = 0; i < count; i++) {
      authorizedHosts[i] = IPAddress(blob[1 + i*4], blob[2 + i*4], blob[3 + i*4], blob[4 + i*4]);
    }
    authorizedHostCount = count;
  }

  // Prior (last-known-good) network config; fall back to compiled defaults.
  priorIP = nvsGetU32(NVS_PRIOR_IP, v) ? u32ToIp(v) : defaultIP;
  priorSubnet = nvsGetU32(NVS_PRIOR_SUBNET, v) ? u32ToIp(v) : defaultSubnet;
  priorGateway = nvsGetU32(NVS_PRIOR_GATEWAY, v) ? u32ToIp(v) : defaultGateway;

  prefs.end();
}

// Persist the current runtime configuration.
void saveConfig() {
  prefs.begin(NVS_NS, false);
  nvsPutString(NVS_HOSTNAME, hostname, hostnameLen);
  nvsPutString(NVS_COMMUNITY, community, communityLen);
  nvsPutU32(NVS_IP, ipToU32(deviceIP));
  nvsPutU32(NVS_SUBNET, ipToU32(deviceSubnet));
  nvsPutU32(NVS_GATEWAY, ipToU32(deviceGateway));

  uint8_t blob[64];
  blob[0] = authorizedHostCount;
  for (uint8_t i = 0; i < authorizedHostCount; i++) {
    blob[1 + i*4] = authorizedHosts[i][0];
    blob[2 + i*4] = authorizedHosts[i][1];
    blob[3 + i*4] = authorizedHosts[i][2];
    blob[4 + i*4] = authorizedHosts[i][3];
  }
  nvsPutBytes(NVS_AUTHHOSTS, blob, 1 + authorizedHostCount * 4);
  prefs.end();
}

// Persist the last-known-good network config (the "prior" slots).
void savePriorNetwork() {
  prefs.begin(NVS_NS, false);
  nvsPutU32(NVS_PRIOR_IP, ipToU32(priorIP));
  nvsPutU32(NVS_PRIOR_SUBNET, ipToU32(priorSubnet));
  nvsPutU32(NVS_PRIOR_GATEWAY, ipToU32(priorGateway));
  prefs.end();
}

// Revert-monitoring flag (persisted so it survives the network-change reboot).
bool getRevertPending() {
  prefs.begin(NVS_NS, true);
  uint32_t v = 0;
  bool ok = nvsGetU32(NVS_REVERT_PENDING, v) && (v == 1);
  prefs.end();
  return ok;
}

void setRevertPending(bool pending) {
  prefs.begin(NVS_NS, false);
  nvsPutU32(NVS_REVERT_PENDING, pending ? 1 : 0);
  prefs.end();
}

////////---------------------------------------   Community encryption (XOR)   ---------------------------------------////////

// XOR-obfuscate the read community so it does not travel in cleartext over DHCP.
// This is obfuscation (the key lives in firmware), not strong crypto - sufficient
// to keep the community out of casual packet captures.

static uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

// Encrypt: cleartext -> hex( XOR(key, cleartext) ).
void encryptCommunity(const char* plain, size_t len, char* hexOut, size_t hexOutSize) {
  static const char hex[] = "0123456789abcdef";
  size_t keyLen = sizeof(COMMUNITY_KEY) - 1;
  if (hexOutSize < 2 * len + 1) return;
  for (size_t i = 0; i < len; i++) {
    uint8_t c = (uint8_t)plain[i] ^ (uint8_t)COMMUNITY_KEY[i % keyLen];
    hexOut[2 * i]     = hex[c >> 4];
    hexOut[2 * i + 1] = hex[c & 0x0F];
  }
  hexOut[2 * len] = '\0';
}

// Decrypt: hex( XOR(key, cleartext) ) -> cleartext, capped at maxLen.
uint8_t decryptCommunity(const char* hexIn, size_t hexLen, char* out, uint8_t maxLen) {
  size_t keyLen = sizeof(COMMUNITY_KEY) - 1;
  size_t n = hexLen / 2;
  if (n > maxLen) n = maxLen;
  for (size_t i = 0; i < n; i++) {
    uint8_t c = (hexNibble(hexIn[2 * i]) << 4) | hexNibble(hexIn[2 * i + 1]);
    out[i] = (char)(c ^ (uint8_t)COMMUNITY_KEY[i % keyLen]);
  }
  out[n] = '\0';
  return (uint8_t)n;
}

// Serial "G" command state machine (non-blocking). Type "G" + Enter to enter
// generate-key mode, then type a community string to encrypt and print the DHCP
// option value. The mode auto-exits 30 seconds after the last value is supplied.
bool gModeActive = false;
uint32_t gModeLastInput = 0;

void handleSerialCommand() {
  // Auto-exit generate-key mode 30 seconds after the last value was supplied.
  if (gModeActive && ((uint32_t)(millis() - gModeLastInput) >= 30000)) {
    gModeActive = false;
    Serial.println("G mode idle timeout; exiting.");
    return;
  }

  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  if (gModeActive) {
    // Already in generate-key mode: treat input as a community value to encrypt.
    if (line.equalsIgnoreCase("G")) {
      Serial.println("Enter the community string (then press Enter):");
      gModeLastInput = millis();
      return;
    }
    if (line.length() > COMMUNITY_MAX_LEN) {
      line.remove(COMMUNITY_MAX_LEN);
      Serial.println("(community truncated to 31 characters)");
    }
    char secret[2 * COMMUNITY_MAX_LEN + 1];
    encryptCommunity(line.c_str(), line.length(), secret, sizeof(secret));
    Serial.print("DHCP option ");
    Serial.print(readCommunityOption);
    Serial.print(" value: ");
    Serial.println(secret);
    gModeLastInput = millis();
    Serial.println("(type another community, or wait 30s to exit)");
    return;
  }

  // Not in generate-key mode: only "G" is recognized.
  if (line.equalsIgnoreCase("G")) {
    gModeActive = true;
    gModeLastInput = millis();
    Serial.println("Enter the community string (then press Enter):");
  } else {
    Serial.println("Unknown command. Type 'G' + Enter to generate the DHCP option 231 value.");
  }
}

////////---------------------------------------   DHCP application   ---------------------------------------////////

// Apply a DHCP response to the runtime config (partial override: only options
// the server returned change the config). Returns true if a network-interface
// setting changed (which requires a save + reboot).
bool applyDhcp(const DHCPConfig& cfg) {
  bool networkChanged = false;

  if (cfg.hasIP && cfg.ip != deviceIP) {
    Serial.print("DHCP: IP changed: "); Serial.print(deviceIP); Serial.print(" -> "); Serial.println(cfg.ip);
    networkChanged = true; deviceIP = cfg.ip;
  }
  if (cfg.hasSubnet && cfg.subnet != deviceSubnet) {
    Serial.print("DHCP: subnet changed: "); Serial.print(deviceSubnet); Serial.print(" -> "); Serial.println(cfg.subnet);
    networkChanged = true; deviceSubnet = cfg.subnet;
  }
  if (cfg.hasGateway && cfg.gateway != deviceGateway) {
    Serial.print("DHCP: gateway changed: "); Serial.print(deviceGateway); Serial.print(" -> "); Serial.println(cfg.gateway);
    networkChanged = true; deviceGateway = cfg.gateway;
  }

  if (cfg.hasHostname) {
    if (cfg.hostnameLen != hostnameLen || memcmp(hostname, cfg.hostname, cfg.hostnameLen) != 0) {
      Serial.print("DHCP: hostname changed: "); Serial.print(hostname); Serial.print(" -> "); Serial.println(cfg.hostname);
    }
    memcpy(hostname, cfg.hostname, cfg.hostnameLen);
    hostname[cfg.hostnameLen] = '\0';
    hostnameLen = cfg.hostnameLen;
  }
  if (cfg.hasCommunity) {
    // Option 231 carries the encrypted (hex) community; decrypt it, cap at 31.
    char decrypted[COMMUNITY_MAX_LEN + 1];
    uint8_t decLen = decryptCommunity(cfg.community, cfg.communityLen, decrypted, COMMUNITY_MAX_LEN);
    if (decLen != communityLen || memcmp(community, decrypted, decLen) != 0) {
      // Print only the new ENCRYPTED value, never the cleartext community.
      Serial.print("DHCP: community changed -> "); Serial.println(cfg.community);
    }
    memcpy(community, decrypted, decLen);
    community[decLen] = '\0';
    communityLen = decLen;
  }
  if (cfg.hasAuthorizedHosts) {
    bool changed = (cfg.authorizedHostCount != authorizedHostCount);
    if (!changed) {
      for (uint8_t i = 0; i < authorizedHostCount; i++) {
        if (authorizedHosts[i] != cfg.authorizedHosts[i]) { changed = true; break; }
      }
    }
    if (changed) {
      Serial.print("DHCP: authorized hosts changed -> ");
      for (uint8_t i = 0; i < cfg.authorizedHostCount; i++) {
        if (i) Serial.print(", ");
        Serial.print(cfg.authorizedHosts[i]);
      }
      Serial.println();
    }
    authorizedHostCount = cfg.authorizedHostCount;
    for (uint8_t i = 0; i < authorizedHostCount; i++) {
      authorizedHosts[i] = cfg.authorizedHosts[i];
    }
  }

  return networkChanged;
}

////////---------------------------------------   Config dump (verification)   ---------------------------------------////////

// Print the active device configuration so settings can be verified at boot.
// NOTE: the read community and authorized-host list are printed ONLY for bring-up
// verification and should be removed from production output once confirmed.
void printConfig() {
  Serial.println();
  Serial.println("=== Device Configuration ===");
  Serial.print("Hostname: "); Serial.println(hostname);
  Serial.print("IP:       "); Serial.println(deviceIP);
  Serial.print("Subnet:   "); Serial.println(deviceSubnet);
  Serial.print("Gateway:  "); Serial.println(deviceGateway);

  uint8_t mac[6];
  ETH.macAddress(mac);
  Serial.print("MAC:      ");
  for (int i = 0; i < 6; i++) {
    if (i) Serial.print(':');
    if (mac[i] < 0x10) Serial.print('0');
    Serial.print(mac[i], HEX);
  }
  Serial.println();

  // Show the community in its encrypted (hex) form, never as cleartext.
  char obfCommunity[2 * COMMUNITY_MAX_LEN + 1];
  encryptCommunity(community, communityLen, obfCommunity, sizeof(obfCommunity));
  Serial.print("Community (encrypted): "); Serial.println(obfCommunity);
  Serial.print("Authorized hosts ("); Serial.print(authorizedHostCount); Serial.println("):");
  for (uint8_t i = 0; i < authorizedHostCount; i++) {
    Serial.print("  "); Serial.println(authorizedHosts[i]); // TEMP: remove after verification
  }
  Serial.println("===========================");
}

////////---------------------------------------   DHCP orchestration   ---------------------------------------////////

// Global DHCP client (binds UDP 68 once, reused for initial acquire + re-query).
DHCPClient dhcp;

// Last lease duration reported by the server (seconds); used as T1 when
// DHCPQueryInterval == 0.
uint32_t dhcpLeaseTime = 0;
uint32_t dhcpLastQueryMs = 0; // millis() of the last DHCP re-query

bool dhcpHasAllRequired(const DHCPConfig& cfg) {
  return cfg.hasIP && cfg.hasSubnet && cfg.hasGateway &&
         cfg.hasHostname && cfg.hasCommunity && cfg.hasAuthorizedHosts;
}

void dhcpPrintMissing(const DHCPConfig& cfg) {
  Serial.println("DHCP: response missing required option(s):");
  if (!cfg.hasIP)              Serial.println("  - IP address (yiaddr)");
  if (!cfg.hasSubnet)          Serial.println("  - subnet mask (option 1)");
  if (!cfg.hasGateway)         Serial.println("  - gateway (option 3)");
  if (!cfg.hasHostname)        Serial.println("  - hostname (option 12)");
  if (!cfg.hasCommunity)       Serial.println("  - read community (option readCommunityOption)");
  if (!cfg.hasAuthorizedHosts) Serial.println("  - authorized hosts (option authorizedSNMPOption)");
}

// Perform a DHCP negotiation and apply it. If a network-interface change is
// applied, the prior network config is saved and the device reboots (so this
// function does not return in that case). Returns true if a non-rebooting
// config was applied.
bool dhcpNegotiateAndApply(bool requireAll) {
  DHCPConfig cfg;
  if (!dhcp.negotiate(cfg, 5000)) {
    Serial.println("DHCP: no response");
    return false;
  }

  dhcpLeaseTime = cfg.leaseTime;

  if (requireAll && !dhcpHasAllRequired(cfg)) {
    dhcpPrintMissing(cfg);
    return false;
  }

  IPAddress oldIP = deviceIP, oldSubnet = deviceSubnet, oldGateway = deviceGateway;
  bool networkChanged = applyDhcp(cfg);

  if (networkChanged) {
    // Only capture the current config as "prior" if it is known-good. If we are
    // already on an unproven config (revert_pending still set), keep the existing
    // prior (the last confirmed-good) rather than overwriting it with a value
    // that was never validated.
    if (!getRevertPending()) {
      priorIP = oldIP; priorSubnet = oldSubnet; priorGateway = oldGateway;
      savePriorNetwork();
    }
    saveConfig();
    setRevertPending(true);
    Serial.println("DHCP: network config changed; rebooting");
    ESP.restart();
    return false; // unreachable
  }

  saveConfig(); // persist non-network changes
  return true;
}

bool sample(void *)


{
    if (measurementInProgress) return true; // Previous cycle still in progress, skip this tick

    if (!requestAuto())
    {
      Serial.println("Error starting auto measurement");
      sampleError = true;
      return true;
    }
    measurementInProgress = true;
    return true; // Leave timer enabled.
}

// Celcius to Farenheit conversion function
int ctof(float x)
{
  return (int)roundf(1.8f * x + 32.0f);
}

// Verify caller is authorized
bool authRequest(IPAddress callerIP)
{
  for (uint8_t i = 0; i < authorizedHostCount; i++)
  {
    if (callerIP == authorizedHosts[i])
    {
      return true;
    }
  }
  return false;
}

// Parse and validate GetRequest
// Returns -1 for invalid/unsupported, 0 for uptime, 1 for hostname, 2 for .1 degrees C, 3 for degrees F, 4 for humidity
int parseRequest(uint8_t *payload, size_t length)
{
  if (!payload) return -1; // Null request
  if(blocking == false)
  {
    blocking = true;
    if(memcmp(SNMP_ASN1_0,payload,sizeof(SNMP_ASN1_0)) == 0)
    {
      Serial.print("ASN1: Valid");
      Serial.println();
      if(memcmp(SNMP_VER1_2,payload+2,sizeof(SNMP_VER1_2)) == 0 || memcmp(SNMP_VER2_2,payload+2,sizeof(SNMP_VER2_2)) == 0)
      {
        Serial.print("SNMP Version 1 or 2: Valid");
        Serial.println();
        if(memcmp(SNMP_READCOMMUNITY_5,payload+5,sizeof(SNMP_READCOMMUNITY_5)) == 0)
        {
          Serial.print("Read Community Supplied");
          Serial.println();
          if(payload[6] == communityLen)
          {
            Serial.print("Read Community Length Matched");
            Serial.println();
            if(memcmp(community,payload+7,communityLen) == 0)
            {
              Serial.print("Read Community Value Matched");
              Serial.println();
              if(memcmp(SNMP_GETREQUEST_7_LEN,payload+7+communityLen,sizeof(SNMP_GETREQUEST_7_LEN)) == 0)
              {
                Serial.print("Processing GetRequest...");
                Serial.println();
                
                // Copy portions of the caller's request info into buffers for us to then send back in the response.
                memcpy(SNMP_GETREQUEST_DATA0,payload,7);
                uint8_t RIDLength = payload[10+communityLen]; // Retrieve the Request ID value length
                if (RIDLength > 6) return -1; // Prevent overflow of SNMP_GETREQUEST_DATA2a[8] (RIDLength+2 must be <= 8)
                if (9+communityLen+RIDLength+14 > length) return -1; // Prevent out-of-bounds memory read
                memcpy(SNMP_GETREQUEST_DATA2a,payload+9+communityLen,RIDLength+2); // Get the Request ID info
                memcpy(SNMP_GETREQUEST_DATA2b,payload+9+communityLen+RIDLength+2,6); // Get the Error info
                memcpy(SNMP_GETREQUEST_DATA3,payload+9+communityLen+RIDLength+2+6,6); // Get Varbind and Object info

                // We finished processing a valid uptime request packet.  Return 0 to the caller.
                if(memcmp(SNMP_GETUPTIME,payload+length-sizeof(SNMP_GETUPTIME),sizeof(SNMP_GETUPTIME)) == 0)
                {
                  return 0;
                }
                // We finished processing a valid sysName request.  Return 5 (hostname) to the caller.
                if(memcmp(SNMP_GETSYSNAME,payload+length-sizeof(SNMP_GETSYSNAME),sizeof(SNMP_GETSYSNAME)) == 0)
                {
                  return 5;
                }
                // We finished processing a valid hostname request packet.  Return 1 to the caller.
                if(memcmp(SNMP_GETHOST,payload+length-sizeof(SNMP_GETHOST),sizeof(SNMP_GETHOST)) == 0)
                {
                  return 1;
                }
                // We finished processing a valid .1 degrees C request packet.  Return 2 to the caller.
                if(memcmp(SNMP_GETTEMPC,payload+length-sizeof(SNMP_GETTEMPC),sizeof(SNMP_GETTEMPC)) == 0)
                {
                  return 2;
                }
                // We finished processing a valid degrees F request packet.  Return 3 to the caller.
                if(memcmp(SNMP_GETTEMPF,payload+length-sizeof(SNMP_GETTEMPF),sizeof(SNMP_GETTEMPF)) == 0)
                {
                  return 3;
                }
                // We finished processing a valid % relative humidity request packet.  Return 4 to the caller.
                if(memcmp(SNMP_GETHUMIDITY,payload+length-sizeof(SNMP_GETHUMIDITY),sizeof(SNMP_GETHUMIDITY)) == 0)
                {
                  return 4;
                }
                // We don't know what this is, print to serial console for debugging
                Serial.println();
                Serial.print("Unsupported or invalid payload: ");
                for(int idx = 0; idx < length; idx++)
                {
                  Serial.printf("%02x",payload[idx]);
                }
                Serial.println();
                Serial.println();
                return -1; // Unsupported/unknown request
              }
              return -1; // Unsupported/unknown request
            }
            return -1; // Unsupported/unknown request
          }
          return -1; // Unsupported/unknown request
        }
        return -1; // Unsupported/unknown request
      }
      return -1; // Unsupported/unknown request
    }
    return -1; // Unsupported/unknown request
  }
  return -1; // Currently blocked processing a request.  We'll ignore this one, release the blocking flag, and wait for a subsequent request to come in.
}

// Build and send response to valid getRequest
void sendGetResponse(int request, IPAddress caller, uint16_t port)
{
  // For sensor data requests, validate sample values before responding
  if(request > 1)
  {
    if(sampleError || pHumidity == 0 || pHumidity > 100 || fTemp < -40 || fTemp >= 141 || cTemp < -400 || cTemp >= 601)
    {
      SNMP_GETREQUEST_DATA2b[2] = 0x05; // Add error to getResponse message
    }
  }
  uint8_t RIDLength = SNMP_GETREQUEST_DATA2a[1];
  switch(request)
  {
    case 0 : // Return uptime
    {
      uint8_t dataType[2] = {0x43,0x04}; // Timetick, uint32
      int64_t microval = esp_timer_get_time()/10000; // Convert micros to 1/100ths of a second
      uint32_t val = static_cast<uint32_t>(microval & 0x0FFFFFFFF); // Uptime
      uint8_t value[4];
      value[0] = (val >> 24) & 0xFF;
      value[1] = (val >> 16) & 0xFF;
      value[2] = (val >> 8) & 0xFF;
      value[3] = val & 0xFF;
      uint8_t PDULen = 24 + RIDLength + 4; // getResponse PDU length plus 4 byte value
      SNMP_GETREQUEST_DATA1[1] = PDULen;
      SNMP_GETREQUEST_DATA3[1] = SNMP_GETREQUEST_DATA3[1] + 4; // Add four bytes to the varbind list to accommodate the 32 bit value being returned
      SNMP_GETREQUEST_DATA3[3] = SNMP_GETREQUEST_DATA3[3] + 4; // Add four bytes to the varbind item to accommodate the 32 bit value being returned
      uint8_t packetLen = (PDULen + 7 + communityLen); // +community length
      SNMP_GETREQUEST_DATA0[1] = packetLen;
      int responseBytes = (packetLen + 2); // +2 to add back the header bytes
      uint8_t responsePayload[responseBytes];
      memcpy(responsePayload,SNMP_GETREQUEST_DATA0,7);
      memcpy(responsePayload+7,community,communityLen);
      memcpy(responsePayload+7+communityLen,SNMP_GETREQUEST_DATA1,2);
      memcpy(responsePayload+9+communityLen,SNMP_GETREQUEST_DATA2a,RIDLength+2); // Get the Request ID info
      memcpy(responsePayload+9+communityLen+RIDLength+2,SNMP_GETREQUEST_DATA2b,6); // Get the Error info
      memcpy(responsePayload+17+communityLen+RIDLength,SNMP_GETREQUEST_DATA3,6);
      memcpy(responsePayload+23+communityLen+RIDLength,SNMP_GETUPTIME,8);
      memcpy(responsePayload+31+communityLen+RIDLength,dataType,2);
      memcpy(responsePayload+33+communityLen+RIDLength,value,4);
      udp.writeTo(responsePayload,responseBytes,caller,port);
      Serial.println();
      break;
    }
    case 1 : // Return hostname
    {
      uint8_t dataType[2] = {0x04,hostnameLen}; // String, length of hostname
      uint8_t PDULen = 26 + RIDLength + hostnameLen; // getResponse PDU length plus length of Request ID plus hostname
      SNMP_GETREQUEST_DATA1[1] = PDULen;
      SNMP_GETREQUEST_DATA3[1] = SNMP_GETREQUEST_DATA3[1] + hostnameLen; // Add bytes to the varbind list to accommodate the hostname value being returned
      SNMP_GETREQUEST_DATA3[3] = SNMP_GETREQUEST_DATA3[3] + hostnameLen; // Add bytes to the varbind item to accommodate the hostname value being returned
      uint8_t packetLen = (PDULen + 7 + communityLen); // +community length
      SNMP_GETREQUEST_DATA0[1] = packetLen;
      int responseBytes = (packetLen + 2); // +2 to add back the header bytes
      uint8_t responsePayload[responseBytes];
      memcpy(responsePayload,SNMP_GETREQUEST_DATA0,7);
      memcpy(responsePayload+7,community,communityLen);
      memcpy(responsePayload+7+communityLen,SNMP_GETREQUEST_DATA1,2);
      memcpy(responsePayload+9+communityLen,SNMP_GETREQUEST_DATA2a,RIDLength+2); // Get the Request ID info
      memcpy(responsePayload+9+communityLen+RIDLength+2,SNMP_GETREQUEST_DATA2b,6); // Get the Error info
      memcpy(responsePayload+17+communityLen+RIDLength,SNMP_GETREQUEST_DATA3,6);
      memcpy(responsePayload+23+communityLen+RIDLength,SNMP_GETHOST,10);
      memcpy(responsePayload+33+communityLen+RIDLength,dataType,2);
      memcpy(responsePayload+35+communityLen+RIDLength,hostname,hostnameLen);
      udp.writeTo(responsePayload,responseBytes,caller,port);
      Serial.println();
      break;
    }
    case 2 : // Return .1 degrees C
    {
      uint8_t dataType[2] = {0x02,0x02}; // Integer, 2 bytes
      uint8_t value[2];
      value[0] = (cTemp >> 8) & 0xFF;
      value[1] = cTemp & 0xFF;
      uint8_t PDULen = 28 + RIDLength + 2; // getResponse PDU length plus two bytes
      SNMP_GETREQUEST_DATA1[1] = PDULen;
      SNMP_GETREQUEST_DATA3[1] = SNMP_GETREQUEST_DATA3[1] + 2; // Add 2 bytes to the varbind list to accommodate the .1 degrees C value being returned
      SNMP_GETREQUEST_DATA3[3] = SNMP_GETREQUEST_DATA3[3] + 2; // Add 2 bytes to the varbind item to accommodate the .1 degrees C value being returned
      uint8_t packetLen = (PDULen + 7 + communityLen); // +community length
      SNMP_GETREQUEST_DATA0[1] = packetLen;
      int responseBytes = (packetLen + 2); // +2 to add back the header bytes
      uint8_t responsePayload[responseBytes];
      memcpy(responsePayload,SNMP_GETREQUEST_DATA0,7);
      memcpy(responsePayload+7,community,communityLen);
      memcpy(responsePayload+7+communityLen,SNMP_GETREQUEST_DATA1,2);
      memcpy(responsePayload+9+communityLen,SNMP_GETREQUEST_DATA2a,RIDLength+2); // Get the Request ID info
      memcpy(responsePayload+9+communityLen+RIDLength+2,SNMP_GETREQUEST_DATA2b,6); // Get the Error info
      memcpy(responsePayload+17+communityLen+RIDLength,SNMP_GETREQUEST_DATA3,6);
      memcpy(responsePayload+23+communityLen+RIDLength,SNMP_GETTEMPC,12);
      memcpy(responsePayload+35+communityLen+RIDLength,dataType,2);
      memcpy(responsePayload+37+communityLen+RIDLength,value,2);
      udp.writeTo(responsePayload,responseBytes,caller,port);
      Serial.println();
      break;
    }
    case 3 : // Return degrees F
    {
      uint8_t dataType[2] = {0x02,0x02}; // Integer, 2 bytes *
      uint8_t value[2];
      value[0] = (fTemp >> 8) & 0xFF;
      value[1] = fTemp & 0xFF;
      uint8_t PDULen = 28 + RIDLength + 2; // getResponse PDU length plus one byte
      SNMP_GETREQUEST_DATA1[1] = PDULen;
      SNMP_GETREQUEST_DATA3[1] = SNMP_GETREQUEST_DATA3[1] + 2; // Add 2 bytes to the varbind list to accommodate the degrees F value being returned
      SNMP_GETREQUEST_DATA3[3] = SNMP_GETREQUEST_DATA3[3] + 2; // Add 2 bytes to the varbind item to accommodate the degrees F value being returned
      uint8_t packetLen = (PDULen + 7 + communityLen); // +community length
      SNMP_GETREQUEST_DATA0[1] = packetLen;
      int responseBytes = (packetLen + 2); // +2 to add back the header bytes
      uint8_t responsePayload[responseBytes];
      memcpy(responsePayload,SNMP_GETREQUEST_DATA0,7);
      memcpy(responsePayload+7,community,communityLen);
      memcpy(responsePayload+7+communityLen,SNMP_GETREQUEST_DATA1,2);
      memcpy(responsePayload+9+communityLen,SNMP_GETREQUEST_DATA2a,RIDLength+2); // Get the Request ID info
      memcpy(responsePayload+9+communityLen+RIDLength+2,SNMP_GETREQUEST_DATA2b,6); // Get the Error info
      memcpy(responsePayload+17+communityLen+RIDLength,SNMP_GETREQUEST_DATA3,6);
      memcpy(responsePayload+23+communityLen+RIDLength,SNMP_GETTEMPF,12);
      memcpy(responsePayload+35+communityLen+RIDLength,dataType,2);
      memcpy(responsePayload+37+communityLen+RIDLength,value,2);
      udp.writeTo(responsePayload,responseBytes,caller,port);
      Serial.println();
      break;
    }
    case 4 : // Return humidity
    {
      uint8_t dataType[2] = {0x02,0x01}; // Integer, 1 byte
      uint8_t PDULen = 28 + RIDLength + 1; // getResponse PDU length plus one byte
      SNMP_GETREQUEST_DATA1[1] = PDULen;
      SNMP_GETREQUEST_DATA3[1] = SNMP_GETREQUEST_DATA3[1] + 1; // Add byte to the varbind list to accommodate the RH% value being returned
      SNMP_GETREQUEST_DATA3[3] = SNMP_GETREQUEST_DATA3[3] + 1; // Add byte to the varbind item to accommodate the RH% value being returned
      uint8_t packetLen = (PDULen + 7 + communityLen); // +community length
      SNMP_GETREQUEST_DATA0[1] = packetLen;
      int responseBytes = (packetLen + 2); // +2 to add back the header bytes
      uint8_t responsePayload[responseBytes];
      memcpy(responsePayload,SNMP_GETREQUEST_DATA0,7);
      memcpy(responsePayload+7,community,communityLen);
      memcpy(responsePayload+7+communityLen,SNMP_GETREQUEST_DATA1,2);
      memcpy(responsePayload+9+communityLen,SNMP_GETREQUEST_DATA2a,RIDLength+2); // Get the Request ID info
      memcpy(responsePayload+9+communityLen+RIDLength+2,SNMP_GETREQUEST_DATA2b,6); // Get the Error info
      memcpy(responsePayload+17+communityLen+RIDLength,SNMP_GETREQUEST_DATA3,6);
      memcpy(responsePayload+23+communityLen+RIDLength,SNMP_GETHUMIDITY,12);
      memcpy(responsePayload+35+communityLen+RIDLength,dataType,2);
      responsePayload[37+communityLen+RIDLength] = pHumidity;
      udp.writeTo(responsePayload,responseBytes,caller,port);
      Serial.println();
      break;
    }
    case 5 : // Return sysName (hostname)
    {
      uint8_t dataType[2] = {0x04,hostnameLen}; // String, length of hostname
      uint8_t PDULen = 24 + RIDLength + hostnameLen; // 8-byte OID
      SNMP_GETREQUEST_DATA1[1] = PDULen;
      SNMP_GETREQUEST_DATA3[1] = SNMP_GETREQUEST_DATA3[1] + hostnameLen;
      SNMP_GETREQUEST_DATA3[3] = SNMP_GETREQUEST_DATA3[3] + hostnameLen;
      uint8_t packetLen = (PDULen + 7 + communityLen);
      SNMP_GETREQUEST_DATA0[1] = packetLen;
      int responseBytes = (packetLen + 2);
      uint8_t responsePayload[responseBytes];
      memcpy(responsePayload,SNMP_GETREQUEST_DATA0,7);
      memcpy(responsePayload+7,community,communityLen);
      memcpy(responsePayload+7+communityLen,SNMP_GETREQUEST_DATA1,2);
      memcpy(responsePayload+9+communityLen,SNMP_GETREQUEST_DATA2a,RIDLength+2);
      memcpy(responsePayload+9+communityLen+RIDLength+2,SNMP_GETREQUEST_DATA2b,6);
      memcpy(responsePayload+17+communityLen+RIDLength,SNMP_GETREQUEST_DATA3,6);
      memcpy(responsePayload+23+communityLen+RIDLength,SNMP_GETSYSNAME,8);
      memcpy(responsePayload+31+communityLen+RIDLength,dataType,2);
      memcpy(responsePayload+33+communityLen+RIDLength,hostname,hostnameLen);
      udp.writeTo(responsePayload,responseBytes,caller,port);
      Serial.println();
      break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);

  // Load configuration (compiled defaults, overlaid by valid NVS values).
  loadConfig();

  // Initialize the temp & humidity sensor
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  if (!sht.begin())
  {
      Serial.println("Error connecting to SHT4x sensor");
      sampleError = true;
  }
  // Make an initial sensor reading while waiting for Ethernet port to negotiate and initialize
  timer.in(1000, sample);

  // Initialize Ethernet
  ETH.begin(ETH_TYPE, ETH_ADDR, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_POWER_PIN, ETH_CLK_MODE);
  while(!ETH.linkUp())
    {
      delay(1000);
    }

  // Configure the network interface per the selected DHCP mode.
  if (DHCPControl == DHCP_NEVER) {
    ETH.config(deviceIP, deviceGateway, deviceSubnet);
  } else {
    dhcp.setCustomOptions(authorizedSNMPOption, readCommunityOption);
    DHCPClient::printMAC(Serial);
    if (!dhcp.begin()) {
      Serial.println("DHCP: failed to bind UDP port 68");
    }

    // Allow the Ethernet PHY/netif to fully stabilize before the first DHCP
    // DISCOVER. On the ESP32-P4, link-up reports before the MAC is reliably
    // able to transmit, so the first (no-IP) DISCOVER is lost without this.
    delay(30000);

    if (DHCPControl == DHCP_ALWAYS) {
      // Block until every required option is present; log missing options.
      while (!dhcpNegotiateAndApply(true)) {
        delay(DHCP_RETRY_INTERVAL);
      }
      ETH.config(deviceIP, deviceGateway, deviceSubnet);
    } else { // DHCP_IFAVAILABLE
      dhcpNegotiateAndApply(false); // may reboot here if the network config changed
      ETH.config(deviceIP, deviceGateway, deviceSubnet);
    }
  }
  dhcpLastQueryMs = millis();

  // Revert monitoring: if the last boot applied a network change, watch for a
  // valid SNMP request within REVERT_TIMEOUT.
  if (DHCPControl == DHCP_IFAVAILABLE && getRevertPending()) {
    revertMonitoring = true;
    revertWindowStartMs = millis();
    Serial.println("Revert monitoring active (30 min window)");
  }

  // Print the active configuration for verification.
  printConfig();

  // Initialize sampling timer
  timer.every(30000, sample); // Take a sensor reading every 30 seconds

  // Begin listening for incoming GetRequest on UDP port 161
  if (udp.listen(161)) {
    Serial.print("UDP Listening on IP: ");
    Serial.println(ETH.localIP());
    // If we receive a packet, verify unicast, valid caller IP address and packet length, then parse and respond
    udp.onPacket([](AsyncUDPPacket packet) {
      // Discard broadcast or multicast messages
      if(!packet.isBroadcast() && !packet.isMulticast())
      {
        // Check for IP authentication and message size before processing any data
        if(authRequest(packet.remoteIP()) && packet.length() < (communityLen + 44) && packet.length() > (communityLen + 34))
        {
          Serial.print("Successful IP Authentication.");
          Serial.println();
          Serial.print("From: ");
          Serial.print(packet.remoteIP());
          Serial.print(":");
          Serial.print(packet.remotePort());
          Serial.println();

          switch (parseRequest(packet.data(),packet.length()))
          {
            case 0 : // Return uptime
            {
              Serial.print("Sending uptime.");
              sendGetResponse(0,packet.remoteIP(),packet.remotePort());
              blocking = false;
              snmpServiced = true;
              break;
            }
            case 1 : // Return hostname
              Serial.print("Sending hostname.");
              sendGetResponse(1,packet.remoteIP(),packet.remotePort());
              blocking = false;
              snmpServiced = true;
            break;
            case 2 : // Return .1 degrees C
              Serial.print("Sending .1 degrees C.");
              sendGetResponse(2,packet.remoteIP(),packet.remotePort());
              blocking = false;
              snmpServiced = true;
            break;
            case 3 : // Return degrees F
              Serial.print("Sending degrees F.");
              sendGetResponse(3,packet.remoteIP(),packet.remotePort());
              blocking = false;
              snmpServiced = true;
            break;
            case 4 : // Return humidity
              Serial.print("Sending humidity.");
              sendGetResponse(4,packet.remoteIP(),packet.remotePort());
              blocking = false;
              snmpServiced = true;
            break;
            case 5 : // Return sysName (hostname)
              Serial.print("Sending sysName.");
              sendGetResponse(5,packet.remoteIP(),packet.remotePort());
              blocking = false;
              snmpServiced = true;
            break;
            default : // Return error
              Serial.println("-1 returned from parser.  Ignore caller.");
              blocking = false;
            break;
          }
        }
      }
    });
  }
}

void loop() {
  handleSerialCommand(); // "G" command: generate DHCP option 231 value (no settings changed)
  timer.tick(); // Temp & humidity sampling

  // Revert monitoring: promote-to-good on first SNMP request, or revert on timeout.
  if (revertMonitoring) {
    if (snmpServiced) {
      priorIP = deviceIP;
      priorSubnet = deviceSubnet;
      priorGateway = deviceGateway;
      savePriorNetwork();
      setRevertPending(false);
      revertMonitoring = false;
      Serial.println("SNMP request serviced; network config promoted to known-good");
    } else if ((millis() - revertWindowStartMs) >= REVERT_TIMEOUT) {
      deviceIP = priorIP;
      deviceSubnet = priorSubnet;
      deviceGateway = priorGateway;
      saveConfig();
      setRevertPending(false);
      Serial.println("No SNMP in revert window; reverting network config and rebooting");
      ESP.restart();
    }
  }

  // Periodic DHCP re-query (IFAVAILABLE / ALWAYS).
  // Re-query at the sooner of DHCPQueryInterval and the lease's renewal time
  // (T1 = 50% of lease), so a short lease can never expire before we re-query.
  if (DHCPControl != DHCP_NEVER) {
    uint32_t intervalMs = 0;
    if (DHCPQueryInterval > 0) {
      intervalMs = DHCPQueryInterval * 1000UL;
    }
    if (dhcpLeaseTime > 0) {
      uint32_t t1Ms = dhcpLeaseTime * 500UL; // standard renewal at 50% of lease
      if (intervalMs == 0 || t1Ms < intervalMs) {
        intervalMs = t1Ms;
      }
    }
    if (intervalMs > 0 && (millis() - dhcpLastQueryMs) >= intervalMs) {
      dhcpLastQueryMs = millis();
      Serial.println("DHCP: re-query");
      dhcpNegotiateAndApply(DHCPControl == DHCP_ALWAYS);
    }
  }

  if (measurementInProgress && autoReady())
  {
    if (sht.getError() == SHT4x_OK)
    {
      float aTemperature = getAutoTemperature();
      float aHumidity = getAutoHumidity();
      fTemp = ctof(aTemperature);                          // Calculate degrees F
      cTemp = (int16_t)roundf(aTemperature * 10.0f);       // SNMP degrees C is an integer in .1 degrees C
      pHumidity = (uint8_t)roundf(aHumidity);

      Serial.print("Temp: ");
      Serial.print(cTemp);
      Serial.print("C, ");
      Serial.print(fTemp);
      Serial.print("F | Humidity: ");
      Serial.println(pHumidity);

      sampleError = false;
    }
    else
    {
      Serial.print("Sensor error: 0x");
      Serial.println(sht.getError(), HEX);
      sampleError = true;
    }
    measurementInProgress = false;
  }
}
