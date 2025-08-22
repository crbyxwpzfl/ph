


// this for an arduino r4 wifi to read ph levels and display them on the led matrix aswell as a web interface
// but this is shit and does not read consistently


#include "WiFiS3.h"    //  wifi stuff
#include <WiFiUdp.h>
#include <ArduinoMDNS.h>
#include <TMC2209.h>

#include "ph_html.h"

WiFiUDP udp;
MDNS mdns(udp);
WiFiServer server(80);


UART & serial_stream = Serial1;    //  uart comms for pump
TMC2209 stepper_driver;    // instantiate TMC2209 for the pump


#include <EEPROM.h>    //  eeprom is non voletile so saved on powerloss and reset
struct{ char ssid[30];
        char pw[30];

        float tankL;    //  tank level in liters

        uint8_t Auto;    // is auto mode on or off
        uint32_t Autointervalms;    //  interval for auto mode cycles
        float Autodeadzone;    //  minimal ph diff to pump sth
        float Autophsetpoint;    //  ph set point
        float Automl;    //  amount in ml to pump for 0.1 ph diff
        float Automaxml;    //  max ml for one pump cycle
        float Manualml;    //  this is not neccessary to save but it is easier to just have this in global struct aswell

        uint16_t pumpspeed;    //  motor speed
        float pumpmsperml;    //  pump calibration value

        float phoffset;    //  constant ph offset
        float slope;    //  slope of the calibration curve
        float intercept;    //  intercept of the calibration curve

      } eeprom;


static inline bool msSince(uint32_t lasttime, uint32_t intervalms) {    //  helper to do wrapsave time intervals
  return (uint32_t)(millis() - lasttime) >= intervalms;
}


String initpump() {
  uint16_t Vm = analogRead(A3);
  String response = String("Vm is " + String(Vm));

  if (Vm > 5000) {
    digitalWrite(4, HIGH);  // set Vio high
    response += " so init pump";

    Serial1.begin(115200);  // Start Serial1 on pins 0 (RX) and 1 (TX)
    stepper_driver.setup(serial_stream);

    stepper_driver.setHardwareEnablePin(6);
    stepper_driver.setRunCurrent(90);    //  perhaps this should be in eeprom too
    stepper_driver.enableCoolStep();
    stepper_driver.moveAtVelocity(0); // stop stepper
    stepper_driver.disable();

    response += " try comms";
  } else {
    return response + " so abort";    //  exit early
  }

  if (stepper_driver.isSetupAndCommunicating()) return response + " and comms good hw " + (stepper_driver.hardwareDisabled() ? "enabled\n" : "disabled\n");
  if (stepper_driver.isCommunicatingButNotSetup()) return response + " and comms good but no setup\n";
  return response + " and no comms \n";
}


