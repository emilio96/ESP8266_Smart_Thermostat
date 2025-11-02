#include <Adafruit_SH110X.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <espnow.h>
#include <Adafruit_AHT10.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <LittleFS.h>

// Definizione display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
//Adafruit_var_global.ssD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

struct dati_display {
  int riga = 0;
  int riga_dx = 0;
  volatile bool display_state = true;
  const unsigned long display_temp_on = 90000;  //90000ms sono 1.5min
  volatile unsigned long display_timer = 0;
} var_display;

// Definizione sensore temperatura
Adafruit_AHT10 aht10;
struct dati_aht10 {
  sensors_event_t current_humidity, current_temperature;
  unsigned long last_temperature_sync = 0;
} var_aht;

// Pin gestione Relay
struct dati_relay {
  const int pin_relay = 15;
  bool relay = false;
  int relay_cont = 0;
  bool change_termo = true;
} var_relay;

// Variabili esp-now
uint8_t esp01s_mac[] = { ***insert your easp01s mac address*** };
struct dati_esp_now {
  const int led_esp = 2;
  char feedback[20] = { 0 };
  float receivedTemperature = 0;
  volatile bool received = false, send = false;
  bool stato_esp_now = false, reset_received_time = false, shutdown = false, set_sender_timer = true;
  unsigned long last_received_time = 0, last_send_shutdown = 0, start_send_shutdown = 0, sender_timer = 0, last_send_time = 0;
  int esp_now_cont = 0, on_time_delay = 6 * 60 * 1000;
  String shutdown_time = "20:30";
} var_esp;

// Pin encoder      // Variabili gestione Encoder
struct dati_encoder {
  const int pinS1 = 14;    // Pin A dell'encoder
  const int pinS2 = 12;    // Pin B dell'encoder
  const int pin_key = 13;  // Pin del pulsante centrale
  volatile long new_position = 0;
  volatile unsigned long last_interrupt = 0;
  volatile bool key_pressed = false;
} var_encoder;

// Definizione variabili per WiFi e HTTP
struct dati_wifi {
  String weatherURL = "";           // insert your API key in the ""
  const char *ssid = "";           // insert your ssid network in the ""
  const char *password = "";      // insert your network password in the ""
  float external_temperature = 0.0, wind_speed = 0.0, external_humidity = 0.0, feel_temperature = 0.0;
  bool stato_wifi = false;
} var_wifi;

// Definizione NTP
struct dati_ntp {
  const long utcOffsetInSeconds = 3600;    // Offset per il fuso orario (1 ora per l'Italia)
  const char *ntpServer = "pool.ntp.org";  // Server NTP
} var_ntp;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, var_ntp.ntpServer, var_ntp.utcOffsetInSeconds);

// Variabili per gestione sincronizzazione
unsigned long last_sync_millis = 0;                  // Memorizza il tempo dell'ultima sincronizzazione
const unsigned long sync_interval = 30 * 60 * 1000;  // 3600000 intervallo di sincronizzazione (1 ora in millisecondi)

// Variabili per menu
const char *menuItems[] = { "Temp Ambiente", "Modalita'", "Termosifoni", "Sincronizzazione", "Statistiche", "Indietro" };
const int num_menu_items = sizeof(menuItems) / sizeof(menuItems[0]);
const char *modes[] = { "Termostato", "Continuo", "Spento" };
struct dati_menu {
  int text_height = 10;  //Ogni 10 faccio una print
  //const char*  menuItems[] = {"Temp Target", "Mode", "Termosifoni", "Back to Home", "cose1", "cose2","cose3","cose4","cose5","cose6","cose7"};
  int current_menu_index = 0;                           // Indice della voce selezionata
  volatile bool in_sub_menu = false, in_menu = false;   // Indica se siamo in un sottomenù
  float target_temperature = 0, termo_temperature = 0;  // Temperatura target
  const float temperatura_default = 19.5, termo_default = 55;
  int current_mode_index = 0;
  volatile bool manual_sync = false;
} var_menu;

//Variabili Globali
struct dati_global {
  int refresh = 0;
  unsigned long time_on = 0, time_off = 0, tempo_on_tot = 0;
  float old_temp = 0.0, old_termo = 0.0;
  int old_mode = 0;
  float continous_limit = 20.0;
  float soglia = 18.7;
  unsigned int hh = 0, mm = 0, ss = 0;
  int current_day = timeClient.getDay();
} var_global;

// Interrupt di gestione del pulsante
void IRAM_ATTR handle_key_press() {
  unsigned long interrupt_time = millis();
  if (interrupt_time - var_encoder.last_interrupt > 300) {
    if (var_display.display_state)
      var_encoder.key_pressed = true;
    else if (!var_display.display_state) {
      //display.var_global.ssd1306_covar_global.mmand(var_global.ssD1306_DISPLAYON);                     // Riaccende il display
      Wire.beginTransmission(0x3C);  // Indirizzo del display          // Fatto mediante libreria Wire.h
      Wire.write(0x00);              // Modalità comando
      Wire.write(0xAF);              // Comando DISPLAYON
      Wire.endTransmission();
      var_display.display_state = true;
      var_display.display_timer = millis();
    }
    var_encoder.last_interrupt = interrupt_time;
  }
}

//Interrupt gestione Encoder
void IRAM_ATTR handle_rotation() {
  unsigned long interrupt_time = millis();
  if (interrupt_time - var_encoder.last_interrupt > 300) {
    if (var_display.display_state) {
      var_menu.in_menu = true;
      leggi_posizione();
      var_encoder.last_interrupt = interrupt_time;
    }
  }
}

// Callback per conferma invio !!! NON INSERIRE DELAY !!!
void onDataSent(uint8_t *mac, uint8_t sendStatus) {
  if (sendStatus == 0)
    var_esp.send = true;
  else
    var_esp.send = false;
  Serial.print("------->Sono in send!<------- ");
  Serial.println(var_esp.send);
}

