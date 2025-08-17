


// this for an arduino r4 wifi to read ph levels and display them on the led matrix aswell as a web interface
// but this is shit and does not read consistently


#include "WiFiS3.h"    //  wifi stuff
#include <WiFiUdp.h>
#include <ArduinoMDNS.h>
int status = WL_IDLE_STATUS;
WiFiUDP udp;
MDNS mdns(udp);
WiFiServer server(80);

float ph = 0.0;    //  set in mess()
float mean = 0.0;    //  set in mess()
float slope = 0.0;    //  set in calibrate()
float intercept = 0.0;    //  set in calibrate()
uint16_t* messvals = nullptr;    //  Global pointer for dynamically allocated array


#include <EEPROM.h>    //  eeprom is non voletile so saved on powerloss and reset
struct{ char ssid[30]; char pw[30]; uint16_t smoothness; float upperphref; float lowerphref; uint16_t upperanalogref; uint16_t loweranalogref; float phoffset; } eeprom;    //  fixed size for calibartion pairs since i dont know malloc


void calibrate(float upperref = 0.0, float lowerref = 0.0 ) {    //  overwrites referenc values and recalculates linear interpolation
  if (upperref) { eeprom.upperphref = upperref; eeprom.upperanalogref = mean; }    //  update upper reference
  if (lowerref) { eeprom.lowerphref = lowerref; eeprom.loweranalogref = mean; }    // update lower reference
  slope = (eeprom.upperphref - eeprom.lowerphref) / (float)(eeprom.upperanalogref - eeprom.loweranalogref);
  intercept = (float)eeprom.upperphref - slope * eeprom.upperanalogref;
  Serial.println("success calibrated slope " + String(slope, 10) + ", intercept " + String(intercept));    //  echo calibration DEBUG
}

void parseserial(String str) {    //  for user to overwrite eeprom struct
  str.trim();
  if (str.startsWith("ssid ")) { String ssidStr = str.substring(5); ssidStr.toCharArray(eeprom.ssid, sizeof(eeprom.ssid)); }
  if (str.startsWith("pw ")) { String pwStr = str.substring(3); pwStr.toCharArray(eeprom.pw, sizeof(eeprom.pw)); }
  if (str.startsWith("smoothness ")) { String smoothnessStr = str.substring(11); eeprom.smoothness = smoothnessStr.toInt(); initmess(); }    //  realloc messvals arry
  if (str.startsWith("upperphref ")) { String upperphrefStr = str.substring(11); calibrate( upperphrefStr.toFloat(), 0.0 ); }
  if (str.startsWith("lowerphref ")) { String lowerphrefStr = str.substring(11); calibrate( 0.0, lowerphrefStr.toFloat() ); }
  if (str.startsWith("phoffset ")) { String phoffsetStr = str.substring(9); eeprom.phoffset = phoffsetStr.toFloat(); }
  EEPROM.put(0, eeprom);    //  put updated values into eeprom
  Serial.println("eeprom vals, " + String(eeprom.ssid) + ", " + String(eeprom.smoothness) + ", " + String(eeprom.upperanalogref) + ", " + String(eeprom.upperphref) + ", " + String(eeprom.loweranalogref) + ", " + String(eeprom.lowerphref));    //  echo eeprom DEBUG
}

void initmess() {    //  respwans and initialises messval array when smoothness is overwritten, smoothness is the count of messvals used for mean calculation
  analogReadResolution(14); // 10bit 1023, 12bit 4096, 14bit 16383
  pinMode(A0, INPUT);
  calibrate();    //  caluculate linear interpolation of reference values from eeprom
  if (eeprom.smoothness > 0 && eeprom.smoothness < 500) {    //  allocate memory for the array based on smoothness
    if (messvals != nullptr) delete[] messvals;    //  free previously allocated memory if any
    messvals = new uint16_t[eeprom.smoothness];    //  dynamically allocate the array
    for (int i=0; i<eeprom.smoothness; i++) { mess(); delay(10); }    //  prefill messvals array so ph is correct
    Serial.println("success messvals smoothness " + String(eeprom.smoothness));
  } else { Serial.println( String(eeprom.smoothness) + "smoothness not in range 0-500"); }
}

