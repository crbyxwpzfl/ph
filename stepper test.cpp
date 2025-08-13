#include <TMC2209.h>

// This example will not work on Arduino boards without HardwareSerial ports,
// such as the Uno, Nano, and Mini.
//
// See this reference for more details:
// https://www.arduino.cc/reference/en/language/functions/communication/serial/

UART & serial_stream = Serial1;

const long SERIAL_BAUD_RATE = 115200;
const int DELAY = 3000;

TMC2209 stepper_driver;  // Instantiate TMC2209

String incoming = "";


void initstepper() {
  digitalWrite(4, HIGH);  // set Vio high

  delay(500);
  
  Serial.print("initialising stepper. Make sure Vm is on!! or disconnect Uart and Vio now...");  // never drive Uart with Vm off
  
  Serial1.begin(SERIAL_BAUD_RATE);  // Start Serial1 on pins 0 (RX) and 1 (TX)
  
  stepper_driver.setup(serial_stream);

  stepper_driver.setHardwareEnablePin(6);
  stepper_driver.setRunCurrent(10);
  stepper_driver.enableCoolStep();
  stepper_driver.disable();
  stepper_driver.moveAtVelocity(0); // stop stepper

  Serial.println("done. pls wait for comms");
}


void setup()
{
  Serial.begin(SERIAL_BAUD_RATE);

  pinMode(4, OUTPUT);  // Vio pin
  digitalWrite(4, LOW);  // set Vio Low

  analogReadResolution(14); // 10bit 1023, 12bit 4096, 14bit 16383
  pinMode(A3, INPUT);
  Serial.println(analogRead(A3));
  
  
  while (analogRead(A3) < 5000) {  // wait for Vm to come on first
  //while (true) {  // for testing but make sure Vm is on befor plugging in Arduino
    Serial.println(analogRead(A3));
    Serial.println("retry in 1 sec");
    delay(1000);  // try again in 5 sec
  }
  
  initstepper();
}

void loop()
{
  delay(DELAY);

  if (Serial.available()) {
    incoming = "";
    while(Serial.available()){
      delay(2);
      char c = Serial.read();
      incoming += c;
    }
    //Serial.println("recieved " + incoming );
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
