/*
GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor

Modified version:
- DHCP by default
- Optional static IPv4 configuration via Web UI
- Web UI protected with HTTP Basic Authentication (default admin/admin)
- Administrator password change
- Persistent settings in ESP32 Preferences/NVS
- Configurable SNMP read community via Web UI (default public)
- Configurable SNMP source whitelist (up to 8 hosts) or allow-any-host mode
- Human-readable Web UI uptime (HH:MM:SS)
*/

//////------------------------------------------- CONFIGURATION SETTINGS AREA --------------------------------------------////////

const char HOSTNAME[] = "SNMP_Temp"; // Set hostname

// SNMP settings are configured from the Web UI and stored in NVS.

/* Valid OIDs (Must query one OID per request):
### Uptime
1.3.6.1.2.1.1.3.0
### Hostname
1.3.6.1.4.1.119.2.1.3.0
### Temperature (.1 degrees C)
1.3.6.1.4.1.119.5.1.2.1.5.1
### Temperature (degrees F)
1.3.6.1.4.1.119.5.1.2.1.5.2
### Humidity (%)
1.3.6.1.4.1.119.5.1.2.1.6.1
*/

//////--------------------------------------- DO NOT EDIT ANYTHING BELOW THIS LINE ---------------------------------------////////
#include <ETH.h>
#include <AsyncUDP.h>
#include <SHT4x.h>
#include <arduino-timer.h>
#include <Preferences.h>
#include <WebServer.h>

#define SHT4X_DEBUG                 false
#define EQUILIBRIUM_WINDOW_SIZE     8
#define DEFAULT_EQUILIBRIUM_TIMEOUT 60000
#define DEFAULT_DT_THRESHOLD        0.034

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

//////---------------------------------------        Create runtime objects        ---------------------------------------////////

static const uint8_t HOSTNAME_LEN = sizeof(HOSTNAME)-1;

#define MAX_AUTHORIZED_HOSTS 8
bool allowSnmpFromAnyHost = false;
IPAddress authorizedHosts[MAX_AUTHORIZED_HOSTS];
uint8_t authorizedHostsQty = 0;

// Persistent configuration and Web UI
Preferences preferences;
WebServer webServer(80);

bool useDhcp = true;
IPAddress staticIp(192, 168, 1, 99);
IPAddress staticGateway(192, 168, 1, 1);
IPAddress staticSubnet(255, 255, 255, 0);
IPAddress staticDns(192, 168, 1, 1);
String adminPassword = "admin";
String snmpCommunity = "public";

bool rebootPending = false;
unsigned long rebootAt = 0;

// Asynchronous UDP object
AsyncUDP udp;

