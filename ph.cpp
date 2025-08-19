


// this for an arduino r4 wifi to read ph levels and display them on the led matrix aswell as a web interface
// but this is shit and does not read consistently


#include "WiFiS3.h"    //  wifi stuff
#include <WiFiUdp.h>
#include <ArduinoMDNS.h>

#include "ph_html.h"

int status = WL_IDLE_STATUS;
WiFiUDP udp;
MDNS mdns(udp);
WiFiServer server(80);

uint8_t AUTOmode = 0;
float ph = 0.0;    //  set in mess()
float mean = 0.0;    //  set in mess()
float slope = 0.0;    //  set in calibrate()
float intercept = 0.0;    //  set in calibrate()
uint16_t* messvals = nullptr;    //  Global pointer for dynamically allocated array


#include <EEPROM.h>    //  eeprom is non voletile so saved on powerloss and reset
struct{ char ssid[30]; char pw[30]; float upperphref; float lowerphref; uint16_t upperanalogref; uint16_t loweranalogref; float phoffset; } eeprom;    //  fixed size for calibartion pairs since i dont know malloc


void calibrate(float upperref = 0.0, float lowerref = 0.0 ) {    //  overwrites referenc values and recalculates linear interpolation
  if (upperref) { eeprom.upperphref = upperref; eeprom.upperanalogref = mean; }    //  update upper reference
  if (lowerref) { eeprom.lowerphref = lowerref; eeprom.loweranalogref = mean; }    // update lower reference
  slope = (eeprom.upperphref - eeprom.lowerphref) / (float)(eeprom.upperanalogref - eeprom.loweranalogref);
  intercept = (float)eeprom.upperphref - slope * eeprom.upperanalogref;
  Serial.println("success calibrated slope " + String(slope, 10) + ", intercept " + String(intercept));    //  echo calibration DEBUG
}


void parseserial(String str) {    //  for user to overwrite eeprom struct
  str.trim();
  if (str.startsWith("ssid "))       { String ssidStr       = str.substring(5);  ssidStr.toCharArray(eeprom.ssid, sizeof(eeprom.ssid)); }
  if (str.startsWith("pw "))         { String pwStr         = str.substring(3);  pwStr.toCharArray(eeprom.pw, sizeof(eeprom.pw)); }
  if (str.startsWith("upperphref ")) { String upperphrefStr = str.substring(11); calibrate( upperphrefStr.toFloat(), 0.0 ); }
  if (str.startsWith("lowerphref ")) { String lowerphrefStr = str.substring(11); calibrate( 0.0, lowerphrefStr.toFloat() ); }
  if (str.startsWith("phoffset "))   { String phoffsetStr   = str.substring(9);  eeprom.phoffset = phoffsetStr.toFloat(); }
  EEPROM.put(0, eeprom);    //  put updated values into eeprom
  Serial.println("eeprom vals, " + String(eeprom.ssid) + ", " + String(eeprom.upperanalogref) + ", " + String(eeprom.upperphref) + ", " + String(eeprom.loweranalogref) + ", " + String(eeprom.lowerphref));    //  echo eeprom DEBUG
}


void mess() {    //  messure analog voltage and convert to ph with continous mean, 100 is the count of messvals used for mean calculation
  static uint16_t index = 0;
  static uint16_t messvals[100] = {};    // switch to fixed size array to simplify
  messvals[index] = analogRead(A0);    //  read one value into messvals
  index = ++index%100;    //  increment index of messvals or reset index to 0
  mean = 0.0;    //  reset mean of readings and recalculate mean
  for (int i=0; i<100; i++) mean += messvals[i];
  mean /= (float)100;
  ph = (slope * mean ) + intercept + eeprom.phoffset;    //  convert mean of analog readings to ph
}


