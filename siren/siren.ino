#include <SPI.h>
#include <Mirf.h>
#include <nRF24L01.h>
#include <MirfHardwareSpiDriver.h>
#include <LowPower.h>
#include <DoxeoConfig.h>

//#define DEBUG
#include "DebugUtils.h"

#define NRF_INTERRUPT 2
#define BUZZER 6
#define SIREN 7
#define BATTERY_SENSE A0

char* nodes[] = {DOXEO_ADDR_MOTHER, DOXEO_ADDR_SOUND};
const int nbNodes = 2;
int selectedNode = 0;
byte data[32];

// Token ID
unsigned long tokenId = 0;
unsigned long tokenIdTime = 0;

// Battery sense
unsigned long batteryLastCompute = 0;
int batteryPcnt = 0;
int oldBatteryPcnt = 0;
float batteryV = 0.0;

// wake up time
unsigned long lastWakeUpTime = 0;
bool checkNewMsg = false;

// Status
bool sirenOn;

void setup() {
  // use the 1.1 V internal reference for battery sens
  analogReference(INTERNAL);
  
  // init PIN
  pinMode(NRF_INTERRUPT, INPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(SIREN, OUTPUT);
  
  digitalWrite(BUZZER, LOW);
  digitalWrite(SIREN, LOW);
  
  // init serial for debugging
  #ifdef DEBUG
    Serial.begin(9600);
    Serial.println("debug mode enable");
  #endif

  // init NRF
  Mirf.cePin = 9;
  Mirf.csnPin = 10;
  Mirf.spi = &MirfHardwareSpi; // Hardware SPI: MISO -> 12, MOSI -> 11, SCK -> 13
  Mirf.init();
  Mirf.channel = DOXEO_CHANNEL; // Choix du canal de communication (128 canaux disponibles, de 0 à 127)
  Mirf.setRADDR((byte *) DOXEO_ADDR_SIREN);
  Mirf.payload = 32; // Taille d'un data (maximum 32 octets)
  Mirf.config(); // Sauvegarde la configuration dans le module radio
  Mirf.configRegister(RF_SETUP, 0x26); // sortie 0dBm @ 250Kbs to improve distance
  Mirf.configRegister(EN_RXADDR, 0x02); // only pipe 1 can received
  Mirf.configRegister(SETUP_RETR, 0x3F);  // retry 15x
  Mirf.setTADDR((byte *) nodes[0]);
  
  if (digitalRead(NRF_INTERRUPT) == LOW) {
    Mirf.configRegister(STATUS, 0x70); // clear IRQ register
  }
  attachInterrupt(digitalPinToInterrupt(NRF_INTERRUPT), wakeUp, FALLING);

  selectedNode = 0;
  sendMessage("init done");
}


void loop() {  
  // message received  
  if (checkNewMsg && Mirf.dataReady()) {
    // read message
    byte byteMessage[32];
    Mirf.getData(byteMessage);
    String msg = String((char*) byteMessage);
    DEBUG_PRINT("message received:" + msg);

    // parse message {receptorName};{id};{message}
    String from = parseMsg(msg,';', 0);
    String receptorName = parseMsg(msg,';', 1);
    int id = parseMsg(msg, ';', 2).toInt();
    String message = parseMsg(msg, ';', 3);

    // handle message
    if (receptorName != DOXEO_ADDR_SIREN) {
      // do nothing, the message is not for us
    } else if (id == tokenId && (millis() - tokenIdTime < 60000)) {
      // already done, send success in case the previous message was not received
      sendAck(id);
    } else if (id == 0) {
      sendMessage("missing ID");
    } else if (message == "ping") {
      sendAck(id);
    } else if (message == "start") {
      sendAck(id);
      // start siren
      digitalWrite(SIREN, HIGH);
      sirenOn = true;
    } else if (message == "stop") {
      sendAck(id);
      // stop siren
      digitalWrite(SIREN, LOW);
      sirenOn = false;
    } else if (message == "battery") {
      sendAck(id);
      // send battery level
      computeBatteryLevel();
      sendBatteryLevel();
    } else {
      sendMessage("device arg error!");
    }
  } else {
    // Sleep 4S
    checkNewMsg = false;
    Mirf.powerDown();
    #ifdef DEBUG
      delay(4000);
    #else
      sleep(SLEEP_4S);
    #endif
    
    // enable reception during 30ms
    Mirf.powerUpRx();
    #ifdef DEBUG
      delay(30);
    #else
      sleep(SLEEP_120MS);
    #endif
    
    // Check battery level every 12 hours
    if (millis() > batteryLastCompute + 43200000 || millis() < batteryLastCompute) {
        batteryLastCompute = millis();
        computeBatteryLevel();
        
        if (abs(oldBatteryPcnt - batteryPcnt) > 1) {
            sendBatteryLevel();
            oldBatteryPcnt = batteryPcnt;
        }
        
        checkNewMsg = true;
    }
  }
}

int computeBatteryLevel() {
    int sensorValue = analogRead(BATTERY_SENSE);
    batteryPcnt = sensorValue / 10;
    
    // ((2M+1M)/1M)*1.1 = Vmax = 3.3 Volts
    // 3.3/1023 = Volts per bit = 0.003225806
    batteryV  = sensorValue * 0.003225806;
}

void sendBatteryLevel() {
  sendMessage("battery=" + String(batteryV) + "v" + String(batteryPcnt) + "%");
}

void sendAck(int id) {
  sendMessage(String(id) + ";success");
  tokenId = id;
  tokenIdTime = millis();
}

void sendMessage(String msg) {
  bool success;

  for (int i = 0; i < 3; ++i) {
    success = sendNrf(String(DOXEO_ADDR_SIREN) + ';' + String(DOXEO_ADDR_MOTHER) + ";" + msg);

    if (success) {
      break;
    } else {
      delay(100);
    }
  }
}

bool sendNrf(String message) {
  DEBUG_PRINT("send message: " + message);
  message.getBytes(data, 32);
  Mirf.configRegister(EN_RXADDR, 0x03); // only pipe 0 and 1 can received for ACK
  
  for (unsigned char i = 0; i < nbNodes; ++i) {
    Mirf.send(data);
    while (Mirf.isSending());

    if (Mirf.sendWithSuccess == true) {
      break;
    } else {
      // change selected node
      selectedNode = (selectedNode + 1 < nbNodes) ? selectedNode + 1 : 0;
      Mirf.setTADDR((byte *) nodes[selectedNode]);
    }
  }

  Mirf.configRegister(EN_RXADDR, 0x02); // only pipe 1 can received
  return Mirf.sendWithSuccess;
}

void sleep(period_t period) {
  if (digitalRead(NRF_INTERRUPT) == LOW) {
    Mirf.configRegister(STATUS, 0x70); // clear IRQ register
  }
  
  //attachInterrupt(digitalPinToInterrupt(NRF_INTERRUPT), wakeUp, LOW);
  LowPower.powerDown(period, ADC_OFF, BOD_OFF);
  //detachInterrupt(digitalPinToInterrupt(NRF_INTERRUPT));
}

String parseMsg(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i)==separator || i==maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }

  return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void wakeUp()
{
  checkNewMsg = true;
}