// Byte strings for managing SNMP packet data:
static const uint8_t SNMP_ASN1_0[1] = {0x30};
static const uint8_t SNMP_VER1_2[3] = {0x02,0x01,0x00};
static const uint8_t SNMP_VER2_2[3] = {0x02,0x01,0x01};
static const uint8_t SNMP_READCOMMUNITY_5[1] = {0x04};
static const uint8_t SNMP_GETREQUEST_7_LEN[1] = {0xa0};
uint8_t SNMP_GETREQUEST_DATA0[7] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00};
uint8_t SNMP_GETREQUEST_DATA1[2] = {0xa2,0x00};
uint8_t SNMP_GETREQUEST_DATA2a[8] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
uint8_t SNMP_GETREQUEST_DATA2b[6] = {0x00,0x00,0x00,0x00,0x00,0x00};
uint8_t SNMP_GETREQUEST_DATA3[6] = {0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t SNMP_GETUPTIME[10] = {0x2b,0x06,0x01,0x02,0x01,0x01,0x03,0x00,0x05,0x00};
static const uint8_t SNMP_GETHOST[12] = {0x2b,0x06,0x01,0x04,0x01,0x77,0x02,0x01,0x03,0x00,0x05,0x00};
static const uint8_t SNMP_GETTEMPC[14] = {0x2b,0x06,0x01,0x04,0x01,0x77,0x05,0x01,0x02,0x01,0x05,0x01,0x05,0x00};
static const uint8_t SNMP_GETTEMPF[14] = {0x2b,0x06,0x01,0x04,0x01,0x77,0x05,0x01,0x02,0x01,0x05,0x02,0x05,0x00};
static const uint8_t SNMP_GETHUMIDITY[14] = {0x2b,0x06,0x01,0x04,0x01,0x77,0x05,0x01,0x02,0x01,0x06,0x01,0x05,0x00};

volatile bool blocking = false;
volatile bool sampleError = false;

SHT4x sht;

// Forward declarations for functions in SHT4x_advancedFunctions.ino
bool requestAuto(measType initialMeasurement = SHT4x_MEASUREMENT_MEDIUM,
                 uint16_t timeout = DEFAULT_EQUILIBRIUM_TIMEOUT,
                 float threshold = DEFAULT_DT_THRESHOLD);
bool autoReady();
float getAutoTemperature();
float getAutoHumidity();
extern bool needsHeating;

auto timer = timer_create_default();
volatile bool measurementInProgress = false;

volatile uint8_t pHumidity = 0;
volatile int16_t fTemp = 0;
volatile int16_t cTemp = 0;

//////---------------------------------------       Function declarations        ---------------------------------------////////
bool sample(void *);
int ctof(float x);
bool authRequest(IPAddress callerIP);
int parseRequest(uint8_t *payload, size_t length);
void sendGetResponse(int request, IPAddress caller, uint16_t port);

bool parseIPv4(const String &value, IPAddress &out);
bool validSubnet(const IPAddress &mask);
bool sameSubnet(const IPAddress &a, const IPAddress &b, const IPAddress &mask);
void loadPersistentConfig();
void loadSnmpHostsFromNvs();
bool requireWebAuth();
String pageStart(const String &title);
String pageEnd();
void handleWebRoot();
void handleSaveNetwork();
void handleChangePassword();
void handleSaveSnmp();
String formatUptime();
void setupWebUI();
void scheduleReboot(unsigned long delayMs = 1500);

//////---------------------------------------           Web UI / NVS             ---------------------------------------////////

bool parseIPv4(const String &value, IPAddress &out)
{
  return out.fromString(value.c_str());
}

bool validSubnet(const IPAddress &mask)
{
  uint32_t m = ((uint32_t)mask[0] << 24) |
               ((uint32_t)mask[1] << 16) |
               ((uint32_t)mask[2] << 8)  |
               (uint32_t)mask[3];
  if (m == 0) return false;
  uint32_t inv = ~m;
  return (inv & (inv + 1)) == 0;
}

bool sameSubnet(const IPAddress &a, const IPAddress &b, const IPAddress &mask)
{
  for (int i = 0; i < 4; i++)
  {
    if ((a[i] & mask[i]) != (b[i] & mask[i])) return false;
  }
  return true;
}

void loadPersistentConfig()
{
  preferences.begin("poesp32", true);
  useDhcp = preferences.getBool("dhcp", true);
  adminPassword = preferences.getString("pass", "admin");
  snmpCommunity = preferences.getString("community", "public");

  String ipValue = preferences.getString("ip", "192.168.1.99");
  String maskValue = preferences.getString("mask", "255.255.255.0");
  String gwValue = preferences.getString("gw", "192.168.1.1");
  String dnsValue = preferences.getString("dns", "192.168.1.1");
  preferences.end();

  loadSnmpHostsFromNvs();

  parseIPv4(ipValue, staticIp);
  parseIPv4(maskValue, staticSubnet);
  parseIPv4(gwValue, staticGateway);
  parseIPv4(dnsValue, staticDns);

  if (!validSubnet(staticSubnet))
  {
    useDhcp = true;
    staticIp = IPAddress(192, 168, 1, 99);
    staticGateway = IPAddress(192, 168, 1, 1);
    staticSubnet = IPAddress(255, 255, 255, 0);
    staticDns = IPAddress(192, 168, 1, 1);
  }

  if (adminPassword.length() == 0)
    adminPassword = "admin";

  if (snmpCommunity.length() == 0 || snmpCommunity.length() > 32)
    snmpCommunity = "public";
}

void loadSnmpHostsFromNvs()
{
  authorizedHostsQty = 0;

  preferences.begin("poesp32", true);
  allowSnmpFromAnyHost = preferences.getBool("snmp_any", false);
  bool hostsConfigured = preferences.getBool("snmp_hosts_set", false);

  if (!hostsConfigured)
  {
    // Preserve the original project's default whitelist on first upgrade.
    authorizedHosts[authorizedHostsQty++] = IPAddress(192, 168, 1, 1);
    authorizedHosts[authorizedHostsQty++] = IPAddress(192, 168, 1, 10);
    preferences.end();
    return;
  }

  for (uint8_t i = 0; i < MAX_AUTHORIZED_HOSTS; i++)
  {
    char key[12];
    snprintf(key, sizeof(key), "snmp_h%u", i);
    String value = preferences.getString(key, "");
    value.trim();

    if (value.length() == 0) continue;

    IPAddress host;
    if (parseIPv4(value, host))
      authorizedHosts[authorizedHostsQty++] = host;
  }

  preferences.end();
}

bool requireWebAuth()
{
  if (webServer.authenticate("admin", adminPassword.c_str())) return true;
  webServer.requestAuthentication(BASIC_AUTH, "PoESP32 Management");
  return false;
}

String pageStart(const String &title)
{
  String html;
  html.reserve(5000);
  html += F("<!doctype html><html><head><meta charset='utf-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>"); html += title; html += F("</title><style>");
  html += F("body{font-family:Arial,sans-serif;background:#f3f5f7;color:#222;margin:0;padding:24px}");
  html += F(".wrap{max-width:760px;margin:auto}.card{background:#fff;border-radius:10px;padding:20px;margin:0 0 18px;box-shadow:0 1px 5px #bbb}");
  html += F("h1{font-size:24px}h2{font-size:19px;margin-top:0}.grid{display:grid;grid-template-columns:180px 1fr;gap:8px 14px}");
  html += F("label{display:block;margin-top:10px;font-weight:600}input[type=text],input[type=password]{width:100%;box-sizing:border-box;padding:9px;margin-top:4px}");
  html += F("button{margin-top:16px;padding:10px 18px;cursor:pointer}.warn{background:#fff3cd;padding:10px;border-radius:6px}.muted{color:#666}");
  html += F("</style></head><body><div class='wrap'>");
  return html;
}

String pageEnd()
{
  return F("</div></body></html>");
}

void handleWebRoot()
{
  if (!requireWebAuth()) return;

  String html = pageStart("PoESP32 Management");
  html += F("<h1>PoESP32 Environmental Monitor</h1>");

  if (adminPassword == "admin")
    html += F("<div class='warn'><b>Warning:</b> default administrator password is still in use.</div><br>");

  html += F("<div class='card'><h2>Status</h2><div class='grid'>");
  html += F("<div>Hostname</div><div>"); html += HOSTNAME; html += F("</div>");
  html += F("<div>Ethernet</div><div>"); html += (ETH.linkUp() ? "Connected" : "Disconnected"); html += F("</div>");
  html += F("<div>Address mode</div><div>"); html += (useDhcp ? "DHCP" : "Static"); html += F("</div>");
  html += F("<div>IP address</div><div>"); html += ETH.localIP().toString(); html += F("</div>");
  html += F("<div>Subnet</div><div>"); html += ETH.subnetMask().toString(); html += F("</div>");
  html += F("<div>Gateway</div><div>"); html += ETH.gatewayIP().toString(); html += F("</div>");
  html += F("<div>DNS</div><div>"); html += ETH.dnsIP().toString(); html += F("</div>");
  html += F("<div>MAC</div><div>"); html += ETH.macAddress(); html += F("</div>");
  html += F("<div>Temperature</div><div>"); html += String((float)cTemp / 10.0f, 1); html += F(" &deg;C</div>");
  html += F("<div>Humidity</div><div>"); html += String(pHumidity); html += F(" %</div>");
  html += F("<div>Uptime</div><div>"); html += formatUptime(); html += F("</div>");
  html += F("</div></div>");

  html += F("<div class='card'><h2>Network</h2><form method='post' action='/save-network'>");
  html += F("<label><input type='radio' name='mode' value='dhcp' "); if (useDhcp) html += F("checked"); html += F("> DHCP</label>");
  html += F("<label><input type='radio' name='mode' value='static' "); if (!useDhcp) html += F("checked"); html += F("> Static IP</label>");
  html += F("<label>IP address<input type='text' name='ip' value='"); html += staticIp.toString(); html += F("'></label>");
  html += F("<label>Subnet mask<input type='text' name='mask' value='"); html += staticSubnet.toString(); html += F("'></label>");
  html += F("<label>Gateway<input type='text' name='gw' value='"); html += staticGateway.toString(); html += F("'></label>");
  html += F("<label>DNS<input type='text' name='dns' value='"); html += staticDns.toString(); html += F("'></label>");
  html += F("<button type='submit'>Save network &amp; restart</button></form></div>");

  html += F("<div class='card'><h2>SNMP</h2><form method='post' action='/save-snmp'>");
  html += F("<label>Read community<input type='text' name='community' maxlength='32' value='"); html += snmpCommunity; html += F("'></label>");
  html += F("<label><input type='checkbox' name='allow_any' value='1' ");
  if (allowSnmpFromAnyHost) html += F("checked");
  html += F("> Allow SNMP from any host</label>");
  html += F("<p class='muted'>When this option is disabled, only the IP addresses below may query the device.</p>");

  for (uint8_t i = 0; i < MAX_AUTHORIZED_HOSTS; i++)
  {
    html += F("<label>Allowed host ");
    html += String(i + 1);
    html += F("<input type='text' name='host");
    html += String(i);
    html += F("' placeholder='192.168.1.10' value='");
    if (i < authorizedHostsQty) html += authorizedHosts[i].toString();
    html += F("'></label>");
  }

  html += F("<button type='submit'>Save SNMP settings</button></form></div>");

  html += F("<div class='card'><h2>Security</h2><p class='muted'>Administrator: <b>admin</b></p>");
  html += F("<form method='post' action='/change-password'>");
  html += F("<label>Current password<input type='password' name='current' autocomplete='current-password'></label>");
  html += F("<label>New password<input type='password' name='newpass' autocomplete='new-password'></label>");
  html += F("<label>Repeat new password<input type='password' name='confirm' autocomplete='new-password'></label>");
  html += F("<button type='submit'>Change password</button></form></div>");

  html += pageEnd();
  webServer.send(200, "text/html; charset=utf-8", html);
}

void handleSaveNetwork()
{
  if (!requireWebAuth()) return;

  bool newDhcp = webServer.arg("mode") != "static";
  IPAddress newIp, newMask, newGateway, newDns;

  if (!newDhcp)
  {
    if (!parseIPv4(webServer.arg("ip"), newIp) ||
        !parseIPv4(webServer.arg("mask"), newMask) ||
        !parseIPv4(webServer.arg("gw"), newGateway) ||
        !parseIPv4(webServer.arg("dns"), newDns))
    {
      webServer.send(400, "text/plain; charset=utf-8", "Invalid IPv4 address.");
      return;
    }

    if (!validSubnet(newMask))
    {
      webServer.send(400, "text/plain; charset=utf-8", "Invalid subnet mask.");
      return;
    }

    if (!sameSubnet(newIp, newGateway, newMask))
    {
      webServer.send(400, "text/plain; charset=utf-8", "IP address and gateway must be in the same subnet.");
      return;
    }
  }

  preferences.begin("poesp32", false);
  preferences.putBool("dhcp", newDhcp);

  if (!newDhcp)
  {
    preferences.putString("ip", newIp.toString());
    preferences.putString("mask", newMask.toString());
    preferences.putString("gw", newGateway.toString());
    preferences.putString("dns", newDns.toString());
  }
  preferences.end();

  String html = pageStart("Network saved");
  html += F("<div class='card'><h2>Network configuration saved</h2><p>The device is restarting.</p>");

  if (newDhcp)
  {
    html += F("<p>The device will request a new address using DHCP.</p>");
  }
  else
  {
    html += F("<p>New address: <b>"); html += newIp.toString(); html += F("</b></p>");
  }

  html += F("</div>");
  html += pageEnd();
  webServer.send(200, "text/html; charset=utf-8", html);

  scheduleReboot(1800);
}

void handleChangePassword()
{
  if (!requireWebAuth()) return;

  String current = webServer.arg("current");
  String newPass = webServer.arg("newpass");
  String confirm = webServer.arg("confirm");

  if (current != adminPassword)
  {
    webServer.send(403, "text/plain; charset=utf-8", "Current password is incorrect.");
    return;
  }

  if (newPass.length() < 8)
  {
    webServer.send(400, "text/plain; charset=utf-8", "New password must contain at least 8 characters.");
    return;
  }

  if (newPass != confirm)
  {
    webServer.send(400, "text/plain; charset=utf-8", "New passwords do not match.");
    return;
  }

  preferences.begin("poesp32", false);
  preferences.putString("pass", newPass);
  preferences.end();
  adminPassword = newPass;

  webServer.send(200, "text/plain; charset=utf-8", "Password changed. Re-open the Web UI and authenticate with the new password.");
}

String formatUptime()
{
  uint32_t totalSeconds = millis() / 1000UL;
  uint32_t hours = totalSeconds / 3600UL;
  uint8_t minutes = (totalSeconds % 3600UL) / 60UL;
  uint8_t seconds = totalSeconds % 60UL;
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%lu:%02u:%02u",
           (unsigned long)hours, minutes, seconds);
  return String(buffer);
}