void mess() {    //  messure analog voltage and convert to ph with continous mean, smoothness is the count of messvals used for mean calculation
  static uint16_t index = 0;    //  static uint16_t messvals[ eeprom.smoothness ] = {}; does not work cause messvals has to be dynamically alloc
  messvals[index] = analogRead(A0);    //  read one value into messvals
  index = ++index%eeprom.smoothness;    //  increment index of messvals or reset index to 0
  mean = 0.0;    //  reset mean of readings and recalculate mean
  for (int i=0; i<eeprom.smoothness; i++) mean += messvals[i];
  mean /= (float)eeprom.smoothness;
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
  while (status != WL_CONNECTED){
    if(millis() > lastloop+10000){    //  retry wifi every 10sec
      lastloop = millis();
      Serial.println( "try ssid " + String(eeprom.ssid) );
      status = WiFi.begin(eeprom.ssid, eeprom.pw);    //  Connect to WPA/WPA2 network. Change this line if using open or WEP network
    }
    if (Serial.available() > 0) parseserial(Serial.readString());    //  let user change eeprom via serial while waiting for wifi
  }
  server.begin();
   
  initmess();    //  allocates messvals and calibrates with eeprom values here to give wifi some time to start

  mdns.begin(WiFi.localIP(), "arduino");    //  setup mdns for arduino.local
  mdns.addServiceRecord("Arduino mDNS Webserver Example._http", 80, MDNSServiceTCP);
  
  Serial.println("success ssid " + String(WiFi.SSID()) + ", ip " + WiFi.localIP().toString() + " or arduino.local, rssi " + String(WiFi.RSSI()) + "dBm");    //  confirm wifi DEBUG
}