// Callback per ricezione dati !!! NON INSERIRE DELAY !!!
void onDataReceive(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
  if (!var_esp.received) {
    memcpy(&var_esp.receivedTemperature, incomingData, sizeof(var_esp.receivedTemperature));
    var_esp.received = true;
    potenza_segnale();
    // Invia feedback al trasmettitore
    //if (!var_esp.shutdown)  //Poichè ho disattivato la callback in send_shutdown_esp potrei anche togliere questo if
    strcpy(var_esp.feedback, "ACK");
    esp_now_send(mac, (uint8_t *)var_esp.feedback, strlen(var_esp.feedback));
  }
}

void potenza_segnale() {
  unsigned long rssi = WiFi.RSSI();
  Serial.print("Signal Strength (RSSI): ");
  Serial.print(rssi);
  Serial.println(" dBm");
}

void leggi_posizione() {
  bool s1_state = digitalRead(var_encoder.pinS1);
  bool s2_state = digitalRead(var_encoder.pinS2);
  if ((digitalRead(var_encoder.pinS1) && digitalRead(var_encoder.pinS2)) || (!digitalRead(var_encoder.pinS1) && digitalRead(var_encoder.pinS2)))
    var_encoder.new_position--;
  else if ((!digitalRead(var_encoder.pinS2) && !digitalRead(var_encoder.pinS1)) || (!digitalRead(var_encoder.pinS2) && digitalRead(var_encoder.pinS1)))
    var_encoder.new_position++;
}

void read_sensor_data() {
  if (millis() - var_aht.last_temperature_sync >= 10000) {
    aht10.getEvent(&var_aht.current_humidity, &var_aht.current_temperature);
    var_aht.current_temperature.temperature = round(var_aht.current_temperature.temperature * 10) / 10;
    var_aht.current_humidity.relative_humidity = round(var_aht.current_humidity.relative_humidity * 10) / 10;
    var_aht.last_temperature_sync = millis();
  }
}

void time_temp() {
  if (millis() - last_sync_millis >= sync_interval || var_menu.manual_sync) {
    bool riattiva = false;
    if (var_esp.stato_esp_now) {
      bool riattiva = true;
      esp_now_deinit();
    }
    if (!var_wifi.stato_wifi)
      connetti_wifi();
    if (!var_wifi.stato_wifi){
      Serial.println("ERRORE CONNESSIONE WIFI");
      if (riattiva)
        start_esp_now();
      return;
    }
    Serial.println("Sincronizzazione NTP...");
    timeClient.update();
    fetchexternal_temperature();
    last_sync_millis = millis();  // Aggiorna il tempo dell'ultima sincronizzazione
    spegni_wifi();
    if (riattiva)
      start_esp_now();
  }
}

void fetchexternal_temperature() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.setTimeout(3000);
    WiFiClient client;

    Serial.println("Inizio richiesta HTTP...");
    // Inizializza la richiesta
    http.begin(client, var_wifi.weatherURL);
    // Aggiungi gli header necevar_global.ssari
    http.addHeader("User-Agent", "ESP8266");
    // Esegui la richiesta
    int httpCode = http.GET();
    Serial.print("HTTP Code: ");
    Serial.println(httpCode);

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      String tempString = " ";
      Serial.println("Payload ricevuto:");
      if (payload.length() > 0) {
        Serial.println(payload);  // Stampa per debugging
        // Cerca il campo "temp" nel payload
        int tempIndex = payload.indexOf("\"temp\":");
        if (tempIndex != -1) {
          int startIndex = tempIndex + 7;                   // 7 caratteri per saltare "\"temp\":"
          int endIndex = payload.indexOf(",", startIndex);  // Trova la fine del valore
          tempString = payload.substring(startIndex, endIndex);
          var_wifi.external_temperature = tempString.toFloat();
          var_wifi.external_temperature = round(var_wifi.external_temperature * 10) / 10;
          Serial.print("Temperatura esterna: ");
          Serial.print(var_wifi.external_temperature);
          Serial.println(" C");
        }
        tempIndex = payload.indexOf("\"feels_like\":");
        if (tempIndex != -1) {
          int startIndex = tempIndex + 13;                  // 13 caratteri per saltare "\"feels_like\":"
          int endIndex = payload.indexOf(",", startIndex);  // Trova la fine del valore
          tempString = payload.substring(startIndex, endIndex);
          var_wifi.feel_temperature = tempString.toFloat();
          var_wifi.feel_temperature = round(var_wifi.feel_temperature * 10) / 10;
          Serial.print("Temperatura percepita: ");
          Serial.print(var_wifi.feel_temperature);
          Serial.println(" C");
        }
        tempIndex = payload.indexOf("\"humidity\":");
        if (tempIndex != -1) {
          int startIndex = tempIndex + 11;                  // 11 caratteri per saltare "\"humidity\":"
          int endIndex = payload.indexOf(",", startIndex);  // Trova la fine del valore
          tempString = payload.substring(startIndex, endIndex);
          var_wifi.external_humidity = tempString.toFloat();
          var_wifi.external_humidity = round(var_wifi.external_humidity * 10) / 10;
          Serial.print("Umidita' esterna: ");
          Serial.print(var_wifi.external_humidity);
          Serial.println(" %");
        }
        tempIndex = payload.indexOf("\"speed\":");
        if (tempIndex != -1) {
          int startIndex = tempIndex + 8;                   // 8 caratteri per saltare "\"speed\":"
          int endIndex = payload.indexOf(",", startIndex);  // Trova la fine del valore
          tempString = payload.substring(startIndex, endIndex);
          var_wifi.wind_speed = tempString.toFloat();
          var_wifi.wind_speed = round(var_wifi.wind_speed * 10) / 10;
          Serial.print("Velocita' vento: ");
          Serial.print(var_wifi.wind_speed);
          Serial.println(" m/s");
        }
      } else {
        Serial.println("Payload vuoto!");
      }
    } else {
      int retry = 0;
      Serial.print("Errore HTTP: ");
      Serial.println(httpCode);
      http.end();       //Chiudo la connessione creata sopra prima di avviarne un'altra
      while (httpCode != HTTP_CODE_OK && retry < 3) {
        Serial.println("Sono nel while per le api temperatura esterna");
        fetchexternal_temperature_retry();
        retry++;
      }
      if (retry != 0) retry = 0;
    }
    http.end();
  } else {
    Serial.println("Wi-Fi non connesso!");
  }
}

