// Enable debug prints to serial monitor
//#define MY_DEBUG

// Enable and select radio type attached
#define MY_RADIO_RF24

// Enable IRQ pin
#define MY_RX_MESSAGE_BUFFER_FEATURE
#define MY_RF24_IRQ_PIN (2)
//#define MY_RX_MESSAGE_BUFFER_SIZE (10)

// RF24 PA level
#define MY_RF24_PA_LEVEL (RF24_PA_HIGH)

// Enable repeater functionality
//#define MY_REPEATER_FEATURE

#include <MySensors.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <Parser.h>

#define DFPLAYER_RX_PIN 8
#define DFPLAYER_TX_PIN 7
#define RELAY1 5
#define RELAY2 6
#define LED A3

// DF Player
SoftwareSerial dfPlayerSerial(DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
DFRobotDFPlayerMini dfPlayer;

// Timer
unsigned long _previousMillis = 0;
unsigned long _timeToStayAwake = 0;

// State
enum State_enum {SLEEPING, WAITING, PLAYING};
uint8_t _state = SLEEPING;
int _oldFolder = 0;
int _oldSound = 0;
int _oldVolume = 0;

// Led blink
boolean _ledOn = false;
unsigned long _previousLedChange = 0;

unsigned long _heartbeatTime = 0;

MyMessage msg(0, V_CUSTOM);
Parser parser = Parser('-');

void before()
{
  // init PIN
  pinMode(RELAY1, OUTPUT);
  digitalWrite(RELAY1, LOW);
  pinMode(RELAY2, OUTPUT);
  digitalWrite(RELAY2, LOW);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
}

void setup() {
  randomSeed(analogRead(0)); // A0
  
  // init DFPlayer
  initDfPlayer();

  // play sound
  startAmplifier();
  dfPlayer.volume(10);  //Set volume value. From 0 to 30
  dfPlayer.play(1);
  changeState(PLAYING);
}

void presentation() {
  // Send the sketch version information to the gateway and Controller
  sendSketchInfo("Sound", "2.1");

  // Present sensor to controller
  present(0, S_CUSTOM);
}

void receive(const MyMessage &myMsg)
{
  if (myMsg.type == V_CUSTOM && myMsg.sensor == 0) {
    parser.parse(myMsg.getString());

    if (parser.isEqual(0, "stop")) {
      dfPlayer.stop();
      send(msg.set(F("play stopped")));
      changeState(WAITING);
    } else if (parser.isEqual(0, "ping")) {
      // nothing to do
    } else if (parser.isEqual(0, "relay")) {
      if (parser.isEqual(1, "on")) {
        digitalWrite(RELAY2, HIGH);
        send(msg.set(F("relay on")));
      } else if (parser.isEqual(1, "off")) {
        digitalWrite(RELAY2, LOW);
        send(msg.set(F("relay off")));
      } else {
        send(msg.set(F("args error")));
      }
    } else if (parser.getInt(0) < 1 || parser.getInt(0) > 99) {
      send(msg.set(F("folder arg error")));
    } else if (parser.getInt(1) < 1 || parser.getInt(1) > 999) {
      send(msg.set(F("sound arg error")));
    } else if (parser.getInt(2) < 1 || parser.getInt(2) > 100) {
      send(msg.set(F("volume arg error")));
    } else {
      if (_state != PLAYING || _oldFolder != parser.getInt(0) || _oldSound != parser.getInt(1) 
              || _oldVolume != parser.getInt(2) || millis() - _previousMillis >= 10000) {
        // play sound
        if (_state == SLEEPING) {
          startAmplifier();
        }
        dfPlayer.volume(map(parser.getInt(2), 0, 100, 0, 30));
        dfPlayer.playFolder(parser.getInt(0), parser.getInt(1));
        _oldFolder = parser.getInt(0);
        _oldSound = parser.getInt(1);
        _oldVolume = parser.getInt(2);
        changeState(PLAYING);
      } else {
        send(msg.set(F("already playing")));
      }
    }
  }
}

void loop() {
  if (_state != SLEEPING) {
    if (millis() - _previousMillis >= _timeToStayAwake) {
      changeState(SLEEPING);
    } else if (dfPlayer.available()) { // Get DFPlayer status
      byte status = dfPlayerDetail(dfPlayer.readType(), dfPlayer.read());

      // Play finished
      if (status == 6) {
        if (_state == PLAYING) {
          send(msg.set(F("play finished")));
        }
        changeState(WAITING);
      }
    }

    // Blink led in waiting state
    if (_state == WAITING && (millis() - _previousLedChange >= 1000)) {
      _ledOn = !_ledOn;
      digitalWrite(LED, _ledOn);
      _previousLedChange = millis();
    }

    wait(1000);
  }

  manageHeartbeat();
}

void changeState(uint8_t state) {
  switch (state) {
    case PLAYING:
      _previousMillis = millis();
      _timeToStayAwake = 10 * 60000; // 10 minutes
      send(msg.set(F("play started")));
      break;
    case WAITING:
      _previousMillis = millis();
      _timeToStayAwake = 30000;
      break;
    case SLEEPING:
      stopAmplifier();
      send(msg.set(F("sleeping")));
      break;
  }

  _state = state;
}

void initDfPlayer() {
  dfPlayerSerial.begin(9600);

  if (!dfPlayer.begin(dfPlayerSerial)) {
    send(msg.set(F("Init error")));
  }

  dfPlayer.setTimeOut(500);
}

void startAmplifier() {
  digitalWrite(RELAY1, HIGH);
  digitalWrite(LED, HIGH);
}

void stopAmplifier() {
  digitalWrite(RELAY1, LOW);
  digitalWrite(LED, LOW);
}

byte dfPlayerDetail(uint8_t type, int value) {
  switch (type) {
    case TimeOut:
      send(msg.set(F("time out")));
      return 1;
    case WrongStack:
      send(msg.set(F("wrong stack")));
      return 2;
    case DFPlayerCardInserted:
      send(msg.set(F("card inserted")));
      return 3;
    case DFPlayerCardRemoved:
      send(msg.set(F("card removed")));
      return 4;
    case DFPlayerCardOnline:
      send(msg.set(F("card online")));
      return 5;
    case DFPlayerPlayFinished:
      return 6;
    case DFPlayerError:
      switch (value) {
        case Busy:
          send(msg.set(F("card not found")));
          return 7;
        case Sleeping:
          send(msg.set(F("sleeping")));
          return 8;
        case SerialWrongStack:
          send(msg.set(F("get wrong ttack")));
          return 9;
        case CheckSumNotMatch:
          send(msg.set(F("checksum not match")));
          return 10;
        case FileIndexOut:
          send(msg.set(F("file index out of bound")));
          return 11;
        case FileMismatch:
          send(msg.set(F("cannot find file")));
          return 12;
        case Advertise:
          send(msg.set(F("in advertise")));
          return 13;
        default:
          send(msg.set(F("unknown error")));
          return 14;
      }
    default:
      return 0;
  }
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
