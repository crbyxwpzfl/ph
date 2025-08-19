


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

float ph = 0.0;    //  set in mess()
float Manualml = 0.0;

float mean = 0.0;    //  set in mess()
float slope = 0.0;    //  set in calibrate()
float intercept = 0.0;    //  set in calibrate()
uint16_t* messvals = nullptr;    //  Global pointer for dynamically allocated array


#include <EEPROM.h>    //  eeprom is non voletile so saved on powerloss and reset
struct{ char ssid[30];
        char pw[30];
        uint8_t AUTOmode;    // is auto mode on or off
        float tankL;    //  tank level in liters
        float Autophsetpoint;    //  ph set point
        float Automl;    //  amount in ml to pump for 0.1 ph diff
        float Autodeadzone;    //  minimal ph diff to pump sth
        uint16_t speed;    //  motor speed
        float pumpref;    //  pump reference
        float upperphref;    // upper ph calibaration point
        float lowerphref;    // lower ph calibaration point
        uint16_t upperanalogref;    // corosponding analog reading
        uint16_t loweranalogref;    // corosponding analog reading
        float phoffset;    //  constant ph offset
      } eeprom;

String calibrateph(float upperref = 0.0, float lowerref = 0.0 ) {    //  overwrites referenc values and recalculates linear interpolation
  if (upperref) { eeprom.upperphref = upperref; eeprom.upperanalogref = mean; }    //  update upper reference
  if (lowerref) { eeprom.lowerphref = lowerref; eeprom.loweranalogref = mean; }    // update lower reference
  slope = (eeprom.upperphref - eeprom.lowerphref) / (float)(eeprom.upperanalogref - eeprom.loweranalogref);
  intercept = (float)eeprom.upperphref - slope * eeprom.upperanalogref;
  return("ph calibration " + String(slope, 10) + ", intercept " + String(intercept));    //  echo calibration DEBUG
}

void calibratepump(){

}

void parseserial(String str) {    //  for user to overwrite eeprom struct
  str.trim();
  if (str.startsWith("ssid "))       { String ssidStr       = str.substring(5);  ssidStr.toCharArray(eeprom.ssid, sizeof(eeprom.ssid)); }
  if (str.startsWith("pw "))         { String pwStr         = str.substring(3);  pwStr.toCharArray(eeprom.pw, sizeof(eeprom.pw)); }
  if (str.startsWith("upperphref ")) { String upperphrefStr = str.substring(11); calibrateph( upperphrefStr.toFloat(), 0.0 ); }
  if (str.startsWith("lowerphref ")) { String lowerphrefStr = str.substring(11); calibrateph( 0.0, lowerphrefStr.toFloat() ); }
  if (str.startsWith("phoffset "))   { String phoffsetStr   = str.substring(9);  eeprom.phoffset = phoffsetStr.toFloat(); }
  if (str.startsWith("AUTOmode "))   { String AUTOmodeStr   = str.substring(9);  eeprom.AUTOmode = AUTOmodeStr.toInt(); }
  if (str.startsWith("tankL "))      { String tankLStr      = str.substring(6);  eeprom.tankL = tankLStr.toFloat(); }
  if (str.startsWith("Autophsetpoint ")) { String AutophsetpointStr = str.substring(11); eeprom.Autophsetpoint = AutophsetpointStr.toFloat(); }
  if (str.startsWith("Automl "))     { String AutomlStr     = str.substring(8);  eeprom.Automl = AutomlStr.toFloat(); }
  if (str.startsWith("Autodeadzone ")) { String AutodeadzoneStr = str.substring(13); eeprom.Autodeadzone = AutodeadzoneStr.toFloat(); }
  if (str.startsWith("speed "))      { String speedStr      = str.substring(6);  eeprom.speed = speedStr.toInt(); }  //  TODO this should start pump calibrati
  if (str.startsWith("pumpref "))    { String pumprefStr    = str.substring(8);  eeprom.pumpref = pumprefStr.toFloat(); }
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
    Serial.println("communication with wifi module failed so stop here");
    while (true);    // don't continue
  }
  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("please upgrade the firmware");
  }

  Serial.println("init wifi ");
  uint32_t lastwifitry = 0, lastdot = 0;    //  attempt to connect to wifi
  while (status != WL_CONNECTED) {    //  try wifi max 4 times
    if (millis() > lastwifitry + 10000) {    //  retry wifi every 10sec
      lastwifitry = millis();
      Serial.print("try ssid " + String(eeprom.ssid));
      status = WiFi.begin(eeprom.ssid, eeprom.pw);    //  Connect to WPA/WPA2 network. Change this line if using open or WEP network
    }
    if (millis() > lastdot + 1000) Serial.print(".");
    if (Serial.available() > 0) parseserial(Serial.readString());    //  let user change eeprom via serial while waiting for wifi
  }
  Serial.println("success" + "rssi " + String(WiFi.RSSI()) + "dBm");

  server.begin();
  Serial.println("webserver is up");


  analogReadResolution(14); // 10bit 1023, 12bit 4096, 14bit 16383
  pinMode(A0, INPUT);    //  A0 is ph value pin
  Serial.println(calibrateph());    //  calibrates with eeprom values here to give wifi some time to start this caluculate linear interpolation of reference values from eeprom


  mdns.begin(WiFi.localIP(), "ph");    //  setup mdns for ph.local
  mdns.addServiceRecord("arduino mDNS ph webserver._http", 80, MDNSServiceTCP);
  Serial.println("mdns is up so ph.local  goes to " + WiFi.localIP().toString());    //  confirm wifi DEBUG
}