void fetchexternal_temperature_retry() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.setTimeout(3000);
    WiFiClient client;

    Serial.println("Inizio richiesta HTTP...");
    // Inizializza la richiesta
    http.begin(client, var_wifi.weatherURL);
    // Aggiungi gli header necevar_global.ssari
    http.addHeader("User-Agent", "ESP8266");
    // Esegui la richiesta
    int httpCode = http.GET();
    Serial.print("HTTP Code: ");
    Serial.println(httpCode);

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      String tempString = " ";
      Serial.println("Payload ricevuto:");
      if (payload.length() > 0) {
        Serial.println(payload);  // Stampa per debugging
        // Cerca il campo "temp" nel payload
        int tempIndex = payload.indexOf("\"temp\":");
        if (tempIndex != -1) {
          int startIndex = tempIndex + 7;                   // 7 caratteri per saltare "\"temp\":"
          int endIndex = payload.indexOf(",", startIndex);  // Trova la fine del valore
          tempString = payload.substring(startIndex, endIndex);
          var_wifi.external_temperature = tempString.toFloat();
          var_wifi.external_temperature = round(var_wifi.external_temperature * 10) / 10;
          Serial.print("Temperatura esterna: ");
          Serial.print(var_wifi.external_temperature);
          Serial.println(" C");
        }
        tempIndex = payload.indexOf("\"feels_like\":");
        if (tempIndex != -1) {
          int startIndex = tempIndex + 13;                  // 13 caratteri per saltare "\"feels_like\":"
          int endIndex = payload.indexOf(",", startIndex);  // Trova la fine del valore
          tempString = payload.substring(startIndex, endIndex);
          var_wifi.feel_temperature = tempString.toFloat();
          var_wifi.feel_temperature = round(var_wifi.feel_temperature * 10) / 10;
          Serial.print("Temperatura percepita: ");
          Serial.print(var_wifi.feel_temperature);
          Serial.println(" C");
        }
        tempIndex = payload.indexOf("\"humidity\":");
        if (tempIndex != -1) {
          int startIndex = tempIndex + 11;                  // 11 caratteri per saltare "\"humidity\":"
          int endIndex = payload.indexOf(",", startIndex);  // Trova la fine del valore
          tempString = payload.substring(startIndex, endIndex);
          var_wifi.external_humidity = tempString.toFloat();
          var_wifi.external_humidity = round(var_wifi.external_humidity * 10) / 10;
          Serial.print("Umidita' esterna: ");
          Serial.print(var_wifi.external_humidity);
          Serial.println(" %");
        }
        tempIndex = payload.indexOf("\"speed\":");
        if (tempIndex != -1) {
          int startIndex = tempIndex + 8;                   // 8 caratteri per saltare "\"speed\":"
          int endIndex = payload.indexOf(",", startIndex);  // Trova la fine del valore
          tempString = payload.substring(startIndex, endIndex);
          var_wifi.wind_speed = tempString.toFloat();
          var_wifi.wind_speed = round(var_wifi.wind_speed * 10) / 10;
          Serial.print("Velocita' vento: ");
          Serial.print(var_wifi.wind_speed);
          Serial.println(" m/s");
        }
      } else {
        Serial.println("Payload vuoto!");
      }
    } else {
      http.end();
      return;
    }
    http.end();
  } else {
    Serial.println("Wi-Fi non connesso!");
  }
}

void calculate_shutdown_esp() {

  int current_hh = timeClient.getFormattedTime().substring(0, 2).toInt();
  int current_mm = timeClient.getFormattedTime().substring(3, 5).toInt();
  int target_hh = 8;
  int target_mm = 30;

  int current_tot_mm = current_hh * 60 + current_mm;
  int target_tot_mm = target_hh * 60 + target_mm;

  int sleep_mm = target_tot_mm - current_tot_mm;
  if (sleep_mm < 0)  //Gestisce il cambio giorno
    sleep_mm += 24 * 60;
  int sleep_hh = sleep_mm / 60;

  snprintf(var_esp.feedback, sizeof(var_esp.feedback), "SLEEP_LONG_%d", sleep_hh);

  var_esp.shutdown = true;
  var_esp.start_send_shutdown = millis();

  Serial.print("Shutdown attivato! ");
  Serial.print(var_esp.feedback);
  Serial.print("--->");
  Serial.println(var_esp.shutdown);
}

void stampa_main() {
  var_menu.in_menu = false;
  display.clearDisplay();
  display.setCursor(var_display.riga, 0);
  display.print(timeClient.getFormattedTime().substring(0, 5));
  display.setCursor(0, 0);
  display.print(var_wifi.external_temperature, 1);
  display.print(" C");
  display.setCursor(var_display.riga_dx, 0);
  if (var_relay.relay)
    display.print("ON");
  else if (!var_relay.relay)
    display.print("OFF");
  display.setCursor((var_display.riga_dx - 20), 0);
  if (var_wifi.stato_wifi)
    display.print("...");
  if (!var_wifi.stato_wifi && !var_esp.stato_esp_now)
    display.print(".");
  if (var_esp.stato_esp_now)
    display.print("..");
  //read_sensor_data();
  display.setCursor(0, 2 * var_menu.text_height);
  display.println("-^-");
  display.print("|_|");
  display.setCursor(25, 2 * var_menu.text_height);
  display.print(var_aht.current_temperature.temperature, 1);
  display.print(" C");
  display.setCursor(25, 3 * var_menu.text_height);
  display.print(var_aht.current_humidity.relative_humidity, 1);
  display.print(" %");
  display.setCursor(75, 2.5 * var_menu.text_height);
  if (var_menu.current_mode_index == 0)
    display.print("Mode: T");
  else if (var_menu.current_mode_index == 1)
    display.print("Mode: C");
  else if (var_menu.current_mode_index == 2)
    display.print("Mode:OFF");
  display.setCursor(0, 5 * var_menu.text_height);
  if (var_menu.current_mode_index == 0 || var_menu.current_mode_index == 2) {
    display.print("T Impostata: ");
    display.print(var_menu.target_temperature, 1);
    display.print(" C");
  } else if (var_menu.current_mode_index == 1) {
    display.print("T Impostata: ");
    display.print(var_menu.termo_temperature, 1);
    display.print(" C");
  }
  display.display();
}

