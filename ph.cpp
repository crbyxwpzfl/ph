


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
        float Autodeadzone;    //  minimal ph diff to pump sth
        float Autophsetpoint;    //  ph set point
        float Automl;    //  amount in ml to pump for 0.1 ph diff
        float Manualml;    //  this is not neccessary to save but it is easyer to just have this in global struct aswell


        uint16_t pumpspeed;    //  motor speed
        float pumpmsperml;    //  pump calibration value

        float phoffset;    //  constant ph offset
        float slope;    //  slope of the calibration curve
        float intercept;    //  intercept of the calibration curve

      } eeprom;


static inline bool msSince(uint32_t lasttime, uint32_t intervalms) {    //  helper to do wrapsave time intervals
  return (uint32_t)(millis() - lasttime) >= intervalms;
}


void initpump() {
  uint16_t Vm = analogRead(A3); Serial.print("Vm is " + String(Vm));

  if (Vm > 5000) {
    digitalWrite(4, HIGH);  // set Vio high
    Serial.print(" so init pump");

    Serial1.begin(115200);  // Start Serial1 on pins 0 (RX) and 1 (TX)
    stepper_driver.setup(serial_stream);

    stepper_driver.setHardwareEnablePin(6);
    stepper_driver.setRunCurrent(10);
    stepper_driver.enableCoolStep();
    stepper_driver.disable();
    stepper_driver.moveAtVelocity(0); // stop stepper

    Serial.println(" done pls wait for comms");
  } else {
    Serial.println(" so abort");
  }
}


String calibratepump(float value) {
  static uint8_t firstcall = 1;
  
  static uint32_t totalpumpms = 0; 

  if (firstcall) {    // on first call treat value as speed setting
    eeprom.pumpspeed = (uint32_t)value;    //  pump speed good values are somewhere around 5000 to 40000

    // TODO start pump here for a fixed time intervall remember to change the 20sec intervall below!!!!
    totalpumpms = pumpml(20.0);    //  pump 20ml and safe total pump time
    
    firstcall = 0;    //  next call is not for speed
    return ("pump will calibrate to " + String(eeprom.pumpspeed) + " pls wait for pump to stop then enter ml");    //  tell user to wait while pump is running
  }

  if (!firstcall) {    //  TODO check for pump still running here!!!!
    eeprom.pumpmsperml = totalpumpms / value ;    //  calculate ms per ml
    firstcall = 1;    //  prep for next calibration
    EEPROM.put(0, eeprom);    //  put calculated ml per ms and corresponding speed into eeprom
    return ("pump calibrated to " + String(eeprom.pumpmsperml) + " ml/ms");
  }
}