void loop() {
  mdns.run();

  if (Serial.available() > 0) parseserial(Serial.readString());    //  let user change eeprom via serial for debugging

  mess();    //  update ph every loop

  WiFiClient client = server.available();  // listen for incoming clients
  if (client) {
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);  // if you've gotten to the end of the line (received a newline character) and the line is blank, the HTTP request has ended, so you can send a reply

        if (c == '\n') {    //  end of line so check what client wants the following are all GETs since these are not saved in back history

          if (currentLine.startsWith("GET /AUTOmode")) {    //  aswer with current mode
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            client.print(AUTOmode);
            break;
          }

          if (currentLine.startsWith("GET /pumpactive")) {    //  should only be hit while in Manual activate pump until Manual ml is zero
            Serial.println(" GET /pumpactive ");
            break;
          }

          if (currentLine.startsWith("GET /pumpinactive")) {    //  should only be hit while in Manual stop pump and freeze Manual ml
            Serial.println(" GET /pumpinactive ");
            break;
          }

          if (currentLine.startsWith("GET /AUTOstats")) {    //  aswer with json for AUTOstats also set current mode to Auto
            float ph = 11.1;
            float tankL = 2.22;
            float Automl = 44.4;
            uint8_t pumpactive = 0;
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: application/json");
            client.println("Connection: close");
            client.println();
            client.print("{\"ph\": ");          client.print(ph,2);
            client.print(", \"tankL\": ");      client.print(tankL,2);
            client.print(", \"ml\": ");         client.print(Automl,2);
            client.print(", \"pumpactive\": "); client.print(pumpactive);
            client.print("}");
            break;
          }

          if (currentLine.startsWith("GET /MANUALstats")) {    //  aswer with json for MANUALstats also set current mode to Manual and stop pump
            float ph = 11.1;
            float tankL = 2.22;
            float Manualml = 44.4;
            uint8_t pumpactive = 0;
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: application/json");
            client.println("Connection: close");
            client.println();
            client.print("{\"ph\": ");          client.print(ph,2);
            client.print(", \"tankL\": ");      client.print(tankL,2);
            client.print(", \"ml\": ");         client.print(Manualml,2);
            client.print(", \"pumpactive\": "); client.print(pumpactive);
            client.print("}");
            break;
          }

          if (currentLine.startsWith("GET /ow?")) {
            String query = currentLine.substring(8, currentLine.indexOf(' ', 8));    //  the query is everything after 8 chars to the second space
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            client.println("recieved overwrite " + query);
            if (query.startsWith("speed")) Serial.println("speed overwrite");
            break;
          }

          client.print(ph_html);    //  serve root for all cases where we did not break early
          break;

        }

        if (c != '\r') {  // do not add \n or \r to currentLine string
          currentLine += c;      // everything else add it to the end of the currentLine
        }

      }
    }
    //delay(1); // give the web browser time to receive the data
    client.stop();
    Serial.println(" client disconnected");
    Serial.println("");
  }
}