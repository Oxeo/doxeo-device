// Enable debug prints to serial monitor
//#define MY_DEBUG

// Enable and select radio type attached
#define MY_RADIO_RF24

// Enable IRQ pin
#define MY_RX_MESSAGE_BUFFER_FEATURE
#define MY_RF24_IRQ_PIN (2)

// RF24 PA level
#define MY_RF24_PA_LEVEL (RF24_PA_HIGH)

// Enable repeater functionality
#define MY_REPEATER_FEATURE
#define MY_REPEATER_FEATURE_WITHOUT_UPS

#include <MySensors.h>
#include <SPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define PIN_WATER_SENSOR1 A1
#define PIN_WATER_PUMP1 7
#define PIN_WATER_SENSOR2 3
#define PIN_WATER_PUMP2 6
#define PIN_LIGHT 8
#define PIN_LIGHT2 A2
#define PIN_ALIM_TEMPERATURE 4
#define PIN_TEMPERATURE 5

#define TEMPERATURE_PRECISION 11 //Max 12 bit, min 9 bit

#define TEMPERATURE_ID 0
#define PUMP1_ID 1
#define PUMP2_ID 2
#define LIGHT_ID 3
#define LIGHT2_ID 4
#define RELAY_ID 5

// Timer
unsigned long pump1Timer = 0;
unsigned long pump2Timer = 0;
unsigned long lightTimer = 0;
unsigned long light2Timer = 0;
unsigned long _heartbeatTime = 0;

// Temperature sensor
OneWire oneWire(PIN_TEMPERATURE);
DallasTemperature sensors(&oneWire);
byte dallasSensorAddress[8];

// Status
bool lightOn;
bool light2On;
bool pump1On;
bool pump2On;
bool pump1IsRunning;
bool pump2IsRunning;
unsigned long lastSendTemperatureTime = 0;

// mode on off auto for pump1
unsigned long pump1AutoModeTimeOn = 0;
unsigned long pump1AutoModeTimeOff = 0;
unsigned long lastChangedWaterPump1Status = 0;

MyMessage msgTemperature(TEMPERATURE_ID, V_TEMP);
MyMessage msgPump1(PUMP1_ID, V_CUSTOM);
MyMessage msgPump2(PUMP2_ID, V_CUSTOM);
MyMessage msgLight(LIGHT_ID, V_CUSTOM);
MyMessage msgLight2(LIGHT2_ID, V_CUSTOM);
MyMessage msgRelay(RELAY_ID, V_CUSTOM);

MyMessage msgToRelay;
unsigned long relayTimer = 0;

void before()
{
  // init PIN
  pinMode(PIN_LIGHT, OUTPUT);
  pinMode(PIN_LIGHT2, OUTPUT);
  pinMode(PIN_WATER_PUMP1, OUTPUT);
  pinMode(PIN_WATER_SENSOR1, INPUT_PULLUP);
  pinMode(PIN_WATER_PUMP2, OUTPUT);
  pinMode(PIN_WATER_SENSOR2, INPUT_PULLUP);

  digitalWrite(PIN_WATER_PUMP1, LOW);
  digitalWrite(PIN_WATER_PUMP2, LOW);
  digitalWrite(PIN_LIGHT, LOW);
  digitalWrite(PIN_LIGHT2, LOW);
}

void setup() {
  initializeDallasSensor();

  enablePump1(0);
  enablePump2(0);
  enableLight(0);
  enableLight2(0);
}

void presentation() {
  // Send the sketch version information to the gateway and Controller
  sendSketchInfo("Fountain", "1.3");

  // Present sensor to controller
  present(TEMPERATURE_ID, S_TEMP, "temperature");
  present(PUMP1_ID, S_CUSTOM, "pump 1");
  present(PUMP2_ID, S_CUSTOM, "pump 2");
  present(LIGHT_ID, S_CUSTOM, "light 1");
  present(LIGHT2_ID, S_CUSTOM, "light 2");
}