void handleSaveSnmp()
{
  if (!requireWebAuth()) return;

  String newCommunity = webServer.arg("community");
  newCommunity.trim();

  if (newCommunity.length() < 1 || newCommunity.length() > 32)
  {
    webServer.send(400, "text/plain; charset=utf-8", "SNMP community must contain 1-32 characters.");
    return;
  }

  bool newAllowAny = webServer.hasArg("allow_any");
  IPAddress newHosts[MAX_AUTHORIZED_HOSTS];
  uint8_t newHostsQty = 0;

  for (uint8_t i = 0; i < MAX_AUTHORIZED_HOSTS; i++)
  {
    String argName = "host" + String(i);
    String value = webServer.arg(argName);
    value.trim();

    if (value.length() == 0) continue;

    IPAddress host;
    if (!parseIPv4(value, host))
    {
      String message = "Invalid SNMP host address in field " + String(i + 1) + ": " + value;
      webServer.send(400, "text/plain; charset=utf-8", message);
      return;
    }

    // Avoid duplicate entries.
    bool duplicate = false;
    for (uint8_t j = 0; j < newHostsQty; j++)
    {
      if (newHosts[j] == host)
      {
        duplicate = true;
        break;
      }
    }

    if (!duplicate)
      newHosts[newHostsQty++] = host;
  }

  if (!newAllowAny && newHostsQty == 0)
  {
    webServer.send(400, "text/plain; charset=utf-8",
                   "Add at least one allowed SNMP host or enable 'Allow SNMP from any host'.");
    return;
  }

  preferences.begin("poesp32", false);
  preferences.putString("community", newCommunity);
  preferences.putBool("snmp_any", newAllowAny);
  preferences.putBool("snmp_hosts_set", true);

  for (uint8_t i = 0; i < MAX_AUTHORIZED_HOSTS; i++)
  {
    char key[12];
    snprintf(key, sizeof(key), "snmp_h%u", i);

    if (i < newHostsQty)
      preferences.putString(key, newHosts[i].toString());
    else
      preferences.remove(key);
  }
  preferences.end();

  snmpCommunity = newCommunity;
  allowSnmpFromAnyHost = newAllowAny;
  authorizedHostsQty = newHostsQty;
  for (uint8_t i = 0; i < newHostsQty; i++)
    authorizedHosts[i] = newHosts[i];

  String html = pageStart("SNMP saved");
  html += F("<div class='card'><h2>SNMP configuration saved</h2>");
  html += F("<p>The new settings are active immediately.</p>");
  html += F("<p>Access policy: <b>");
  html += (allowSnmpFromAnyHost ? "any host" : "whitelist only");
  html += F("</b></p><p><a href='/'>Return to management</a></p></div>");
  html += pageEnd();
  webServer.send(200, "text/html; charset=utf-8", html);
}