uint32_t pumpml(float ml = -69.0){    //  TODO change this so we can pump mls baised on calibration value. 
                            //  WHILE running/pumping this should return the mls wich are left to pump and the correct amount of tankL
                            //  when done pumping this should update the eeprom value for tankL once not constantly
                            //  add a stop functionality
                            //  pumpml() should return the amount of ml left to pump later this is used to see if pump is running and to update manual pumped ml number
                            //  perhaps add a power percentage to eeprom
                            //  perhaps add stallguard detection

  static uint32_t pumpactivation = 0;    //  timestamp when pump was activated
  static uint32_t totalpumpms = 0;    //  total time to pump in ms

  if (ml > 99.9) {    //  consider a value of great than this as a fault TODO reconsider this perhaps this is stoopid since this can hardlock for a big ph diff
    // TODO shut off pump to be safe here
    return 0;    //  return pump off
  }

  //  TODO only do this when pump is running otherwise the msSince() causes problemis
  if (ml < 0.0) {    //  this should only occur for periodical call so check current time against total pump time and perhaps stop pump
    if (msSince(pumpactivation, totalpumpms)) {    //  when pump time is up stop pump
      // TODO stop pump here
      EEPROM.put(0, eeprom);    //  save current tankL here
      return 0;    //  return pump off
    } else {
      // TODO recaluclate eeprom.Manualml
      // TODO update tank level
      return 1;    //  return pumping
    }
  }

  if (!ml) {    //  when called with zero this should stop pump and reset Manualml
    eeprom.Manualml = 0.0;    //  reset Manualml
    // TODO shut off pump
    return 0; //  return pump off
  }

  if (ml) {    //  when called with a value this should pump the amount
    pumpactivation = millis();    //  save activation time stamp
    totalpumpms = ml * eeprom.pumpmsperml;    //  calculate total pump time with pumpmsperml
    // TODO activate pump
    return totalpumpms;    //  return pump on
  }



  if (stepper_driver.isSetupAndCommunicating())
  {    
    bool hardware_disabled = stepper_driver.hardwareDisabled();
    TMC2209::Settings settings = stepper_driver.getSettings();
    TMC2209::Status status = stepper_driver.getStatus();
    bool software_enabled = settings.software_enabled;
    bool standstill = status.standstill;

    if (hardware_disabled && !software_enabled) {
      stepper_driver.enable();
      delay(100);
    }

    Serial.println("setup and comms good."
                  ". hw is " + String(hardware_disabled ? "disabled" : "enabled") + 
                  ". sw is " + String(software_enabled ? "enabled" : "disabled") + 
                  ". stepper is " + String(standstill ? "standstill" : "moving")  );

    if (incoming == "stop") {  // dont send line endings
      stepper_driver.moveAtVelocity(0); // stop stepper
      stepper_driver.disable();  // Software Disable
      Serial.println("stepper stopped.");
      incoming = "";
    }

    if (incoming.startsWith("speed ") && !hardware_disabled && software_enabled) {
      Serial.println("setting speed to " + String((int32_t)incoming.substring(6).toInt()) );
      stepper_driver.moveAtVelocity((int32_t)incoming.substring(6).toInt());
      incoming = "";
    }

    if (incoming.startsWith("power ")) {
      Serial.println("setting power procent to " + String((int32_t)incoming.substring(6).toInt()) );
      stepper_driver.moveAtVelocity(0); // stop stepper
      stepper_driver.disable();  // Software Disable
      stepper_driver.setRunCurrent((int32_t)incoming.substring(6).toInt());
      stepper_driver.enable();
      incoming = "";
    }

    return; // Exit early, loop() will be called again
  }
  

  if (stepper_driver.isCommunicatingButNotSetup())
  {
    Serial.println("no setup but comms good. try again");
    initstepper();
    return; // Exit early, loop() will be called again
  }


  Serial.println("no comms. connect Vm then type 'start'");
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
    return ("first pair is " + String(truepharr[index]) + ", " + String(analogarr[index]) + " pls enter a second value");
  }

  EEPROM.put(0, eeprom);    //  put calculated intercept and slope into eeprom
  return ("ph calibrated to " + String(eeprom.slope, 10) + ", intercept " + String(eeprom.intercept));    //  echo calibration DEBUG
}


float mess(bool returnanalogvalue = false) {    //  messure analog voltage and convert to ph with continous mean, 100 is the count of messvals used for mean calculation
  static uint16_t index = 0;
  static uint16_t messvals[100] = {};    // switch to fixed size array to simplify
  float analogmean = 0.0;    //  reset mean of readings and recalculate mean

  messvals[index] = analogRead(A0);    //  read one value into messvals
  index = (index + 1) % 100;    //  increment index of messvals or reset index to 0

  for (int i=0; i<100; i++) analogmean += messvals[i];
  analogmean /= (float)100;
  ph = (eeprom.slope * analogmean ) + eeprom.intercept + eeprom.phoffset;    //  convert mean of analog readings to ph
  return returnanalogvalue ? analogmean : ph;
}


