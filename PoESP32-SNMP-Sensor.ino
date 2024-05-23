////////------------------------------------------- CONFIGURATION SETTINGS AREA --------------------------------------------////////

const uint8_t HOSTNAME[] = "PoESP32-Unit"; // Set hostname

// Ethernet configuration
IPAddress ip(192, 168, 1, 99); // Set device IP address.
IPAddress gateway(192, 168, 1, 1); // Set default gateway IP address.
IPAddress subnet(255, 255, 255, 0); // Set network subnet mask.

// SNMP read community configuration
const uint8_t SNMP_READCOMMUNITY_VALUE_7[] = "readonly"; // Set SNMP read community for your environment.

//The AUTHORIZED_HOSTS list should include the IP of any hosts that may query this device via SNMP.
const IPAddress AUTHORIZED_HOSTS[2] = {IPAddress(192,168,1,1),IPAddress(192,168,1,10)};
//Update the array size here     ^    to reflect the number of authorized host IPAddress entries you listed.
const int AUTHORIZED_HOSTS_QTY = 2;
//Update this number to match    ^    the one above.

/* Valid OIDs (Must query one OID per request):
### Uptime (0 to 49 days before value wraps)
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

////////--------------------------------------- DO NOT EDIT ANYTHING BELOW THIS LINE ---------------------------------------////////

#include <ETH.h>
#include <AsyncUDP.h>
#include <SensirionI2cSht4x.h>
#include <arduino-timer.h>

#define ETH_ADDR        1
#define ETH_POWER_PIN   5
#define ETH_TYPE        ETH_PHY_IP101
#define ETH_PHY_MDC     23
#define ETH_PHY_MDIO    18

////////---------------------------------------        Create runtime objects        ---------------------------------------////////

// Calculate the length of the user-specifed hostname string.
static const uint8_t HOSTNAME_LEN = sizeof(HOSTNAME)-1;

// Asynchronous UDP object
AsyncUDP udp;

// Byte strings for managing SNMP packet data
static const uint8_t SNMP_ASN1_0[1] = {0x30};
static const uint8_t SNMP_VER1_2[3] = {0x02,0x01,0x00};
static const uint8_t SNMP_VER2_2[3] = {0x02,0x01,0x01};
static const uint8_t SNMP_READCOMMUNITY_5[1] = {0x04};
static const uint8_t SNMP_READCOMMUNITY_LEN_6[1] = {sizeof(SNMP_READCOMMUNITY_VALUE_7)-1};
static const uint8_t SNMP_GETREQUEST_7_LEN[1] = {0xa0};

uint8_t SNMP_GETREQUEST_DATA0[7] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00}; // ASN.1, length (calc at runtime), ver int, ver len, ver value, community, community len
uint8_t SNMP_GETREQUEST_DATA1[2] = {0xa2,0x00}; // request_type (0xa2), length (calc at runtime)
uint8_t SNMP_GETREQUEST_DATA2a[8] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}; // Request ID int, RID length, RID ID[0], RID ID[1], RID ID[2], RID ID[3], RID ID[4], RID ID[5]
uint8_t SNMP_GETREQUEST_DATA2b[6] = {0x00,0x00,0x00,0x00,0x00,0x00}; // err int, err len, err value, index int, index len, index value
uint8_t SNMP_GETREQUEST_DATA3[6] = {0x00,0x00,0x00,0x00,0x00,0x00}; // Varbind list type, Varbind list len, Varbind type, Varbind len, Object ID, Object ID len

static const uint8_t SNMP_GETUPTIME[10] = {0x2b,0x06,0x01,0x02,0x01,0x01,0x03,0x00,0x05,0x00};
static const uint8_t SNMP_GETHOST[12] = {0x2b,0x06,0x01,0x04,0x01,0x77,0x02,0x01,0x03,0x00,0x05,0x00};
static const uint8_t SNMP_GETTEMPC[14] = {0x2b,0x06,0x01,0x04,0x01,0x77,0x05,0x01,0x02,0x01,0x05,0x01,0x05,0x00};
static const uint8_t SNMP_GETTEMPF[14] = {0x2b,0x06,0x01,0x04,0x01,0x77,0x05,0x01,0x02,0x01,0x05,0x02,0x05,0x00};
static const uint8_t SNMP_GETHUMIDITY[14] = {0x2b,0x06,0x01,0x04,0x01,0x77,0x05,0x01,0x02,0x01,0x06,0x01,0x05,0x00};

// Blocking flag to avoid packet processing contention in case we're flooded with requests
bool blocking = false;

// Sampling result flag to indicate a problem with the SHT40 sensor
bool sampleError = false;

// I2C SHT40 sensor object and setup
SensirionI2cSht4x sensor;
#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0
static char errorMessage[64];
static int16_t error;

// Periodic temp & humidity sampling timer
auto timer = timer_create_default();

// Holds current sample values
uint8_t pHumidity = 0;
uint8_t fTemp = 0;
int16_t cTemp = 0;

////////---------------------------------------     End create runtime objects     ---------------------------------------////////

////////---------------------------------------       Function declarations        ---------------------------------------////////

// SHT40 sensor sampling function, used by the non-blocking timer and executed every 30 seconds
bool sample(void *);
// Celcius to farenheit conversion
int ctof(float x);
// Verified authorized caller by checking request IP
bool authRequest(IPAddress callerIP);
// Parses incoming SNMP messages received from valid authorized IP addresses
int parseRequest(uint8_t *payload, size_t length);
// Construct and send the response message for valid OID getRequests
void sendGetResponse(int request, IPAddress caller, uint16_t port);

////////---------------------------------------     End function declarations      ---------------------------------------////////

bool sample(void *)
{
    float aTemperature = 0.0;
    float Humidity = 0.0;
    if(pHumidity < 76 || (pHumidity > 75 && cTemp > 500))
    {
      error = sensor.measureMediumPrecision(aTemperature, Humidity); // Take a standard reading
    }
    else
    {
      error = sensor.activateMediumHeaterPowerShort(aTemperature, Humidity); // High humidity, activate internal heater for better accuracy
    }

    if (error != NO_ERROR)
    {
      Serial.print("Error trying to execute measurement: ");
      errorToString(error, errorMessage, sizeof errorMessage);
      Serial.println(errorMessage);
      sampleError = true; // Flip sampleArror flag so we are aware we have a problem.
      return true; // Leave timer enabled even though the sample failed.
    }

    fTemp = ctof(aTemperature); // Calculate degrees F
    cTemp = aTemperature * 10; // SNMP degrees C is an integer in .1 degrees C
    pHumidity = round(Humidity);

    Serial.print("Temp: ");
    Serial.print(cTemp);
    Serial.print("C, ");
    Serial.print(fTemp);
    Serial.print("F | Humidity: ");
    Serial.print(pHumidity);
    Serial.print("\t");
    Serial.println();

    sampleError = false; // Successful read, reset the error flag
    return true; // Leave timer enabled.
}

// Celcius to Farenheit conversion funtion
int ctof(float x)
{
  return round(1.8 * x + 32);
}

// Verify caller is authorized
bool authRequest(IPAddress callerIP)
{
  for(int i=0;i<AUTHORIZED_HOSTS_QTY;i++)
  {
    if (callerIP == AUTHORIZED_HOSTS[i])
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
          if(memcmp(SNMP_READCOMMUNITY_LEN_6,payload+6,sizeof(SNMP_READCOMMUNITY_LEN_6)) == 0)
          {
            Serial.print("Read Community Length Matched");
            Serial.println();
            if(memcmp(SNMP_READCOMMUNITY_VALUE_7,payload+7,sizeof(SNMP_READCOMMUNITY_VALUE_7)-1) == 0)
            {
              Serial.print("Read Community Value Matched");
              Serial.println();
              if(memcmp(SNMP_GETREQUEST_7_LEN,payload+6+sizeof(SNMP_READCOMMUNITY_VALUE_7),sizeof(SNMP_GETREQUEST_7_LEN)) == 0)
              {
                Serial.print("Processing GetRequest...");
                Serial.println();
                
                // Copy portions of the caller's request info into buffers for us to then send back in the response.
                memcpy(SNMP_GETREQUEST_DATA0,payload,7);
                uint8_t RIDLength = payload[9+sizeof(SNMP_READCOMMUNITY_VALUE_7)]; // Retrieve the Request ID value length
                memcpy(SNMP_GETREQUEST_DATA2a,payload+9+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1,RIDLength+2); // Get the Request ID info
                memcpy(SNMP_GETREQUEST_DATA2b,payload+9+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength+2,6); // Get the Error info
                memcpy(SNMP_GETREQUEST_DATA3,payload+9+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength+2+6,6); // Get Varbind and Object info

                // We finished processing a valid uptime request packet.  Return 0 to the caller.
                if(memcmp(SNMP_GETUPTIME,payload+length-sizeof(SNMP_GETUPTIME),sizeof(SNMP_GETUPTIME)) == 0)
                {
                  return 0;
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
              }
            }
          }
        }
      }
    }
    blocking = false;
    return -1; // Unsupported/unknown request
  }
  blocking = false;
  return -1; // Currently blocked processing a request.  We'll ignore this one and wait for the following request to come in.
}

// Build and send response to valid getRequest
void sendGetResponse(int request, IPAddress caller, uint16_t port)
{
  // Check to make sure we have valid sample data and send it to the caller
  if(sampleError == false && pHumidity > 0 && pHumidity < 100 && fTemp >= 0 && fTemp < 128 && cTemp > -180 && cTemp < 533)
  {
    // Sensor data looks good
  }
  else
  {
    if(request > 1) // The request was for sensor data
    {
      // Add error to getResponse message
      SNMP_GETREQUEST_DATA2b[2] = 0x05;
    }
  }
  uint8_t RIDLength = SNMP_GETREQUEST_DATA2a[1];
  switch(request)
  {
    case 0 : // Return uptime
    {
      uint8_t dataType[2] = {0x43,0x04}; // Timetick, uint32
      uint32_t val = millis()/10; // Uptime (will wrap every 49 days)
      uint8_t value[4];
      value[0] = (val >> 24) & 0xFF;
      value[1] = (val >> 16) & 0xFF;
      value[2] = (val >> 8) & 0xFF;
      value[3] = val & 0xFF;
      uint8_t PDULen = 24 + RIDLength + 4; // getResponse PDU length plus 4 byte value
      SNMP_GETREQUEST_DATA1[1] = PDULen;
      SNMP_GETREQUEST_DATA3[1] = SNMP_GETREQUEST_DATA3[1] + 4; // Add four bytes to the varbind list to accommodate the 32 bit value being returned
      SNMP_GETREQUEST_DATA3[3] = SNMP_GETREQUEST_DATA3[3] + 4; // Add four bytes to the varbind item to accommodate the 32 bit value being returned
      uint8_t packetLen = (PDULen + 7 + sizeof(SNMP_READCOMMUNITY_VALUE_7) - 1); // -1 to remove the null SNMP_READCOMMUNITY_VALUE_7 terminator
      SNMP_GETREQUEST_DATA0[1] = packetLen;
      int responseBytes = (packetLen + 2); // +2 to add back the header bytes
      uint8_t responsePayload[responseBytes];
      memcpy(responsePayload,SNMP_GETREQUEST_DATA0,7);
      memcpy(responsePayload+7,SNMP_READCOMMUNITY_VALUE_7,sizeof(SNMP_READCOMMUNITY_VALUE_7)-1);
      memcpy(responsePayload+7+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1,SNMP_GETREQUEST_DATA1,2);
      memcpy(responsePayload+9+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1,SNMP_GETREQUEST_DATA2a,RIDLength+2); // Ger the Request ID info
      memcpy(responsePayload+9+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength+2,SNMP_GETREQUEST_DATA2b,6); // Get the Error info
      memcpy(responsePayload+17+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,SNMP_GETREQUEST_DATA3,6);
      memcpy(responsePayload+23+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,SNMP_GETUPTIME,8);
      memcpy(responsePayload+31+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,dataType,2);
      memcpy(responsePayload+33+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,value,4);
      udp.writeTo(responsePayload,responseBytes,caller,port);
      Serial.println();
      break;
    }
    case 1 : // Return hostname
    {
      uint8_t dataType[2] = {0x04,HOSTNAME_LEN}; // String, length of HOSTNAME
      uint8_t PDULen = 26 + RIDLength + HOSTNAME_LEN; // getResponse PDU length plus length of Request ID plus HOSTNAME
      SNMP_GETREQUEST_DATA1[1] = PDULen;
      SNMP_GETREQUEST_DATA3[1] = SNMP_GETREQUEST_DATA3[1] + HOSTNAME_LEN; // Add bytes to the varbind list to accommodate the hostname value being returned
      SNMP_GETREQUEST_DATA3[3] = SNMP_GETREQUEST_DATA3[3] + HOSTNAME_LEN; // Add bytes to the varbind item to accommodate the hostname value being returned
      uint8_t packetLen = (PDULen + 7 + sizeof(SNMP_READCOMMUNITY_VALUE_7) - 1); // -1 to remove the null SNMP_READCOMMUNITY_VALUE_7 terminator
      SNMP_GETREQUEST_DATA0[1] = packetLen;
      int responseBytes = (packetLen + 2); // +2 to add back the header bytes
      uint8_t responsePayload[responseBytes];
      memcpy(responsePayload,SNMP_GETREQUEST_DATA0,7);
      memcpy(responsePayload+7,SNMP_READCOMMUNITY_VALUE_7,sizeof(SNMP_READCOMMUNITY_VALUE_7)-1);
      memcpy(responsePayload+7+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1,SNMP_GETREQUEST_DATA1,2);
      memcpy(responsePayload+9+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1,SNMP_GETREQUEST_DATA2a,RIDLength+2); // Get the Request ID info
      memcpy(responsePayload+9+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength+2,SNMP_GETREQUEST_DATA2b,6); // Get the Error info
      memcpy(responsePayload+17+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,SNMP_GETREQUEST_DATA3,6);
      memcpy(responsePayload+23+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,SNMP_GETHOST,10);
      memcpy(responsePayload+33+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,dataType,2);
      memcpy(responsePayload+35+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,HOSTNAME,HOSTNAME_LEN);
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
      SNMP_GETREQUEST_DATA3[1] = SNMP_GETREQUEST_DATA3[1] + 2; // Add byte to the varbind list to accommodate the .1 degrees C value being returned
      SNMP_GETREQUEST_DATA3[3] = SNMP_GETREQUEST_DATA3[3] + 2; // Add byte to the varbind item to accommodate the .1 degrees C value being returned
      uint8_t packetLen = (PDULen + 7 + sizeof(SNMP_READCOMMUNITY_VALUE_7) - 1); // -1 to remove the null SNMP_READCOMMUNITY_VALUE_7 terminator
      SNMP_GETREQUEST_DATA0[1] = packetLen;
      int responseBytes = (packetLen + 2); // +2 to add back the header bytes
      uint8_t responsePayload[responseBytes];
      memcpy(responsePayload,SNMP_GETREQUEST_DATA0,7);
      memcpy(responsePayload+7,SNMP_READCOMMUNITY_VALUE_7,sizeof(SNMP_READCOMMUNITY_VALUE_7)-1);
      memcpy(responsePayload+7+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1,SNMP_GETREQUEST_DATA1,2);
      memcpy(responsePayload+9+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1,SNMP_GETREQUEST_DATA2a,RIDLength+2); // Ger the Request ID info
      memcpy(responsePayload+9+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength+2,SNMP_GETREQUEST_DATA2b,6); // Get the Error info
      memcpy(responsePayload+17+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,SNMP_GETREQUEST_DATA3,6);
      memcpy(responsePayload+23+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,SNMP_GETTEMPC,12);
      memcpy(responsePayload+35+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,dataType,2);
      memcpy(responsePayload+37+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,value,2);
      udp.writeTo(responsePayload,responseBytes,caller,port);
      Serial.println();
      break;
    }
    case 3 : // Return degrees F
    {
      uint8_t dataType[2] = {0x02,0x01}; // Integer, 1 byte
      uint8_t PDULen = 28 + RIDLength + 1; // getResponse PDU length plus one byte
      SNMP_GETREQUEST_DATA1[1] = PDULen;
      SNMP_GETREQUEST_DATA3[1] = SNMP_GETREQUEST_DATA3[1] + 1; // Add byte to the varbind list to accommodate the degrees F value being returned
      SNMP_GETREQUEST_DATA3[3] = SNMP_GETREQUEST_DATA3[3] + 1; // Add byte to the varbind item to accommodate the degrees F value being returned
      uint8_t packetLen = (PDULen + 7 + sizeof(SNMP_READCOMMUNITY_VALUE_7) - 1); // -1 to remove the null SNMP_READCOMMUNITY_VALUE_7 terminator
      SNMP_GETREQUEST_DATA0[1] = packetLen;
      int responseBytes = (packetLen + 2); // +2 to add back the header bytes
      uint8_t responsePayload[responseBytes];
      memcpy(responsePayload,SNMP_GETREQUEST_DATA0,7);
      memcpy(responsePayload+7,SNMP_READCOMMUNITY_VALUE_7,sizeof(SNMP_READCOMMUNITY_VALUE_7)-1);
      memcpy(responsePayload+7+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1,SNMP_GETREQUEST_DATA1,2);
      memcpy(responsePayload+9+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1,SNMP_GETREQUEST_DATA2a,RIDLength+2); // Ger the Request ID info
      memcpy(responsePayload+9+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength+2,SNMP_GETREQUEST_DATA2b,6); // Get the Error info
      memcpy(responsePayload+17+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,SNMP_GETREQUEST_DATA3,6);
      memcpy(responsePayload+23+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,SNMP_GETTEMPF,12);
      memcpy(responsePayload+35+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,dataType,2);
      responsePayload[37+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength] = fTemp;
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
      uint8_t packetLen = (PDULen + 7 + sizeof(SNMP_READCOMMUNITY_VALUE_7) - 1); // -1 to remove the null SNMP_READCOMMUNITY_VALUE_7 terminator
      SNMP_GETREQUEST_DATA0[1] = packetLen;
      int responseBytes = (packetLen + 2); // +2 to add back the header bytes
      uint8_t responsePayload[responseBytes];
      memcpy(responsePayload,SNMP_GETREQUEST_DATA0,7);
      memcpy(responsePayload+7,SNMP_READCOMMUNITY_VALUE_7,sizeof(SNMP_READCOMMUNITY_VALUE_7)-1);
      memcpy(responsePayload+7+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1,SNMP_GETREQUEST_DATA1,2);
      memcpy(responsePayload+9+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1,SNMP_GETREQUEST_DATA2a,RIDLength+2); // Ger the Request ID info
      memcpy(responsePayload+9+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength+2,SNMP_GETREQUEST_DATA2b,6); // Get the Error info
      memcpy(responsePayload+17+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,SNMP_GETREQUEST_DATA3,6);
      memcpy(responsePayload+23+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,SNMP_GETHUMIDITY,12);
      memcpy(responsePayload+35+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength,dataType,2);
      responsePayload[37+sizeof(SNMP_READCOMMUNITY_VALUE_7)-1+RIDLength] = pHumidity;
      udp.writeTo(responsePayload,responseBytes,caller,port);
      Serial.println();
      break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
      delay(100);
  }
  // Initialize the temp & humidity sensor
  Wire.begin(16,17);
  sensor.begin(Wire, SHT40_I2C_ADDR_44);

  // Verify the sensor is connected and healthy
  sensor.softReset();
  delay(1000);
  uint32_t serialNumber = 0;
  error = sensor.serialNumber(serialNumber);
  if (error != NO_ERROR) {
      Serial.print("Error trying to execute serialNumber(): ");
      errorToString(error, errorMessage, sizeof errorMessage);
      Serial.println(errorMessage);
      sampleError = true;
  }
  // Make an initial sensor reading while waiting for Ethernet port to negotiate and initialize
  timer.in(1000, sample);

  // Initialize Ethernet
  ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_TYPE);
  ETH.config(ip, gateway, subnet);
  ETH.linkUp();

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
        if(authRequest(packet.remoteIP()) && packet.length() < 52 && packet.length() > 43)
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
              break;
            }
            case 1 : // Return hostname
              Serial.print("Sending hostname.");
              sendGetResponse(1,packet.remoteIP(),packet.remotePort());
              blocking = false;
            break;
            case 2 : // Return .1 degrees C
              Serial.print("Sending .1 degrees C.");
              sendGetResponse(2,packet.remoteIP(),packet.remotePort());
              blocking = false;
            break;
            case 3 : // Return degrees F
              Serial.print("Sending degrees F.");
              sendGetResponse(3,packet.remoteIP(),packet.remotePort());
              blocking = false;
            break;
            case 4 : // Return humidity
              Serial.print("Sending humidity.");
              sendGetResponse(4,packet.remoteIP(),packet.remotePort());
              blocking = false;
            break;
            default : // Return error
              Serial.print("-1 returned from parser.  Ignore caller.");
            break;
          }
        }
      }
    });
  }
}

void loop() {
  timer.tick(); // Temp & humidity sampling
}