uint32_t pumpml(float ml = -69.0){
  static uint32_t pumpactivation = 0;    //  timestamp when pump was activated
  static uint32_t totalpumpms = 0;    //  total time to pump in ms
  static uint32_t lastpumpcall = 0;    //  timestamp of last pump call this is for recalculation of Manualml and tank level each call

  if (stepper_driver.isSetupAndCommunicating()) {    //  check for pump setup and comms  perhpahs integrate stallguard here and add standstill detection
    bool hardware_disabled = stepper_driver.hardwareDisabled();

    if (ml > 99.9) {    //  consider a value of great than this as a fault reconsider this perhaps this is stoopid since this can hardlock for a big ph diff

      stepper_driver.moveAtVelocity(0);    // stop pump
      stepper_driver.disable();    // disable pump

      return 0;    //  return pump off
    }

    if (ml < 0.0) {    //  this should only occur for periodical call so check current time against total pump time and perhaps stop pump only when pumping otherwise the msSince perhaps causes problemi
      if (hardware_disabled) return 0;    //  when pump off exit early nothing to do here
      if (msSince(pumpactivation, totalpumpms)) {    //  when pump time is up and pump is active stop pump and save Manualml and tank level

        stepper_driver.moveAtVelocity(0);    // stop pump
        stepper_driver.disable();    // disable pump

        eeprom.Manualml = 0.0;    //  reset Manualml
        EEPROM.put(0, eeprom);    //  save current tank level here
        return 0;    //  return pump off
      }
      if (!msSince(pumpactivation, totalpumpms)) {    //  when pump time is not up and pump is active recalc Manualml and tank level
        uint32_t dt = (uint32_t)(millis() - lastpumpcall);    //  roleover safe calc elapsed ms
        if (!eeprom.Auto) eeprom.Manualml = eeprom.Manualml - ( (float)(dt) / eeprom.pumpmsperml );    //  recalculate Manualml substract time since last call over msperml       // TODO consider negative values here
        eeprom.tankL = eeprom.tankL -  ( ( (float)(dt) / eeprom.pumpmsperml ) * 0.001 );    //  recalculate tank level substract time since last call over msperml
        lastpumpcall = millis();
        return 1;    //  return pumping
      }
    }

    if (!ml) {    //  when called with zero this should stop pump and freeze Manualml

      stepper_driver.moveAtVelocity(0);    // stop pump
      stepper_driver.disable();    // disable pump

      EEPROM.put(0, eeprom);    //  save current tank level here
      return 0; //  return pump off
    }

    if (ml) {    //  when called with a value this calcs the pump time and activates the pump
      if (eeprom.Auto && ml > eeprom.Automaxml) ml = eeprom.Automaxml;    //  cap ml while in Auto

      pumpactivation = lastpumpcall = millis();    //  save activation time stamp
      totalpumpms = ml * eeprom.pumpmsperml;    //  calculate total pump time with pumpmsperml

      stepper_driver.enable();
      stepper_driver.moveAtVelocity(eeprom.pumpspeed);

      return totalpumpms;    //  return pump on
    }
  }

  if (stepper_driver.isCommunicatingButNotSetup()) {    //  retry initialization here
    Serial.println("no setup but comms good. try init again");
    initpump();
    return 0;
  }

  //Serial.println("no comms with pump");    //  this is bad this likly indicates wirering issue
  return 0;
}


String calibratepump(float value) {
  static uint8_t firstcall = 1;
  static uint32_t totalpumpms = 0; 

  if (firstcall) {    // on first call treat value as speed setting
    eeprom.pumpspeed = (uint32_t)value;    //  pump speed good values are somewhere around 5000 to 40000

    totalpumpms = pumpml(20.0);    //  pump 20ml and safe total pump time perhaps make this a setting in eeprom too
    
    firstcall = 0;    //  next call is not for speed
    return ("pump will calibrate to " + String(eeprom.pumpspeed) + " pls wait for pump to stop then enter ml \n");    //  tell user to wait while pump is running
  }

  if (!firstcall) {    //  TODO check for pump still running here!!!!
    eeprom.pumpmsperml = (float)totalpumpms / value ;    //  calculate ms per ml
    firstcall = 1;    //  prep for next calibration
    EEPROM.put(0, eeprom);    //  put calculated ml per ms and corresponding speed into eeprom
    return ("pump calibrated to " + String(eeprom.pumpmsperml) + " ml/ms \n");
  }
  return ("pump calibration error \n");    //  this is to please compiler
}


float mess(bool returnanalogvalue = false) {    //  messure analog voltage and convert to ph with continous mean, 100 is the count of messvals used for mean calculation
  static uint16_t index = 0;
  static uint16_t messvals[100] = {};    // switch to fixed size array to simplify
  float analogmean = 0.0;    //  reset mean of readings and recalculate mean

  messvals[index] = analogRead(A0);    //  read one value into messvals
  index = (index + 1) % 100;    //  increment index of messvals or reset index to 0

  for (int i=0; i<100; i++) analogmean += messvals[i];
  analogmean /= (float)100;
  float ph = (eeprom.slope * analogmean ) + eeprom.intercept + eeprom.phoffset;    //  convert mean of analog readings to ph
  return returnanalogvalue ? analogmean : ph;
}