void draw_menu() {
  display.clearDisplay();
  // La funzione così scritta serve per stampare il max n. di elementi per
  // ogni pagina del menù. Risolvere bug di selezione pavar_global.ssaggio ultimo/primo elemento.
  int max_raw = int(int(SCREEN_HEIGHT) / var_menu.text_height);
  int index = 0;
  if (num_menu_items > max_raw)
    index = max_raw;
  else
    index = num_menu_items;

  if (var_menu.current_menu_index <= max_raw) {
    for (int i = 0; i < index; i++) {
      if (i == var_menu.current_menu_index) {
        display.setCursor(0, i * 10);
        display.print("> ");  // Evidenzia la voce selezionata
      } else {
        display.setCursor(var_menu.text_height, i * 10);
      }
      display.print(menuItems[i]);
    }
    display.display();
  } else {
    index = num_menu_items - max_raw;
    for (int i = 0; i < index; i++) {
      if ((i + max_raw) == var_menu.current_menu_index) {
        display.setCursor(0, i * 10);
        display.print("> ");  // Evidenzia la voce selezionata
      } else {
        display.setCursor(var_menu.text_height, i * 10);
      }
      display.print(menuItems[i + max_raw]);
    }
    display.display();
  }
}

void draw_sub_menu() {
  display.clearDisplay();
  if (var_menu.current_menu_index == 0) {  // Sottomenù per "Temp Target"
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print("Set Temp:");
    display.setCursor(0, 20);
    display.print(var_menu.target_temperature, 1);
    display.print(" C");
    display.setTextSize(1);
  } else if (var_menu.current_menu_index == 1) {  // Sottomenù per "Mode"
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print("Mode:");
    display.setCursor(0, 20);
    display.print(modes[var_menu.current_mode_index]);
    display.setTextSize(1);
  } else if (var_menu.current_menu_index == 2) {  // Sottomenù per "Termosifoni"
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print("Set Temp:");
    display.setCursor(0, 20);
    display.print(var_menu.termo_temperature, 1);
    display.print(" C");
    display.setTextSize(1);
  } else if (var_menu.current_menu_index == 3) {  //  Sottomenù per "Sincronizzazione"
    manual_syncronization();
    return;
  } else if (var_menu.current_menu_index == 4) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Termpo accensione:");
    display.setCursor(0, 10);
    display.print(var_global.hh);
    display.print(":");
    display.print(var_global.mm);
    display.print(":");
    display.print(var_global.ss);
    if (var_menu.current_mode_index == 1) {
      display.setCursor(0, 40);
      display.print("esp_now_cont: ");
      display.println(var_esp.esp_now_cont);
      display.print("T. soglia: ");
      display.print(var_global.soglia, 1);
      display.print(" C");
      if (var_esp.esp_now_cont > 0) {
        int tempo = (millis() - var_esp.last_received_time) / (1000 * 60);
        display.setCursor(0, 20);
        display.print("Temp Termo: ");
        display.print(tempo);
        display.println("mm fa:");
        display.print(var_esp.receivedTemperature);
        display.print(" C");
      }
    }
  }
  display.display();
}

void manual_syncronization() {
  if (millis() - last_sync_millis <= (10 * 60 * 1000)) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Ultima sincronizzazione: ");
    display.print(String((millis() - last_sync_millis) / (60 * 1000)));
    display.print("min fa.");
    display.display();
    delay(2000);
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Temperatura: ");
    display.print(var_wifi.external_temperature, 1);
    display.print(" C");
    display.setCursor(0, var_menu.text_height);
    display.print("Percepita: ");
    display.print(var_wifi.feel_temperature, 1);
    display.print(" C");
    display.setCursor(0, 2 * var_menu.text_height);
    display.print("Umidita': ");
    display.print(var_wifi.external_humidity, 1);
    display.print(" %");
    display.setCursor(0, 3 * var_menu.text_height);
    display.print("Vento: ");
    display.print(var_wifi.wind_speed, 1);
    display.print(" m/s");
    display.setCursor(0, 5 * var_menu.text_height);
    if (var_wifi.stato_wifi)
      display.print("Wifi ON");
    else if (!var_wifi.stato_wifi)
      display.print("Wifi OFF");
    display.display();
    delay(3000);
  } else {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Stato WiFi: ");
    if (var_wifi.stato_wifi)
      display.print("ON");
    else if (!var_wifi.stato_wifi)
      display.print("OFF");
    display.display();
    delay(1000);
    var_menu.manual_sync = true;
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Sto sincronizzando...");
    display.display();
    //connetti_wifi();
    time_temp();
    /*display.clearDisplay();
    display.setCursor(0,0);
    display.println("Connesso!")&display.print(WiFi.localIP());
    display.display();*/
    delay(1500);
    var_menu.manual_sync = false;
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Sincronizzacione eseguita con successo!");
    display.display();
    delay(1500);
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Temp: ");
    display.print(var_wifi.external_temperature, 1);
    display.print(" C");
    display.setCursor(0, var_menu.text_height);
    display.print("Percepita: ");
    display.print(var_wifi.feel_temperature, 1);
    display.print(" C");
    display.setCursor(0, 2 * var_menu.text_height);
    display.print("Umidita': ");
    display.print(var_wifi.external_humidity, 1);
    display.print(" %");
    display.setCursor(0, 3 * var_menu.text_height);
    display.print("Vento: ");
    display.print(var_wifi.wind_speed, 1);
    display.print(" m/s");
    display.setCursor(0, 5 * var_menu.text_height);
    if (var_wifi.stato_wifi)
      display.print("Wifi ON");
    else if (!var_wifi.stato_wifi)
      display.print("Wifi OFF");
    display.display();
    delay(3000);
  }
}