void setupWebUI()
{
  webServer.on("/", HTTP_GET, handleWebRoot);
  webServer.on("/save-network", HTTP_POST, handleSaveNetwork);
  webServer.on("/change-password", HTTP_POST, handleChangePassword);
  webServer.on("/save-snmp", HTTP_POST, handleSaveSnmp);

  webServer.onNotFound([]() {
    if (!requireWebAuth()) return;
    webServer.send(404, "text/plain", "Not found");
  });

  webServer.begin();
  Serial.println("Web UI started on TCP port 80");
}

void scheduleReboot(unsigned long delayMs)
{
  rebootPending = true;
  rebootAt = millis() + delayMs;
}

//////---------------------------------------        Sensor / SNMP functions      ---------------------------------------////////

bool sample(void *)
{
  if (measurementInProgress) return true;

  if (!requestAuto())
  {
    Serial.println("Error starting auto measurement");
    sampleError = true;
    return true;
  }

  measurementInProgress = true;
  return true;
}

int ctof(float x)
{
  return (int)roundf(1.8f * x + 32.0f);
}

bool authRequest(IPAddress callerIP)
{
  if (allowSnmpFromAnyHost) return true;

  for (uint8_t i = 0; i < authorizedHostsQty; i++)
  {
    if (callerIP == authorizedHosts[i])
      return true;
  }

  return false;
}