String calibrateph(float trueph) {    //  overwrites referenc values and recalculates linear interpolation
  static float truepharr[2] = {0.0 , 0.0};
  static float analogarr[2] = {0.0 , 0.0};
  static uint8_t index = 0;

  truepharr[index] = trueph;
  analogarr[index] = mess(true);    //  mess true returns the analog value
  index = (index + 1) % 2;

  if (truepharr[0] != 0.0 && truepharr[1] != 0.0) {    //  when there are two fix points avalible calculate slope and intercept
    eeprom.slope = (truepharr[0] - truepharr[1]) / (float)(analogarr[0] - analogarr[1]);
    eeprom.intercept = (float)truepharr[0] - eeprom.slope * analogarr[0];
  } else {
    return ("first pair is " + String(truepharr[0]) + ", " + String(analogarr[0]) + " pls enter a second value \n");
  }

  EEPROM.put(0, eeprom);    //  put calculated intercept and slope into eeprom
  return ("ph calibrated to " + String(eeprom.slope, 10) + ", intercept " + String(eeprom.intercept) + "\n");    //  echo calibration DEBUG
}


String parseserial(String query) {    //  for user to overwrite eeprom struct
  String response = String("query " + query + "\n");
  query.trim();
  
  if (query.startsWith("ssid "))  query.substring(5).toCharArray(eeprom.ssid, sizeof(eeprom.ssid));
  if (query.startsWith("pw "))    query.substring(3).toCharArray(eeprom.pw, sizeof(eeprom.pw));

  if (query.startsWith("tankL ")) eeprom.tankL = query.substring(6).toFloat();

  if (query.startsWith("Auto "))           eeprom.Auto = query.substring(5).toInt();
  if (query.startsWith("Autointervalms ")) eeprom.Autointervalms = query.substring(15).toInt();
  if (query.startsWith("Autodeadzone "))   eeprom.Autodeadzone = query.substring(13).toFloat();
  if (query.startsWith("Autophsetpoint ")) eeprom.Autophsetpoint = query.substring(15).toFloat();
  if (query.startsWith("Automl "))         eeprom.Automl = query.substring(7).toFloat();
  if (query.startsWith("Automaxml "))      eeprom.Automaxml = query.substring(10).toFloat();
  if (query.startsWith("Manualml "))       eeprom.Manualml = query.substring(9).toFloat();

  if (query.startsWith("calibratepump "))  response += calibratepump(query.substring(14).toFloat());    //  this expects first a speed value eg. 5000 to 40000 and then a ml value eg. 12.34
  if (query.startsWith("pumpspeed "))      eeprom.pumpspeed = query.substring(10).toFloat();
  if (query.startsWith("pumpmsperml "))    eeprom.pumpmsperml = query.substring(12).toFloat();

  if (query.startsWith("phoffset "))     eeprom.phoffset = query.substring(9).toFloat();
  if (query.startsWith("calibrateph "))  response += calibrateph(query.substring(12).toFloat());    //  this expects two true ph value eg. 4.0 and 7.0

  if (query.startsWith("initpump "))  response += initpump();
  if (query.startsWith("pumpml "))    pumpml(query.substring(7).toFloat());    //  this expects a ml value eg. 12.34

  EEPROM.put(0, eeprom);    //  put new values into eeprom

  response += String( "\nph " + String(mess()) + "\n"
                + "analog " + String(mess(true)) + "\n"
                + "------ eeprom vals ------\n"
                + "ssid " + String(eeprom.ssid) + "\n"
            //  + "pw" + String(eeprom.pw) + "\n"

                + "tankL " + String(eeprom.tankL) + "\n"

                + "Auto " + String(eeprom.Auto) + "\n"
                + "Autointervalms " + String(eeprom.Autointervalms) + "\n"
                + "Autodeadzone " + String(eeprom.Autodeadzone) + "\n"
                + "Autophsetpoint " + String(eeprom.Autophsetpoint) + "\n"
                + "Automl " + String(eeprom.Automl) + "\n"
                + "Automaxml " + String(eeprom.Automaxml) + "\n"
                + "Manualml " + String(eeprom.Manualml) + "\n"

                + "pumpspeed " + String(eeprom.pumpspeed) + "\n"
                + "pumpmsperml " + String(eeprom.pumpmsperml, 4) + "\n"

                + "phoffset " + String(eeprom.phoffset) + "\n"
                + "slope " + String(eeprom.slope, 10) + "\n"
                + "intercept " + String(eeprom.intercept, 10) + "\n"

                + "------ pump info ------\n"
                + "pump setup and comms " + (stepper_driver.isSetupAndCommunicating() ? "good" : "bad") + "\n"
                + "pump just comms " + (stepper_driver.isCommunicatingButNotSetup() ? "good" : "bad") + "\n"
                + "pump hardware " + (stepper_driver.hardwareDisabled() ? "disabled" : "enabled") + "\n"
                );    //  echo eeprom for DEBUG
  return response;
}