void loop() {
  mdns.run();

  if (Serial.available() > 0) parseserial(Serial.readString());    //  let user change eeprom via serial for debugging

  mess();    //  update ph every loop only in Auto mode TODO

  WiFiClient client = server.available();  // listen for incoming clients
  if (client) {
    String currentLine = "";                // make a String to hold incoming data from the client
    Serial.println("new client");
    boolean currentLineIsBlank = true;  // an HTTP request ends with a blank line
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);  // if you've gotten to the end of the line (received a newline character) and the line is blank, the HTTP request has ended, so you can send a reply
        if (c == '\n') {
          if (currentLine.length() == 0) {
            //if (c == '\n' && currentLineIsBlank) {
            Serial.println("");
            Serial.println("client wants page");
            client.print(R"rawliteral(
HTTP/1.1 200 OK
Content-Type: text/html
Connection: close

<!DOCTYPE HTML>
<html lang='en'>
<head>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <meta charset='UTF-8'>
    <style>
        input[type=time]::-webkit-datetime-edit-text { color: white; }
        input[type=time]::-webkit-datetime-edit-hour-field {
            background-color: #f2f4f5;
            border-radius: 15%;
            padding: 19px 13px;
        }
        input[type=time]::-webkit-datetime-edit-minute-field {
            background-color: #f2f4f5;
            border-radius: 15%;
            padding: 19px 13px;
        }
        .box {
            width: 40%;
            margin: 0 auto;
            background: rgba(255, 255, 255, 0.2);
            padding: 35px;
            border: 2px solid #fff;
            border-radius: 20px/50px;
            background-clip: padding-box;
            text-align: center;
        }
        .button {
            font-size: 1em;
            padding: 10px;
            color: #fff;
            border: 2px solid #06D85F;
            border-radius: 20px/50px;
            text-decoration: none;
            cursor: pointer;
            transition: all 0.3s ease-out;
        }
        .button:hover { background: #06D85F; }
        .overlay {
            position: fixed;
            top: 0;
            bottom: 0;
            left: 0;
            right: 0;
            background: rgba(0, 0, 0, 0.7);
            transition: opacity 500ms;
            visibility: hidden;
            opacity: 0;
        }
        .popup {
            margin: 100px auto;
            padding: 50px;
            background: #fff;
            border-radius: 5px;
            width: 60%;
            position: relative;
            transition: all 5s ease-in-out;
        }
        .popup h2 {
            margin-top: 0;
            color: #333;
            font-family: Tahoma, Arial, sans-serif;
        }
        .popup .close {
            position: absolute;
            top: 10px;
            right: 30px;
            transition: all 200ms;
            font-size: 30px;
            font-weight: bold;
            text-decoration: none;
            color: #333;
        }
        .popup .close:hover { color: #2196F3; }
        .popup .content {
            max-height: 30%;
            overflow: auto;
        }
        @media screen and (max-width: 700px) {
            .box { width: 70%; }
            .popup { width: 70%; }
        }
        .switch {
            position: relative;
            display: inline-block;
            width: 60px;
            height: 34px;
        }
        .switch input {
            opacity: 0;
            width: 0;
            height: 0;
        }
        .slider {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: #ccc;
            -webkit-transition: .4s;
            transition: .4s;
            border-radius: 34px;
        }
        .slider:before {
            position: absolute;
            content: '';
            height: 26px;
            width: 26px;
            left: 4px;
            bottom: 4px;
            background-color: white;
            -webkit-transition: .4s;
            transition: .4s;
            border-radius: 50%;
        }
        input:checked + .slider { background-color: #2196F3; }
        input:focus + .slider { box-shadow: 0 0 1px #2196F3; }
        input:checked + .slider:before {
            -webkit-transform: translateX(26px);
            -ms-transform: translateX(26px);
            transform: translateX(26px);
        }
    </style>
    <title>pH</title>
</head>
<body>
<main align='center'>
    <div id='popup' class='overlay' style='visibility: hidden;'>
        <div class='popup'></br></br>
            <h2>ENTER TRUE PH VALUE</h2></br></br>
            <a class='close' id='close' style='font-family:Arial; font-size:60px; font-weight: bolder;'>&times;</a>
            <div class='content'>
                <input style='border: none; color: #2a2c2d; font-family:Arial; font-size:100px; font-weight: bolder;' type='time' id='ph' min='00:00' max='14:00' value='07:00' />
                <a style='font-family:Arial; font-size:40px; font-weight: bolder;'>&ensp;ph</a>
                </br></br></br><h2 id='send'>SEND</h2></br>
            </div>
        </div>
    </div>
    <div class='box'>
        </br></br></br></br></br></br>
        <h1 style='font-family:Arial; font-size:100px' id='phvalue'>0</h1>
        <h1 style='font-family:Arial; font-size:15px' id='phvalue'>DISPLAY</h1>
        <label class='switch' id='switch'>
            <input type='checkbox' id='checkbox'>
            <span class='slider round'></span>
        </label>
        </br></br></br></br></br></br></br></br>
        <h1 style='font-family:Arial; font-size:15px' id='open'>CALIBRATE</h1>
    </div>
</main>
<script>
    setInterval(timer, 500);
    document.getElementById('close').addEventListener('click', close);
    document.getElementById('open').addEventListener('click', open);
    document.getElementById('send').addEventListener('click', send);
    function timer() {
        if (document.hasFocus() && document.getElementById('popup').style.visibility == 'hidden') {
            fetchdata();
        }
    }
    function close() {
        document.getElementById('popup').style.visibility = 'hidden';
        document.getElementById('popup').style.opacity = 0;
        document.getElementById('switch').style.visibility = 'visible';
        document.getElementById('switch').style.opacity = 1;
    }
    function open() {
        document.getElementById('popup').style.visibility = 'visible';
        document.getElementById('popup').style.opacity = 1;
        document.getElementById('switch').style.visibility = 'hidden';
        document.getElementById('switch').style.opacity = 0;
    }
    function send() {
        val = document.getElementById('ph').value;
        ph = val[0] + val[1] + '.' + val[3] + val[4] + 'ph';
        console.log('sending ' + ph);
        fetch(ph, { cache: 'no-cache' })
            .then(response => response.json())
            .then(data => handle(data));
    }
    var cb = document.getElementById('checkbox');
    var befor = false;
    function fetchdata() {
        if (cb.checked != befor) { req = cb.checked; befor = cb.checked; } else { req = 'get'; }
        fetch(req, { cache: 'no-cache' })
            .then(response => response.json())
            .then(data => handle(data));
    }
    function handle(data) {
        console.log(data);
        if (cb.checked != data['display']) { cb.click(); console.log('set cb to ' + data['display']); }
        document.getElementById('phvalue').innerHTML = data['ph'];
    }
</script>
</body>
</html>
)rawliteral");
            break;
          } else {    // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
        
        if (currentLine.endsWith("GET /false")) {
          //displaystate = false;
          Serial.println("");
          Serial.println("client wants diplay false");
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: application/json");
          client.println("Connection: close");
          client.println();
          client.print("\{\"ph\": ");
          client.print(ph);
          client.print(", ");
          client.print("\"display\": ");
          client.print(0); //client.print(displaystate);
          client.print("\}");
          break;
        }

        if (currentLine.endsWith("GET /true")) {
          //displaystate = true;
          Serial.println("");
          Serial.println("client wants diplay true");
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: application/json");
          client.println("Connection: close");
          client.println();
          client.print("\{\"ph\": ");
          Serial.println(ph);
          client.print(ph);
          client.print(", ");
          client.print("\"display\": ");
          client.print(0); //client.print(displaystate);
          client.print("\}");
          break;
        }

        if (currentLine.endsWith("GET /get")) {
          Serial.println("");
          Serial.println("client wants data");
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: application/json");
          client.println("Connection: close");
          client.println();
          client.print("\{\"ph\": ");
          client.print(ph);
          Serial.println(ph);
          client.print(", ");
          client.print("\"display\": ");
          client.print(0); //client.print(displaystate);
          client.print("\}");
          break;
        }
        
        /*
        if (currentLine.endsWith("ph")) {
          currentLine.remove(0,5);
          float knownph = currentLine.toFloat();
          //addcalipair(knownph, volt);
          //EEPROM.get(0, calipairs);
          Serial.println("");
          Serial.println("client wants to calibarate");
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: application/json");
          client.println("Connection: close");
          client.println();
          client.print("\{\"ph\": ");
          client.print(ph);
          client.print(", ");
          client.print("\"display\": ");
          client.print("nan");
          client.print(", ");
          client.print("\"slope\": ");
          client.print(slope);
          client.print(", ");
          client.print("\"intercept\": ");
          client.print(intercept);
          client.print(", ");
          client.print("\"calipairs\": ");
          client.print("\[");
          client.print("\["); client.print(calipairs[0].voltx); client.print(", "); client.print(calipairs[0].phy); client.print("\]"); client.print(", ");
          client.print("\["); client.print(calipairs[1].voltx); client.print(", "); client.print(calipairs[1].phy); client.print("\]"); client.print(", ");
          client.print("\["); client.print(calipairs[2].voltx); client.print(", "); client.print(calipairs[2].phy); client.print("\]"); client.print(", ");
          client.print("\["); client.print(calipairs[3].voltx); client.print(", "); client.print(calipairs[3].phy); client.print("\]"); client.print(", ");
          client.print("\["); client.print(calipairs[4].voltx); client.print(", "); client.print(calipairs[4].phy); client.print("\]");
          client.print("\]");
          client.print("\}");
          break;
        }
        */

      }
    }
    //delay(1); // give the web browser time to receive the data
    client.stop();
    Serial.println("client disconnected");
    Serial.println("");
  }
}