void setup() {
  Serial.begin(115200);    //  Serial.setTimeout(10);
  EEPROM.get(0, eeprom);    //  fetch current eeprom values and since eeprom is a global struct this updates the global struct
  
  if (WiFi.status() == WL_NO_MODULE) {      //  check for the WiFi module and verison
    Serial.println("Communication with WiFi module failed!");
    while (true);    // don't continue
  }
  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  uint32_t lastloop = 0;    //  attempt to connect to wifi
  while (status != WL_CONNECTED) {    //  try wifi max 4 times
    if (millis() > lastloop + 10000) {    //  retry wifi every 10sec
      lastloop = millis();
      Serial.println("try ssid " + String(eeprom.ssid));
      status = WiFi.begin(eeprom.ssid, eeprom.pw);    //  Connect to WPA/WPA2 network. Change this line if using open or WEP network
    }
    if (millis() > lastloop + 10000) Serial.print(".");
    if (Serial.available() > 0) parseserial(Serial.readString());    //  let user change eeprom via serial while waiting for wifi
  }

  server.begin();
  
  
  analogReadResolution(14); // 10bit 1023, 12bit 4096, 14bit 16383
  pinMode(A0, INPUT);    //  A0 is ph value pin
  calibrate();    //  calibrates with eeprom values here to give wifi some time to start this caluculate linear interpolation of reference values from eeprom

  mdns.begin(WiFi.localIP(), "ph");    //  setup mdns for ph.local
  mdns.addServiceRecord("Arduino mDNS ph Webserver._http", 80, MDNSServiceTCP);

  Serial.println("success ssid " + String(WiFi.SSID()) + ", ip " + WiFi.localIP().toString() + " or ph.local, rssi " + String(WiFi.RSSI()) + "dBm");    //  confirm wifi DEBUG
}

void loop() {
  mdns.run();

  if (Serial.available() > 0) parseserial(Serial.readString());    //  let user change eeprom via serial for debugging

  mess();    //  update ph every loop

  WiFiClient client = server.available();  // listen for incoming clients
  if (client) {
    String currentLine = "";                // make a String to hold incoming data from the client
    //Serial.println("new client");
    //boolean currentLineIsBlank = true;  // an HTTP request ends with a blank line
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);  // if you've gotten to the end of the line (received a newline character) and the line is blank, the HTTP request has ended, so you can send a reply
        if (c == '\n') {
          if (currentLine.length() == 0) {
            //if (c == '\n' && currentLineIsBlank) {
            client.print(ph_html);    // serve html
            break;
          } else {    // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }

        if (currentLine.endsWith("GET /AUTOmode")) {    //  aswer with current mode
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/plain");
          client.println("Connection: close");
          client.println();
          client.print(AUTOmode);
          break;
        }

        if (currentLine.endsWith("GET /AUTOstats")) {    //  aswer with json for AUTOstats
          float ph = 11.1;
          float tankL = 2.22;
          float Automl = 44.4;
          uint8_t pumpactive = 0;
          String payload =  String("{\"ph\": ")         + String(ph, 2)
                         + String(", \"tankL\": ")      + String(tankL, 2)
                         + String(", \"ml\": ")         + String(Automl, 2)
                         + String(", \"pumpactive\": ") + String(pumpactive)
                         + String("}");
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: application/json");
          client.print("Content-Length: ");
          client.println(payload.length());
          client.println("Connection: close");
          client.println();
          client.print(payload);
          break;
        }

        if (currentLine.endsWith("GET /MANUALstats")) {    //  aswer with json for MANUALstats
          float ph = 11.1;
          float tankL = 2.22;
          float Manualml = 44.4;
          uint8_t pumpactive = 0;
          String payload =  String("{\"ph\": ")         + String(ph, 2)
                         + String(", \"tankL\": ")      + String(tankL, 2)
                         + String(", \"ml\": ")         + String(Manualml, 2)
                         + String(", \"pumpactive\": ") + String(pumpactive)
                         + String("}");
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: application/json");
          client.print("Content-Length: ");
          client.println(payload.length());
          client.println("Connection: close");
          client.println();
          client.print(payload);
          break;
        }

      }
    }
    //delay(1); // give the web browser time to receive the data
    client.stop();
    Serial.println(" client disconnected");
    Serial.println("");
  }
}