void parseserial(String str) {    //  for user to overwrite eeprom struct
  str.trim();
  if (str.startsWith("ssid "))       { String ssidStr       = str.substring(5);  ssidStr.toCharArray(eeprom.ssid, sizeof(eeprom.ssid)); }
  if (str.startsWith("pw "))         { String pwStr         = str.substring(3);  pwStr.toCharArray(eeprom.pw, sizeof(eeprom.pw)); }

  if (str.startsWith("tankL "))      { String tankLStr      = str.substring(6);  eeprom.tankL = tankLStr.toFloat(); }

  if (str.startsWith("Auto "))   { String AutoStr   = str.substring(9);  eeprom.Auto = AutoStr.toInt(); }
  if (str.startsWith("Autophsetpoint ")) { String AutophsetpointStr = str.substring(11); eeprom.Autophsetpoint = AutophsetpointStr.toFloat(); }
  if (str.startsWith("Automl "))     { String AutomlStr     = str.substring(8);  eeprom.Automl = AutomlStr.toFloat(); }
  if (str.startsWith("Autodeadzone ")) { String AutodeadzoneStr = str.substring(13); eeprom.Autodeadzone = AutodeadzoneStr.toFloat(); }

  if (str.startsWith("calibratepump "))      { String speedStr      = str.substring(6);  eeprom.speed = speedStr.toInt(); }  //  TODO this should start pump calibration


  if (str.startsWith("phoffset "))   { String phoffsetStr   = str.substring(9);  eeprom.phoffset = phoffsetStr.toFloat(); }
  if (str.startsWith("calibrateph ")) {}    //  TODO this should start pump calibration


  // TODO perhaps add manual dispense here also

  EEPROM.put(0, eeprom);    //  put updated values into eeprom
  Serial.println("eeprom vals, " + String(eeprom.ssid) + ", " + String(eeprom.upperanalogref) + ", " + String(eeprom.upperphref) + ", " + String(eeprom.loweranalogref) + ", " + String(eeprom.lowerphref));    //  echo eeprom DEBUG
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
    if (Serial.available() > 0) parseserial(Serial.readString());    //  let user change eeprom via serial while waiting for wifi
  }
  Serial.println("success" + "rssi " + String(WiFi.RSSI()) + "dBm");

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
}

void loop() {
  if (Serial.available() > 0) parseserial(Serial.readString());    //  let user change eeprom via serial for debugging

  mdns.run();    //  periodically call mdns

  mess();    //  measure ph every loop

  pumpml();    //  periodically call pump

  if (eeprom.Auto && msSince(lastAutopump, eeprom.Autointerval)) {    //  when in auto and its time for auto pump
    float phdiff = (float)(mess() - eeprom.Autophsetpoint)    //  calculate ph diff
    if (phdiff > eeprom.Autodeadzone) {    //  only pump when ph diff is greater than the deadzone
        lastAutopump = millis();
        pumpml((float)(phdiff * 10.0f * eeprom.Automl));    //  calculate ml to pump phdiff times Automl per 0.1ph diff
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

          if (currentLine.startsWith("GET /Auto")) {    //  aswer with current mode
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            client.print(eeprom.Auto);
            break;
          }

          if (currentLine.startsWith("GET /phsetpoint?value=")) {    //  overwrite ph setpoint here should only be hit with non zero values
            eeprom.phsetpoint = currentLine.substring(22, currentLine.indexOf(' ', 22)).toFloat();
            EEPROM.put(0, eeprom);    //  save new value
            break;
          }

          if (currentLine.startsWith("GET /tankLevel?value=")) {    //  overwrite tank level here should only be hit with non zero values
            eeprom.tankL = currentLine.substring(21, currentLine.indexOf(' ', 21)).toFloat();
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
              lastAutopump = millis(); eeprom.Auto = 1;    //  reset Auto time and activate auto
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
              eeprom.Auto = 0;    //  reset Auto time and deactivate auto
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

          if (currentLine.startsWith("GET /ow?")) {
            String query = currentLine.substring(8, currentLine.indexOf(' ', 8));    //  the query is everything after 8 chars to the second space
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            client.println("recieved overwrite " + query);
            if (query.startsWith("speed")) Serial.println("speed overwrite");

            // TODO additionally add all the info of eeprom and current variables here

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