void handle_menu_selection(unsigned long timer) {
  while (!var_encoder.key_pressed && (millis() - timer < (1.5 * 60 * 1000))) {
    if (var_menu.current_menu_index == 0) {  // Temp Target
      if (var_encoder.new_position > 0)
        var_menu.target_temperature += 0.1;
      else if (var_encoder.new_position < 0)
        var_menu.target_temperature -= 0.1;
      var_encoder.new_position = 0;  // Resetta la posizione dell'encoder
      draw_sub_menu();
    } else if (var_menu.current_menu_index == 1) {  // Mode
      if (var_encoder.new_position > 0)
        var_menu.current_mode_index = abs(var_menu.current_mode_index + 1) % (sizeof(modes) / sizeof(modes[0]));  // Scorre tra Auto e Manual
      else if (var_encoder.new_position < 0)
        var_menu.current_mode_index = abs(var_menu.current_mode_index - 1) % (sizeof(modes) / sizeof(modes[0]));  // Torna indietro
      var_encoder.new_position = 0;
      draw_sub_menu();
    } else if (var_menu.current_menu_index == 2) {  // Termosifoni
      if (var_encoder.new_position > 0)
        var_menu.termo_temperature += var_encoder.new_position * 1;
      else if (var_encoder.new_position < 0)
        var_menu.termo_temperature += var_encoder.new_position * 1;
      var_encoder.new_position = 0;
      draw_sub_menu();
    } else if (var_menu.current_menu_index == 3 || var_menu.current_menu_index == 4)  //Sincronizzazione
      break;
  }
  if (var_menu.target_temperature != var_global.old_temp || var_menu.current_mode_index != var_global.old_mode || var_menu.termo_temperature != var_global.old_termo) {
    save_settings();
    if (var_menu.current_mode_index != var_global.old_mode)
      var_esp.reset_received_time = true;
    if (timeClient.getFormattedTime().substring(0, 5) >= var_esp.shutdown_time && var_global.old_mode == 1 && var_menu.current_mode_index != 1) {
      if (!var_esp.shutdown && var_esp.esp_now_cont > 0)
        calculate_shutdown_esp();
    }
    var_global.old_temp = var_menu.target_temperature;
    var_global.old_mode = var_menu.current_mode_index;
    var_global.old_termo = var_menu.termo_temperature;
    Serial.println("Ho appena salvato i parametri!");
  }
}

void accendi_relay() {
  if (!var_relay.relay) {
    digitalWrite(var_relay.pin_relay, HIGH);
    var_relay.relay = true;
    var_relay.relay_cont++;
    var_global.time_on = millis();
    //delay(50);
  }
}

void spegni_relay() {
  if (var_relay.relay) {
    digitalWrite(var_relay.pin_relay, LOW);
    var_relay.relay = false;
    var_global.time_off = millis();
    //delay(50);
  }
}

void gestione_relay() {
  if (var_menu.current_mode_index == 0) {
    var_esp.esp_now_cont = 0;
    if (var_esp.stato_esp_now && !var_esp.shutdown)
      spegni_wifi();
    if (var_aht.current_temperature.temperature <= (var_menu.target_temperature - 0.2))
      accendi_relay();
    else if (var_aht.current_temperature.temperature >= (var_menu.target_temperature + 0.2))
      spegni_relay();
  } else if (var_menu.current_mode_index == 1) {  //Modalità continua
    if (var_esp.esp_now_cont == 0) {
      //gestione_esp_now();
      if (var_wifi.feel_temperature <= 0) 
        var_global.soglia = 19;
      else if (var_wifi.feel_temperature > 3)
        var_global.soglia = 18.7;
      if (var_aht.current_temperature.temperature <= (var_global.soglia - 0.2))
        accendi_relay();
      else if (var_aht.current_temperature.temperature > var_global.soglia)
        spegni_relay();
      return;
    }
    if (var_aht.current_temperature.temperature >= var_global.continous_limit) {
      spegni_relay();
      return;
    } else if (var_esp.esp_now_cont > 0 && var_esp.receivedTemperature >= 13 && var_esp.receivedTemperature <= 70) {
      if (var_wifi.feel_temperature <= 0 && var_relay.change_termo) {
        var_relay.change_termo = false;
        var_global.old_termo = var_menu.termo_temperature;
        var_menu.termo_temperature = 60;
        var_global.soglia = 19;
      } else if (var_wifi.feel_temperature > 3) {
        var_menu.termo_temperature = var_global.old_termo;
        var_global.soglia = 18.7;
        var_relay.change_termo = true;
      }
      if (var_aht.current_temperature.temperature >= var_global.soglia) {
        if (var_esp.receivedTemperature <= var_menu.termo_temperature - 10) {
          //Da valutare funzionamento
          if (!var_relay.relay && var_aht.current_temperature.temperature >= (var_global.continous_limit - 0.2))
            return;
          else
            accendi_relay();
        } else if (var_esp.receivedTemperature >= var_menu.termo_temperature)
          spegni_relay();
      } else if (var_aht.current_temperature.temperature < var_global.soglia)
        accendi_relay();
    } else if (var_esp.esp_now_cont > 0 && var_esp.receivedTemperature < 13 && var_esp.receivedTemperature > 70) {
      var_menu.current_mode_index = 0;  //Passa alla modalità termostato
      var_esp.reset_received_time = true;
      print_errore(0);
    }
  } else if (var_menu.current_mode_index == 2) {  //Spento
    var_esp.esp_now_cont = 0;
    if (var_esp.stato_esp_now && !var_esp.shutdown)
      spegni_wifi();
    spegni_relay();
  }
}