int parseRequest(uint8_t *payload, size_t length)
{
  if (!payload || blocking || length < 20) return -1;

  blocking = true;
  size_t communityLen = snmpCommunity.length();

  // Minimum header: SEQUENCE, version, community type+length+value, GetRequest.
  if (length < 8 + communityLen) return -1;

  if (memcmp(SNMP_ASN1_0, payload, sizeof(SNMP_ASN1_0)) != 0) return -1;
  Serial.println("ASN1: Valid");

  if (memcmp(SNMP_VER1_2, payload + 2, sizeof(SNMP_VER1_2)) != 0 &&
      memcmp(SNMP_VER2_2, payload + 2, sizeof(SNMP_VER2_2)) != 0)
    return -1;
  Serial.println("SNMP Version 1 or 2: Valid");

  if (payload[5] != SNMP_READCOMMUNITY_5[0]) return -1;
  Serial.println("Read Community Supplied");

  if (payload[6] != communityLen) return -1;
  Serial.println("Read Community Length Matched");

  if (memcmp(payload + 7, snmpCommunity.c_str(), communityLen) != 0) return -1;
  Serial.println("Read Community Value Matched");

  // GetRequest tag follows immediately after the community value.
  size_t pduTagOffset = 7 + communityLen;
  if (pduTagOffset >= length || payload[pduTagOffset] != SNMP_GETREQUEST_7_LEN[0]) return -1;
  Serial.println("Processing GetRequest...");

  // Preserve the original response-building layout.
  memcpy(SNMP_GETREQUEST_DATA0, payload, 7);

  size_t ridLenOffset = 10 + communityLen;
  if (ridLenOffset >= length) return -1;
  uint8_t RIDLength = payload[ridLenOffset];
  if (RIDLength > 6) return -1;

  size_t data2aOffset = 9 + communityLen;
  if (data2aOffset + RIDLength + 2 + 6 + 6 > length) return -1;

  memcpy(SNMP_GETREQUEST_DATA2a, payload + data2aOffset, RIDLength + 2);
  memcpy(SNMP_GETREQUEST_DATA2b, payload + data2aOffset + RIDLength + 2, 6);
  memcpy(SNMP_GETREQUEST_DATA3, payload + data2aOffset + RIDLength + 2 + 6, 6);

  if (length >= sizeof(SNMP_GETUPTIME) && memcmp(SNMP_GETUPTIME, payload + length - sizeof(SNMP_GETUPTIME), sizeof(SNMP_GETUPTIME)) == 0) return 0;
  if (length >= sizeof(SNMP_GETHOST) && memcmp(SNMP_GETHOST, payload + length - sizeof(SNMP_GETHOST), sizeof(SNMP_GETHOST)) == 0) return 1;
  if (length >= sizeof(SNMP_GETTEMPC) && memcmp(SNMP_GETTEMPC, payload + length - sizeof(SNMP_GETTEMPC), sizeof(SNMP_GETTEMPC)) == 0) return 2;
  if (length >= sizeof(SNMP_GETTEMPF) && memcmp(SNMP_GETTEMPF, payload + length - sizeof(SNMP_GETTEMPF), sizeof(SNMP_GETTEMPF)) == 0) return 3;
  if (length >= sizeof(SNMP_GETHUMIDITY) && memcmp(SNMP_GETHUMIDITY, payload + length - sizeof(SNMP_GETHUMIDITY), sizeof(SNMP_GETHUMIDITY)) == 0) return 4;

  Serial.println("Unsupported or invalid SNMP payload.");
  return -1;
}