void initwifi() {
  int status = WL_IDLE_STATUS;

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
    if (msSince(lastwifitry, 10000)) {
      lastwifitry = millis(); Serial.print("try ssid " + String(eeprom.ssid));
      status = WiFi.begin(eeprom.ssid, eeprom.pw);    //  Connect to WPA/WPA2 network. Change this line if using open or WEP network
    }
    if (msSince(lastdot, 1000)) {
      lastdot = millis(); Serial.print(".");
    }
    if (Serial.available() > 0) Serial.println( parseserial(Serial.readString()) );    //  let user change eeprom via serial while waiting for wifi
  }
  Serial.println("success rssi " + String(WiFi.RSSI()) + "dBm");

  server.begin();
  Serial.println("webserver is up");
  
  mdns.begin(WiFi.localIP(), "ph");    //  setup mdns for ph.local
  mdns.addServiceRecord("arduino mDNS ph webserver._http", 80, MDNSServiceTCP);
  Serial.println("mdns is up so ph.local  goes to " + WiFi.localIP().toString());    //  confirm wifi DEBUG
}


void setup() {
  pinMode(4, OUTPUT);  // Vio pin
  digitalWrite(4, LOW);  // set Vio Low do not power Vio without Vm

  analogReadResolution(14); // 10bit 1023, 12bit 4096, 14bit 16383
  pinMode(A3, INPUT);    //  A3 is to see if there is Vm
  pinMode(A0, INPUT);    //  A0 is ph value pin

  Serial.begin(115200);    //  Serial.setTimeout(10);
  EEPROM.get(0, eeprom);    //  fetch current eeprom values and since eeprom is a global struct this updates the global struct

  initwifi();

  Serial.println( initpump() );
}