void print_errore(int errore) {
  if (!var_display.display_state) {
    Wire.beginTransmission(0x3C);  // Indirizzo del display          // Fatto mediante libreria Wire.h
    Wire.write(0x00);              // Modalità comando
    Wire.write(0xAF);              // Comando DISPLAYON
    Wire.endTransmission();
    var_display.display_state = true;
    var_display.display_timer = millis();
  }
  if (errore == 0) {
    digitalWrite(var_esp.led_esp, LOW);
    Serial.println("Errore nella lettura termosifone o danno!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("ERRORE!");
    display.println("Sensore termosifone!");
    delay(15 * 1000);
    digitalWrite(var_esp.led_esp, HIGH);
  } else if (errore == 1) {
    digitalWrite(var_esp.led_esp, LOW);
    Serial.println("ERRORE! Nessun dato arrivato! Attivo termostato");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("ERRORE!");
    display.println("Nessun dato arrivato!");
    display.println("Attivo termostato");
    display.display();
    delay(15 * 1000);
    digitalWrite(var_esp.led_esp, HIGH);
  }
  display.clearDisplay();
  display.display();
}

void spegni_wifi() {
  WiFi.disconnect(true);
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);
  delay(50);
  var_wifi.stato_wifi = false;
  var_esp.stato_esp_now = false;
}

void connetti_wifi() {
  if (WiFi.getMode() == 0)
    WiFi.mode(WIFI_STA);
  WiFi.begin(var_wifi.ssid, var_wifi.password);
  unsigned long wifi_no_block = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifi_no_block <= (1.5 * 60 * 1000))
    delay(500);
  if (WiFi.status() == WL_CONNECTED)
    var_wifi.stato_wifi = true;
}