void receive(const MyMessage &message)
{
  if (message.type == V_CUSTOM) {
    String data = message.getString();

    if (message.sensor == PUMP1_ID) {
      unsigned long timeSelectedMinute = parseMsg(data, '-', 0).toInt();
      unsigned long timeOnMinute = parseMsg(data, '-', 1).toInt();
      unsigned long timeOffMinute = parseMsg(data, '-', 2).toInt();

      enablePump1(timeSelectedMinute * 60UL, timeOnMinute * 60UL, timeOffMinute * 60UL);
    }

    if (message.sensor == PUMP2_ID) {
      unsigned long timeSelectedMinute = data.toInt();
      enablePump2(timeSelectedMinute * 60UL);
    }

    if (message.sensor == LIGHT_ID) {
      unsigned long timeSelectedMinute = data.toInt();
      enableLight(timeSelectedMinute * 60UL);
    }

    if (message.sensor == LIGHT2_ID) {
      unsigned long timeSelectedMinute = data.toInt();
      enableLight2(timeSelectedMinute * 60UL);
    }

    if (message.sensor == RELAY_ID) {
      relayMessage(&message);
    }
  }
}

void loop() {
  if ((millis() - lastSendTemperatureTime) >= 600000) {
    lastSendTemperatureTime = millis();

    float temp = getDallasTemperature();
    send(msgTemperature.set(temp, 1));
  }

  if (lightOn || light2On || pump1On || pump2On) {

    if (lightOn && lightTimer < millis()) {
      enableLight(0);
    }

    if (light2On && light2Timer < millis()) {
      enableLight2(0);
    }

    if (pump1On && pump1Timer < millis()) {
      enablePump1(0);
    }

    if (pump2On && pump2Timer < millis()) {
      enablePump2(0);
    }

    if (pump1On) {
      // manage water sensor
      if (digitalRead(PIN_WATER_SENSOR1) == HIGH) {
        enablePump1(0);
        send(msgPump1.set("no water"));
      }

      // manage on off auto mode
      if (pump1AutoModeTimeOn != 0 && pump1AutoModeTimeOff != 0) {
        if (pump1IsRunning && millis() > lastChangedWaterPump1Status + pump1AutoModeTimeOn) {
          startPump1(false);
          send(msgPump1.set("stand by"));
        } else if (pump1IsRunning == false && millis() > lastChangedWaterPump1Status + pump1AutoModeTimeOff) {
          startPump1(true);
          send(msgPump1.set("started"));
        }
      }
    }

    if (pump2On) {
      // manage water sensor
      if (digitalRead(PIN_WATER_SENSOR2) == HIGH) {
        enablePump2(0);
        send(msgPump2.set("no water"));
      }
    }
  }

  manageHeartbeat();
}

void startPump1(bool start) {
  if (start) {
    pump1IsRunning = true;
    digitalWrite(PIN_WATER_PUMP1, HIGH);
  } else {
    pump1IsRunning = false;
    digitalWrite(PIN_WATER_PUMP1, LOW);
  }
  lastChangedWaterPump1Status = millis();
}

void startPump2(bool start) {
  if (start) {
    pump2IsRunning = true;
    digitalWrite(PIN_WATER_PUMP2, HIGH);
  } else {
    pump2IsRunning = false;
    digitalWrite(PIN_WATER_PUMP2, LOW);
  }
}

void enablePump1(unsigned long durationSecond) {
  enablePump1(durationSecond, 0, 0);
}

void enablePump1(unsigned long durationSecond, unsigned long timeOnSecond, unsigned long timeOffSecond) {
  if (durationSecond > 0) {
    pinMode(PIN_WATER_SENSOR1, INPUT_PULLUP);
    startPump1(true);
    pump1Timer = millis() + durationSecond * 1000;
    pump1AutoModeTimeOn = timeOnSecond * 1000;
    pump1AutoModeTimeOff = timeOffSecond * 1000;
    pump1On = true;
    send(msgPump1.set("started"));
  } else {
    startPump1(false);
    pinMode(PIN_WATER_SENSOR1, INPUT); // to save energy
    pump1Timer = 0;
    pump1On = false;
    send(msgPump1.set("stopped"));
  }
}

void enablePump2(unsigned long durationSecond) {
  if (durationSecond > 0) {
    pinMode(PIN_WATER_SENSOR2, INPUT_PULLUP);
    startPump2(true);
    pump2Timer = millis() + durationSecond * 1000;
    pump2On = true;
    send(msgPump2.set("started"));
  } else {
    startPump2(false);
    pinMode(PIN_WATER_SENSOR2, INPUT); // to save energy
    pump2Timer = 0;
    pump2On = false;
    send(msgPump2.set("stopped"));
  }
}

void enableLight(unsigned long durationSecond) {
  if (durationSecond > 0) {
    digitalWrite(PIN_LIGHT, HIGH);
    lightTimer = millis() + durationSecond * 1000;
    lightOn = true;
    send(msgLight.set("started"));
  } else {
    digitalWrite(PIN_LIGHT, LOW);
    lightTimer = 0;
    lightOn = false;
    send(msgLight.set("stopped"));
  }
}