void loop() {
  if (Serial.available() > 0) Serial.println( parseserial(Serial.readString()) );    //  let user change eeprom via serial for debugging

  mdns.run();    //  periodically call mdns

  mess();    //  measure ph every loop

  pumpml();    //  periodically call pump

  static uint32_t lastAutopump = 0;    //  timestamp of last auto pump
  if (eeprom.Auto && msSince(lastAutopump, eeprom.Autointervalms)) {    //  when in auto and its time for auto pump
    float phdiff = (float)(mess() - eeprom.Autophsetpoint);    //  calculate ph diff
    if (phdiff > eeprom.Autodeadzone) {    //  only pump when ph diff is greater than the deadzone
        lastAutopump = millis();
        pumpml( (float)( phdiff * eeprom.Automl * 10.0f ) );    //  calculate ml to pump phdiff times Automl per 0.1ph diff
    }
  }

  WiFiClient client = server.available();  // listen for incoming clients
  if (client) {
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);  // if you've gotten to the end of the line (received a newline character) and the line is blank, the HTTP request has ended, so you can send a reply

        if (c == '\n') {    //  end of line so check what client wants the following are all GETs since these are not saved in back history

          if (currentLine.startsWith("GET /mode")) {    //  aswer with current mode
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            client.print(String(eeprom.Auto));
            break;
          }

          if (currentLine.startsWith("GET /phsetpoint?value=")) {    //  overwrite ph setpoint here should only be hit with non zero values
            eeprom.Autophsetpoint = currentLine.substring(22, currentLine.indexOf(' ', 22)).toFloat();
            EEPROM.put(0, eeprom);    //  save new value
            break;
          }

          if (currentLine.startsWith("GET /tankL?value=")) {    //  overwrite tank level here should only be hit with non zero values
            eeprom.tankL = currentLine.substring(17, currentLine.indexOf(' ', 17)).toFloat();
            EEPROM.put(0, eeprom);    //  save new value
            break;
          }

          if (currentLine.startsWith("GET /Automl?value=")) {    //  should only be hit while in Auto should only be hit with non zero values
            eeprom.Automl = currentLine.substring(18, currentLine.indexOf(' ', 18)).toFloat();
            EEPROM.put(0, eeprom);    //  save new value
            break;
          }

          if (currentLine.startsWith("GET /Manualml?value=")) {    //  should only be hit while in Manual should only be hit with non zero values
            eeprom.Manualml = currentLine.substring(20, currentLine.indexOf(' ', 20)).toFloat();    //  does not have to be saved Manualml is just for convenience in struct
            break;
          }

          if (currentLine.startsWith("GET /pumpactive")) {    //  should only be hit while in Manual activate pump until Manual ml is zero
            pumpml(eeprom.Manualml);    //  start pumping
            break;
          }

          if (currentLine.startsWith("GET /pumpinactive")) {    //  should only be hit while in Manual stop pump and freeze Manual ml
            pumpml(0.0);    //  stop pumping
            break;
          }

          if (currentLine.startsWith("GET /AutoStats")) {    //  aswer with json for AutoStats also set current mode to Auto
            if (!eeprom.Auto) {
              lastAutopump = millis();    //  reset Auto time
              eeprom.Auto = 1;    //  set auto mode
              EEPROM.put(0, eeprom);
              pumpml(0.0);    //  also stop pump on mode change
            }

            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: application/json");
            client.println("Connection: close");
            client.println();
            client.print("{\"ph\": ");          client.print(mess(),2);
            client.print(", \"tankL\": ");      client.print(eeprom.tankL,2);
            client.print(", \"ml\": ");         client.print(eeprom.Automl,2);    //  constantly show Automl
            client.print(", \"pumpactive\": "); client.print(pumpml() ? 1 : 0);
            client.print("}");
            break;
          }

          if (currentLine.startsWith("GET /ManualStats")) {    //  aswer with json for ManualStats also set current mode to Manual and stop pump
            if (eeprom.Auto) {
              eeprom.Auto = 0;    //  deactivate auto mode
              EEPROM.put(0, eeprom);
              pumpml(0.0);    //  also stop pump on mode change
            }

            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: application/json");
            client.println("Connection: close");
            client.println();
            client.print("{\"ph\": ");          client.print(mess(),2);
            client.print(", \"tankL\": ");      client.print(eeprom.tankL,2);
            client.print(", \"ml\": ");         client.print(eeprom.Manualml,2);    //  show current Manualml left to pump
            client.print(", \"pumpactive\": "); client.print(pumpml() ? 1 : 0);
            client.print("}");
            break;
          }

          if (currentLine.startsWith("GET /serial?")) {
            String query = currentLine.substring(12, currentLine.indexOf(' ', 12));    //  everything after "...?" to the second space is the query
            query.replace("=", " ");    //  alltogether make eg "GET /serial?pumpspeed=10000 HTTP/1.1" to "pumpspeed 10000" and pass this to parseserial()

            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            client.println("recieved overwrite ");
            client.println( parseserial( query ) );    // the query is everything after 8 chars to the second space
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