void start_esp_now() {
  if (var_wifi.stato_wifi) {
    WiFi.disconnect(true);
    var_wifi.stato_wifi = false;
  }
  if (WiFi.getMode() == 0)
    WiFi.mode(WIFI_STA);
  wifi_set_channel(1);                  //Setto esplicitamente il canale di comunicazione
  if (esp_now_init() != 0)
    Serial.println("ESP-NOW init failed");
  else {
    Serial.println("ESP-NOW init done");
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_add_peer(esp01s_mac, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
    esp_now_register_recv_cb(onDataReceive);
    esp_now_register_send_cb(onDataSent);
    var_esp.stato_esp_now = true;
    //delay(100);
  }
}

void load_settings() {
  File file = LittleFS.open("/settings.txt", "r");
  if (!file) {
    var_menu.target_temperature = var_menu.temperatura_default;
    var_menu.termo_temperature = var_menu.termo_default;
    var_menu.current_mode_index = 3;
    return;
  }
  var_menu.target_temperature = file.readStringUntil('\n').toFloat();
  var_menu.termo_temperature = file.readStringUntil('\n').toFloat();
  var_menu.current_mode_index = file.readStringUntil('\n').toFloat();
  file.close();
}

void save_settings() {
  File file = LittleFS.open("/settings.txt", "w");
  if (!file)
    return;
  file.println(var_menu.target_temperature);
  file.println(var_menu.termo_temperature);
  file.println(var_menu.current_mode_index);
  file.close();
}

void spegnimento_display() {
  if (var_display.display_state) {     //Eseguo la funzione solo se lo schermo è acceso
    if (millis() - var_display.display_timer >= var_display.display_temp_on) {
      if (var_menu.in_sub_menu) {      //Se sono nei menù torno alla pagina principale e ritardo lo spegnimento al prossimo controllo
        var_menu.in_sub_menu = false;
        stampa_main();
        var_display.display_timer = millis() + (var_display.display_temp_on * 0.5);  //In questo modo, tornato alla home, resta acceso ancora per metà tempo
        return;
      } else if (var_menu.in_menu) {
        stampa_main();
        var_display.display_timer = millis() + (var_display.display_temp_on * 0.5);
        return;
      } else if (!var_menu.in_sub_menu && !var_menu.in_menu) {  //Se sono nella schermata principale
        //display.var_global.ssd1306_covar_global.mmand(var_global.ssD1306_DISPLAYOFF);            //Spegne il display
        Wire.beginTransmission(0x3C);  // Indirizzo del display    //Fatto mediante libreria Wire.h
        Wire.write(0x00);              // Modalità comando
        Wire.write(0xAE);              // Comando DISPLAYOFF
        Wire.endTransmission();
        var_display.display_state = false;
      }
    }
  }
}

void rapid_off() {
  if (!digitalRead(var_encoder.pin_key) && !var_menu.in_menu && !var_menu.in_sub_menu) {
    Serial.println("Il tasto è premuto");
    unsigned long conta = 0;  //Spegnimento rapido da main_screen
    if (var_display.display_state && !var_menu.in_menu && !var_menu.in_sub_menu) {
      conta = millis();
      while (!digitalRead(var_encoder.pin_key)) {
        delay(50);
        conta = millis() - conta;
      }
      Serial.print("Il tasto è stato premuto per: ");
      Serial.print(conta);
      Serial.println("ms");
    }
    if (conta >= 1500 && var_menu.current_mode_index == 1) {  //E' mediamente sempre di più perchè è da considerare il delay(1000) nel void loop()
      var_menu.current_mode_index = 2;
      var_esp.reset_received_time = true;
      save_settings();
      Serial.println("Ho appena salvato i parametri!");
      if (timeClient.getFormattedTime().substring(0, 5) >= var_esp.shutdown_time && !var_esp.shutdown) {
        calculate_shutdown_esp();
      }
    } else if (conta >= 1500) {
      var_menu.current_mode_index = 2;
      save_settings();
      Serial.println("Ho appena salvato i parametri!");
    } else
      conta = 0;
  }
}

void gestione_esp_now() {
  if (var_menu.current_mode_index == 1) {
    if (var_esp.esp_now_cont == 0 && var_esp.reset_received_time) {
      var_esp.last_received_time = millis();
      var_esp.reset_received_time = false;
    }
    if ((millis() - var_esp.last_received_time >= var_esp.on_time_delay && millis() - var_esp.last_received_time <= (25 * 60 * 1000)) || var_esp.esp_now_cont == 0 || var_esp.received) {  //Ogni 6 minuti riaccendo esp-now
      if (var_esp.shutdown) {
        var_esp.shutdown = false;
        spegni_wifi();  //Devo riavviare il wifi perchè per mandare lo shutdown de-registro la funzione di ricezione
        start_esp_now();
      }
      if (!var_esp.stato_esp_now)
        start_esp_now();
      if (var_esp.received) {
        if (!var_esp.send) {
          if (var_esp.set_sender_timer) {
            var_esp.sender_timer = millis();
            var_esp.set_sender_timer = false;
          }
          if (millis() - var_esp.sender_timer <= (80 * 1000) && millis() - var_esp.last_send_time >= (20 * 1000)) {
            for (int cont = 0; cont < 3; cont++) {
              if (!var_esp.send)
                esp_now_send(esp01s_mac, (uint8_t *)var_esp.feedback, strlen(var_esp.feedback));
              else if (var_esp.send) break;
              delay(150);
            }
            var_esp.last_send_time = millis();
          } else if (millis() - var_esp.sender_timer > (80 * 1000)) {
            var_esp.on_time_delay = 4.5 * 60 * 1000;
            var_esp.send = true;
          }
        } //else if (var_esp.send) {
        if (var_esp.send) {
          if (millis() - var_esp.sender_timer <= (90 * 1000))
            var_esp.on_time_delay = 6 * 60 * 1000;
          Serial.print("Prossima riattivazione esp_now tra: ");
          Serial.println(var_esp.on_time_delay);
          var_esp.last_received_time = millis();
          var_esp.esp_now_cont++;
          digitalWrite(var_esp.led_esp, LOW);
          delay(150);
          digitalWrite(var_esp.led_esp, HIGH);
          Serial.print("Temperatura ricevuta: ");
          Serial.println(var_esp.receivedTemperature);
          Serial.print("Contatore var_esp.esp_now_cont: ");
          Serial.println(var_esp.esp_now_cont);
          var_esp.received = false;
          var_esp.send = false;
          var_esp.set_sender_timer = true;
          spegni_wifi();
        }
      }
    } //else if (millis() - var_esp.last_received_time > (25 * 60 * 1000) && !var_esp.received) {  //Se sono pavar_global.ssati 25 minuti senza ricevere niente lo considero un errore
    if (millis() - var_esp.last_received_time > (25 * 60 * 1000) && !var_esp.received) {  
      spegni_wifi();
      var_menu.current_mode_index = 0;
      var_esp.reset_received_time = true;
      print_errore(1);
    } else if (millis() - var_esp.last_received_time > (25 * 60 * 1000) && var_esp.received) {
      digitalWrite(var_esp.led_esp, LOW);
      delay(150);
      digitalWrite(var_esp.led_esp, HIGH);
      var_esp.esp_now_cont++;
      var_esp.last_received_time = millis();
      var_esp.received = false;
      spegni_wifi();
    }
  }
}

void send_shutdown_esp() {
  if (millis() - var_esp.last_send_shutdown >= (15 * 1000) && var_esp.shutdown && millis() - var_esp.start_send_shutdown <= (15 * 60 * 1000)) {
    var_esp.last_send_shutdown = millis();
    if (!var_esp.stato_esp_now)
      start_esp_now();
    esp_now_unregister_recv_cb();
    if (!var_esp.send) {
      for (int cont = 0; cont < 3; cont++) {
        if (!var_esp.send)
          esp_now_send(esp01s_mac, (uint8_t *)var_esp.feedback, strlen(var_esp.feedback));
        else if (var_esp.send) break;
        delay(150);
      }
      Serial.print("received: ");
      Serial.print(var_esp.received);
      Serial.print("     send: ");
      Serial.println(var_esp.send);
    } 
    if (var_esp.send) {
      var_esp.shutdown = false;
      if (var_esp.received) var_esp.received = false;
      var_esp.send = false;
      spegni_wifi();
    }
  } else if ((millis() - var_esp.start_send_shutdown > (15 * 60 * 1000) && var_esp.shutdown)) {
    var_esp.shutdown = false;
    if (var_esp.received) var_esp.received = false;
    if (var_esp.send) var_esp.send = false;
    spegni_wifi();
  }
}

void calcolo_statistiche() {
  if (!var_relay.relay) {
    if (var_global.time_off > var_global.time_on) {
      unsigned long differenza = var_global.time_off - var_global.time_on;
      var_global.tempo_on_tot = var_global.tempo_on_tot + differenza;  //esprevar_global.sso in millisecondi
      var_global.time_off = 0;
      var_global.time_on = 0;
    }
  }
  unsigned long total_ss = var_global.tempo_on_tot / 1000;
  var_global.hh = total_ss / 3600;
  var_global.mm = (total_ss % 3600) / 60;
  var_global.ss = total_ss % 60;
}

void reset_variabili() {
  if (timeClient.getFormattedTime().substring(0, 2) == "00" && timeClient.getFormattedTime().substring(3, 5) >= "30" && var_global.current_day != timeClient.getDay()) {
    var_global.tempo_on_tot = 0;
    var_esp.esp_now_cont = 0;
    var_relay.relay_cont = 0;
    var_esp.last_send_time = 0;
    var_relay.change_termo = true;
    var_esp.shutdown = false;
    var_esp.set_sender_timer = true;
    var_global.current_day = timeClient.getDay();
  }
}

void estimated_morning_temperature() {
}

void setup() {
  Serial.begin(9600);
  // Inizializzazione display
  //if (!display.begin(var_global.ssD1306_SWITCHCAPVCC, 0x3C)) {
  if (!display.begin(0x3C, true)) {
    Serial.println("Errore display");
    while (true)
      ;
  }
  display.display();  //mostra logo Adafruit
  delay(500);
  display.clearDisplay();

  // Configurazione var_esp.led_esp
  pinMode(var_esp.led_esp, OUTPUT);
  digitalWrite(var_esp.led_esp, HIGH);  //Pin internal_led funziona con logica inversa

  // Inizializzazione WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(var_wifi.ssid, var_wifi.password);
  unsigned long wifi_no_block = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifi_no_block <= (1.5 * 60 * 1000)) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED)
    var_wifi.stato_wifi = true;
  else
    var_wifi.stato_wifi = false;
  //display.setTextColor(var_global.ssD1306_WHITE);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  if (var_wifi.stato_wifi) {
    display.println("Connesso a Wi-Fi! ") & display.print(WiFi.localIP());
    display.display();
  } else {
    display.print("FAIL 2 connect!");
    display.display();
  }
  delay(2000);

  // Configurazione pin relay
  pinMode(var_relay.pin_relay, OUTPUT);
  digitalWrite(var_relay.pin_relay, LOW);
  var_relay.relay = false;

  // Configurazione Encoder/Pulsante
  pinMode(var_encoder.pinS1, INPUT_PULLUP);
  pinMode(var_encoder.pinS2, INPUT_PULLUP);
  pinMode(var_encoder.pin_key, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(var_encoder.pin_key), handle_key_press, FALLING);
  attachInterrupt(digitalPinToInterrupt(var_encoder.pinS1), handle_rotation, CHANGE);

  // Inizializza il sensore AHT103 e faccio prima lettura
  if (!aht10.begin()) {
    Serial.println("AHT10 non trovato!");
    while (true) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.setTextSize(2);
      display.print("NO AHT10");
      display.display();
    }
  }
  aht10.getEvent(&var_aht.current_humidity, &var_aht.current_temperature);
  var_aht.current_temperature.temperature = round(var_aht.current_temperature.temperature * 10) / 10;
  var_aht.current_humidity.relative_humidity = round(var_aht.current_humidity.relative_humidity * 10) / 10;

  // Gestione memoria salvataggio parametri
  if (!LittleFS.begin()) {
    Serial.println("Errore inizializzazione LittleFS");
    return;
  }
  load_settings();
  var_global.old_temp = var_menu.target_temperature;
  var_global.old_termo = var_menu.termo_temperature;
  var_global.old_mode = var_menu.current_mode_index;

  // Primo aggiornamento temperatura esterna
  fetchexternal_temperature();
  last_sync_millis = millis();

  // Avvia il client NTP per sincronizzazione orario
  if (var_wifi.stato_wifi) {
    timeClient.begin();
    timeClient.update();          // Sincronizza subito
    last_sync_millis = millis();  // Registra il tempo della sincronizzazione
  }
  //Calcolo lo shifting per stampare l'orario al centro
  int16_t x1, y1;
  uint16_t textWidth, textHeight;
  if (var_wifi.stato_wifi)
    display.getTextBounds(timeClient.getFormattedTime().substring(0, 5), 0, 0, &x1, &y1, &textWidth, &textHeight);
  else
    display.getTextBounds("00:00", 0, 0, &x1, &y1, &textWidth, &textHeight);
  var_display.riga = (128 - textWidth) * 0.5;
  display.getTextBounds("OFF", 0, 0, &x1, &y1, &textWidth, &textHeight);
  var_display.riga_dx = (128 - textWidth);

  spegni_wifi();

  if (var_menu.current_mode_index != 1) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("Il termostato non e' impostato in");
    display.println("modalita' continua.");
    display.println("Spegnere esp-01s se");
    display.println("acceso!");
    display.display();
    delay(10000);
  }
}

