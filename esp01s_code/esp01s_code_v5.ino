////// esp-01s code //////////////

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define led 2
OneWire dallas(0);
DallasTemperature temp(&dallas);

uint8_t nodeMCUMAC[] = {***insert your esp8266 mac address***}; // MAC del NodeMCU
bool ackReceived=false;
bool send=false;

//Varibili deep_sleep
int deep_sleep_base=10*60*1e6;                               //10min o 6min
const int deep_sleep_cycle=60*60*1e6;                        //1h

char feedback[20]={0};

struct RTCData {
  uint32_t sleepCounter;
  uint32_t checksum; // Per verificare l'integrità
};

RTCData rtcData;

// Calcola checksum semplice
uint32_t calculateChecksum(RTCData *data) {
  return data->sleepCounter ^ 0xAAAA5555; // XOR con un valore fisso
}

void readRTC() {
  if (ESP.rtcUserMemoryRead(0, (uint32_t*)&rtcData, sizeof(rtcData))) {
    if (rtcData.checksum != calculateChecksum(&rtcData)) {
      rtcData.sleepCounter = 0;
    }
  } else {
    rtcData.sleepCounter = 0;
  }
}

void writeRTC() {
  rtcData.checksum = calculateChecksum(&rtcData);
  if (!ESP.rtcUserMemoryWrite(0, (uint32_t*)&rtcData, sizeof(rtcData))) {
    Serial.println("Errore scrittura RTC!");
  }
}

// Callback per conferma invio
void onDataSent(uint8_t *mac, uint8_t sendStatus) {
  if(sendStatus==0)
    send=true;
  else
    send=false;
}

// Callback per ricezione feedback
void onDataReceive(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
    memcpy(&feedback, incomingData, len);
    scan_control();
    ackReceived=true;
}

void scan_control() {
  int sleepHours = 0;
  if (sscanf(feedback, "SLEEP_LONG_%d", &sleepHours) == 1 && sleepHours > 0){
    rtcData.sleepCounter = sleepHours;
    writeRTC();
  }
}

void setup() {

    temp.begin();

    readRTC();

    if(rtcData.sleepCounter>0){
      rtcData.sleepCounter--;
      writeRTC();
      ESP.deepSleep(deep_sleep_cycle);
      return;
    }else if (rtcData.sleepCounter == 0){
      rtcData.sleepCounter = 0;               // Imposta un valore predefinito
      writeRTC();
    }

    pinMode(led, OUTPUT);
    digitalWrite(led, HIGH);                  //il led on-board si accende quando è LOW

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != 0) return;
    digitalWrite(led, LOW);
    delay(100);
    digitalWrite(led, HIGH);

    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_add_peer(nodeMCUMAC, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataReceive);
    
    //loop
    int cont=0;
    float temperature=0.0;
    float somma=0.0;
    float old_temperature=0.0;
    while(cont<6){                            //faccio una media su 5 campioni
      cont++;
      temp.requestTemperatures();
      temperature=temp.getTempCByIndex(0);
      if(cont==1){
        somma=temperature;
        old_temperature=temperature;
      }else{
        if(temperature>=(3+old_temperature)) temperature=old_temperature;
        else old_temperature=temperature;
        somma=(somma+temperature)/2;
      }
      delay(1000);
    }
    esp_now_send(nodeMCUMAC, (uint8_t *)&somma, sizeof(somma));
    digitalWrite(led, LOW);
    delay(150);
    digitalWrite(led, HIGH);
    delay(500);
    delay(500);
    // Attende feedback
    if(ackReceived && send) {
      ackReceived=false;
      send=false;
      deep_sleep_base=10*60*1e6;
      digitalWrite(led, LOW);
      delay(150);
      digitalWrite(led, HIGH);
    }else if (!ackReceived) {
      delay(5*1000);
      bool first_try=true;
      unsigned long start_time=millis();
      unsigned long timer=millis();
      while (!ackReceived && millis()-start_time<=(60*1000)){
        if(millis()-timer>=(12*1000)||first_try){
          esp_now_send(nodeMCUMAC, (uint8_t *)&somma, sizeof(temperature));
          digitalWrite(led, LOW);
          delay(150);
          digitalWrite(led, HIGH);
          timer=millis(); 
          first_try=false;
        }
        delay(1000);
        digitalWrite(led, LOW);
        delay(1000);
        digitalWrite(led, HIGH);
        delay(1000);
        if(ackReceived) break;
      }
      if(!ackReceived)
        deep_sleep_base=6*60*1e6;
      else if(ackReceived){  
        ackReceived=false;
        deep_sleep_base=10*60*1e6;
      }
      send=false;
    }
    ESP.deepSleep(deep_sleep_base);            //in microsecondi -> moltiplicando per e6 si passa in sec -> 10min = 600e6
  }

void loop() {    
}