void enableLight2(unsigned long durationSecond) {
  if (durationSecond > 0) {
    digitalWrite(PIN_LIGHT2, HIGH);
    light2Timer = millis() + durationSecond * 1000;
    light2On = true;
    send(msgLight2.set("started"));
  } else {
    digitalWrite(PIN_LIGHT2, LOW);
    light2Timer = 0;
    light2On = false;
    send(msgLight2.set("stopped"));
  }
}

void initializeDallasSensor() {
  pinMode(PIN_ALIM_TEMPERATURE, OUTPUT);
  digitalWrite(PIN_ALIM_TEMPERATURE, HIGH);

  // Allow 50ms for the sensor to be ready
  delay(50);

  sensors.begin();
  sensors.setWaitForConversion(false);
  int numSensors = sensors.getDeviceCount();
  oneWire.search(dallasSensorAddress);
  sensors.setResolution(dallasSensorAddress, TEMPERATURE_PRECISION);

  // set power pin for DS18B20 to input before sleeping, saves power
  digitalWrite(PIN_ALIM_TEMPERATURE, LOW);
  pinMode(PIN_ALIM_TEMPERATURE, INPUT);
}

float getDallasTemperature() {
  pinMode(PIN_ALIM_TEMPERATURE, OUTPUT);
  digitalWrite(PIN_ALIM_TEMPERATURE, HIGH);
  sleep(30);

  sensors.setResolution(dallasSensorAddress, TEMPERATURE_PRECISION);
  sensors.requestTemperatures(); // Send the command to get temperatures

  // 9bit requres 95ms, 10bit 187ms, 11bit 375ms and 12bit resolution takes 750ms
  sleep(400);

  // Fetch temperature
  float temperature = sensors.getTempC(dallasSensorAddress);

  // set power pin for DS18B20 to input before sleeping, saves power
  digitalWrite(PIN_ALIM_TEMPERATURE, LOW);
  pinMode(PIN_ALIM_TEMPERATURE, INPUT);

  return temperature;
}

String parseMsg(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void wakeUp()
{
  // do nothing
}

inline void manageHeartbeat() {
  static unsigned long _heartbeatLastSend = 0;
  static unsigned long _heartbeatWait = random(1000, 60000);
  static unsigned long _heartbeatRetryNb = 0;

  if (millis() - _heartbeatLastSend >= _heartbeatWait) {
    bool success = sendHeartbeat();

    if (success) {
      _heartbeatWait = 60000;
      _heartbeatRetryNb = 0;
    } else {
      if (_heartbeatRetryNb < 10) {
        _heartbeatWait = random(100, 3000);
        _heartbeatRetryNb++;
      } else {
        _heartbeatWait = random(45000, 60000);
        _heartbeatRetryNb = 0;
      }
    }
    
    _heartbeatLastSend = millis();
  }
}


void relayMessage(const MyMessage *message) {
  if (getPayload(message->getString()) != NULL) {
      msgToRelay.destination = getMessagePart(message->getString(), 0);
      msgToRelay.sensor = getMessagePart(message->getString(), 1);
      mSetCommand(msgToRelay, getMessagePart(message->getString(), 2));
      mSetRequestAck(msgToRelay, getMessagePart(message->getString(), 3));
      msgToRelay.type = getMessagePart(message->getString(), 4);
      msgToRelay.sender = message->sender;
      mSetAck(msgToRelay, false);
      msgToRelay.set(getPayload(message->getString()));

      relayTimer = millis();
      bool success = false;
      while(millis() - relayTimer < 2500 && !success) {
        success = transportSendWrite(msgToRelay.destination, msgToRelay);
        wait(5);
      }

      if (success) {
        send(msgRelay.set(F("success")));
      } else {
        send(msgRelay.set(F("ko")));
      }
    }
}

int getMessagePart(const char* message, const byte index) {
  byte indexCount = 0;

  if (index == 0 && strlen(message) > 0) {
    return atoi(message);
  }
  
  for (byte i=0; i < strlen(message) - 1; i++) {
    if (message[i] == '-') {
      indexCount++;
    }

    if (indexCount == index) {
      return atoi(message + i + 1);
    }
  }

  return 0;
}

char* getPayload(const char* message) {
  byte indexCount = 0;
  
  for (byte i=0; i < strlen(message) - 1; i++) {
    if (message[i] == '-') {
      indexCount++;
    }

    if (indexCount == 5) {
      return message + i + 1;
    }
  }

  return NULL;
}