void loop() {
  time_temp();
  read_sensor_data();
  if (!var_menu.in_sub_menu && !var_menu.in_menu) {
    var_global.refresh = 1000;
    if (var_display.display_state)
      stampa_main();
  }

  // Funzioni che sfruttano esp-now
  gestione_esp_now();
  gestione_relay();
  send_shutdown_esp();

  calcolo_statistiche();

  if (!var_menu.in_sub_menu && var_menu.in_menu) {
    var_global.refresh = 300;  //300ms è il tempo minimo per il interrupt consequenziali
    draw_menu();
    if (var_encoder.new_position != 0) {
      var_menu.current_menu_index = (var_menu.current_menu_index + var_encoder.new_position + num_menu_items) % num_menu_items;
      var_encoder.new_position = 0;
      draw_menu();
    }
  }
  // Gestione del pulsante
  if (var_encoder.key_pressed) {
    var_encoder.key_pressed = false;
    rapid_off();
    if (var_menu.in_sub_menu) {
      var_menu.in_sub_menu = false;  // Torna al menu principale
      return;
    } else if (var_menu.current_menu_index == (num_menu_items - 1) && var_menu.in_menu) {  // Nell'ultima posizione c'è sempre "Back to Home"
      stampa_main();
      return;
    } else if (var_menu.in_menu) {
      var_menu.in_sub_menu = true;
      draw_sub_menu();
    }
  }
  // Gestione sottomenù
  if (var_menu.in_sub_menu) {
    unsigned long sub_menu_timer = millis();
    handle_menu_selection(sub_menu_timer);
  }

  spegnimento_display();
  reset_variabili();

  delay(var_global.refresh);
}