void sendGetResponse(int request, IPAddress caller, uint16_t port)
{
  size_t communityLen = snmpCommunity.length();
  if (request > 1)
  {
    if (sampleError || pHumidity == 0 || pHumidity > 100 || fTemp < -40 || fTemp >= 141 || cTemp < -400 || cTemp >= 601)
      SNMP_GETREQUEST_DATA2b[2] = 0x05;
  }

  uint8_t RIDLength = SNMP_GETREQUEST_DATA2a[1];

  switch (request)
  {
    case 0:
    {
      uint8_t dataType[2] = {0x43,0x04};
      int64_t microval = esp_timer_get_time()/10000;
      uint32_t val = static_cast<uint32_t>(microval & 0x0FFFFFFFF);
      uint8_t value[4];
      value[0] = (val >> 24) & 0xFF;
      value[1] = (val >> 16) & 0xFF;
      value[2] = (val >> 8) & 0xFF;
      value[3] = val & 0xFF;
      uint8_t PDULen = 24 + RIDLength + 4;
      SNMP_GETREQUEST_DATA1[1] = PDULen;
      SNMP_GETREQUEST_DATA3[1] += 4;
      SNMP_GETREQUEST_DATA3[3] += 4;
      uint8_t packetLen = PDULen + 7 + communityLen;
      SNMP_GETREQUEST_DATA0[1] = packetLen;
      int responseBytes = packetLen + 2;
      uint8_t responsePayload[responseBytes];
      memcpy(responsePayload,SNMP_GETREQUEST_DATA0,7);
      memcpy(responsePayload+7,snmpCommunity.c_str(),communityLen);
      memcpy(responsePayload+7+communityLen,SNMP_GETREQUEST_DATA1,2);
      memcpy(responsePayload+9+communityLen,SNMP_GETREQUEST_DATA2a,RIDLength+2);
      memcpy(responsePayload+9+communityLen+RIDLength+2,SNMP_GETREQUEST_DATA2b,6);
      memcpy(responsePayload+17+communityLen+RIDLength,SNMP_GETREQUEST_DATA3,6);
      memcpy(responsePayload+23+communityLen+RIDLength,SNMP_GETUPTIME,8);
      memcpy(responsePayload+31+communityLen+RIDLength,dataType,2);
      memcpy(responsePayload+33+communityLen+RIDLength,value,4);
      udp.writeTo(responsePayload,responseBytes,caller,port);
      Serial.println();
      break;
    }

    case 1:
    {
      uint8_t dataType[2] = {0x04,HOSTNAME_LEN};
      uint8_t PDULen = 26 + RIDLength + HOSTNAME_LEN;
      SNMP_GETREQUEST_DATA1[1] = PDULen;
      SNMP_GETREQUEST_DATA3[1] += HOSTNAME_LEN;
      SNMP_GETREQUEST_DATA3[3] += HOSTNAME_LEN;
      uint8_t packetLen = PDULen + 7 + communityLen;
      SNMP_GETREQUEST_DATA0[1] = packetLen;
      int responseBytes = packetLen + 2;
      uint8_t responsePayload[responseBytes];
      memcpy(responsePayload,SNMP_GETREQUEST_DATA0,7);
      memcpy(responsePayload+7,snmpCommunity.c_str(),communityLen);
      memcpy(responsePayload+7+communityLen,SNMP_GETREQUEST_DATA1,2);
      memcpy(responsePayload+9+communityLen,SNMP_GETREQUEST_DATA2a,RIDLength+2);
      memcpy(responsePayload+9+communityLen+RIDLength+2,SNMP_GETREQUEST_DATA2b,6);
      memcpy(responsePayload+17+communityLen+RIDLength,SNMP_GETREQUEST_DATA3,6);
      memcpy(responsePayload+23+communityLen+RIDLength,SNMP_GETHOST,10);
      memcpy(responsePayload+33+communityLen+RIDLength,dataType,2);
      memcpy(responsePayload+35+communityLen+RIDLength,HOSTNAME,HOSTNAME_LEN);
      udp.writeTo(responsePayload,responseBytes,caller,port);
      Serial.println();
      break;
    }

    case 2:
    {
      uint8_t dataType[2] = {0x02,0x02};
      uint8_t value[2];
      value[0] = (cTemp >> 8) & 0xFF;
      value[1] = cTemp & 0xFF;
      uint8_t PDULen = 28 + RIDLength + 2;
      SNMP_GETREQUEST_DATA1[1] = PDULen;
      SNMP_GETREQUEST_DATA3[1] += 2;
      SNMP_GETREQUEST_DATA3[3] += 2;
      uint8_t packetLen = PDULen + 7 + communityLen;
      SNMP_GETREQUEST_DATA0[1] = packetLen;
      int responseBytes = packetLen + 2;
      uint8_t responsePayload[responseBytes];
      memcpy(responsePayload,SNMP_GETREQUEST_DATA0,7);
      memcpy(responsePayload+7,snmpCommunity.c_str(),communityLen);
      memcpy(responsePayload+7+communityLen,SNMP_GETREQUEST_DATA1,2);
      memcpy(responsePayload+9+communityLen,SNMP_GETREQUEST_DATA2a,RIDLength+2);
      memcpy(responsePayload+9+communityLen+RIDLength+2,SNMP_GETREQUEST_DATA2b,6);
      memcpy(responsePayload+17+communityLen+RIDLength,SNMP_GETREQUEST_DATA3,6);
      memcpy(responsePayload+23+communityLen+RIDLength,SNMP_GETTEMPC,12);
      memcpy(responsePayload+35+communityLen+RIDLength,dataType,2);
      memcpy(responsePayload+37+communityLen+RIDLength,value,2);
      udp.writeTo(responsePayload,responseBytes,caller,port);
      Serial.println();
      break;
    }

    case 3:
    {
      uint8_t dataType[2] = {0x02,0x02};
      uint8_t value[2];
      value[0] = (fTemp >> 8) & 0xFF;
      value[1] = fTemp & 0xFF;
      uint8_t PDULen = 28 + RIDLength + 2;
      SNMP_GETREQUEST_DATA1[1] = PDULen;
      SNMP_GETREQUEST_DATA3[1] += 2;
      SNMP_GETREQUEST_DATA3[3] += 2;
      uint8_t packetLen = PDULen + 7 + communityLen;
      SNMP_GETREQUEST_DATA0[1] = packetLen;
      int responseBytes = packetLen + 2;
      uint8_t responsePayload[responseBytes];
      memcpy(responsePayload,SNMP_GETREQUEST_DATA0,7);
      memcpy(responsePayload+7,snmpCommunity.c_str(),communityLen);
      memcpy(responsePayload+7+communityLen,SNMP_GETREQUEST_DATA1,2);
      memcpy(responsePayload+9+communityLen,SNMP_GETREQUEST_DATA2a,RIDLength+2);
      memcpy(responsePayload+9+communityLen+RIDLength+2,SNMP_GETREQUEST_DATA2b,6);
      memcpy(responsePayload+17+communityLen+RIDLength,SNMP_GETREQUEST_DATA3,6);
      memcpy(responsePayload+23+communityLen+RIDLength,SNMP_GETTEMPF,12);
      memcpy(responsePayload+35+communityLen+RIDLength,dataType,2);
      memcpy(responsePayload+37+communityLen+RIDLength,value,2);
      udp.writeTo(responsePayload,responseBytes,caller,port);
      Serial.println();
      break;
    }

    case 4:
    {
      uint8_t dataType[2] = {0x02,0x01};
      uint8_t PDULen = 28 + RIDLength + 1;
      SNMP_GETREQUEST_DATA1[1] = PDULen;
      SNMP_GETREQUEST_DATA3[1] += 1;
      SNMP_GETREQUEST_DATA3[3] += 1;
      uint8_t packetLen = PDULen + 7 + communityLen;
      SNMP_GETREQUEST_DATA0[1] = packetLen;
      int responseBytes = packetLen + 2;
      uint8_t responsePayload[responseBytes];
      memcpy(responsePayload,SNMP_GETREQUEST_DATA0,7);
      memcpy(responsePayload+7,snmpCommunity.c_str(),communityLen);
      memcpy(responsePayload+7+communityLen,SNMP_GETREQUEST_DATA1,2);
      memcpy(responsePayload+9+communityLen,SNMP_GETREQUEST_DATA2a,RIDLength+2);
      memcpy(responsePayload+9+communityLen+RIDLength+2,SNMP_GETREQUEST_DATA2b,6);
      memcpy(responsePayload+17+communityLen+RIDLength,SNMP_GETREQUEST_DATA3,6);
      memcpy(responsePayload+23+communityLen+RIDLength,SNMP_GETHUMIDITY,12);
      memcpy(responsePayload+35+communityLen+RIDLength,dataType,2);
      responsePayload[37+communityLen+RIDLength] = pHumidity;
      udp.writeTo(responsePayload,responseBytes,caller,port);
      Serial.println();
      break;
    }
  }
}

//////---------------------------------------               Setup                 ---------------------------------------////////

void setup()
{
  Serial.begin(115200);
  delay(200);

  loadPersistentConfig();

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
  ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_TYPE, ETH_CLK_MODE);

  // Calling ETH.config() switches the interface to a static address.
  // When DHCP is selected, we intentionally do not call ETH.config().
  if (!useDhcp)
  {
    ETH.config(staticIp, staticGateway, staticSubnet, staticDns);
  }

  Serial.println("Waiting for Ethernet link...");
  while (!ETH.linkUp())
  {
    delay(500);
  }

  Serial.println("Ethernet link UP");

  if (useDhcp)
  {
    Serial.println("Waiting for DHCP...");
    unsigned long dhcpStarted = millis();

    while (ETH.localIP() == IPAddress(0, 0, 0, 0) && millis() - dhcpStarted < 30000UL)
    {
      delay(250);
    }

    if (ETH.localIP() == IPAddress(0, 0, 0, 0))
      Serial.println("DHCP timeout: no IPv4 address obtained yet");
  }

  Serial.print("Ethernet IP: ");
  Serial.println(ETH.localIP());
  Serial.print("Gateway: ");
  Serial.println(ETH.gatewayIP());
  Serial.print("Subnet: ");
  Serial.println(ETH.subnetMask());
  Serial.print("DNS: ");
  Serial.println(ETH.dnsIP());
  Serial.print("MAC: ");
  Serial.println(ETH.macAddress());

  setupWebUI();

  // Initialize sampling timer
  timer.every(30000, sample);

  // Begin listening for incoming GetRequest on UDP port 161
  if (udp.listen(161))
  {
    Serial.print("UDP Listening on IP: ");
    Serial.println(ETH.localIP());

    udp.onPacket([](AsyncUDPPacket packet)
    {
      if (!packet.isBroadcast() && !packet.isMulticast())
      {
        if (authRequest(packet.remoteIP()) &&
            packet.length() < (snmpCommunity.length() + 80) &&
            packet.length() > (snmpCommunity.length() + 20))
        {
          Serial.println("Successful IP Authentication.");
          Serial.print("From: ");
          Serial.print(packet.remoteIP());
          Serial.print(":");
          Serial.println(packet.remotePort());

          switch (parseRequest(packet.data(), packet.length()))
          {
            case 0:
              Serial.print("Sending uptime.");
              sendGetResponse(0, packet.remoteIP(), packet.remotePort());
              blocking = false;
              break;

            case 1:
              Serial.print("Sending hostname.");
              sendGetResponse(1, packet.remoteIP(), packet.remotePort());
              blocking = false;
              break;

            case 2:
              Serial.print("Sending .1 degrees C.");
              sendGetResponse(2, packet.remoteIP(), packet.remotePort());
              blocking = false;
              break;

            case 3:
              Serial.print("Sending degrees F.");
              sendGetResponse(3, packet.remoteIP(), packet.remotePort());
              blocking = false;
              break;

            case 4:
              Serial.print("Sending humidity.");
              sendGetResponse(4, packet.remoteIP(), packet.remotePort());
              blocking = false;
              break;

            default:
              Serial.print("-1 returned from parser. Ignore caller.");
              blocking = false;
              break;
          }
        }
      }
    });
  }
}

//////---------------------------------------                Loop                 ---------------------------------------////////

void loop()
{
  webServer.handleClient();

  if (rebootPending && (long)(millis() - rebootAt) >= 0)
  {
    delay(50);
    ESP.restart();
  }

  timer.tick();

  if (measurementInProgress && autoReady())
  {
    if (sht.getError() == SHT4x_OK)
    {
      float aTemperature = getAutoTemperature();
      float aHumidity = getAutoHumidity();
      fTemp = ctof(aTemperature);
      cTemp = (int16_t)roundf(aTemperature * 10.0f);
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
