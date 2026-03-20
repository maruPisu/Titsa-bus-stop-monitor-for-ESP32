#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_wifi.h"
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C 
#define STOPSIZE 2
#define LINESIZE 20

const char* ssid = "OnePlus 6T";
const char* password = "polpolpo";

const char* url1 = "https://titsa.com/ajax/xGetInfoParada.php?id_parada=1934";
const char* url2 = "https://titsa.com/ajax/xGetInfoParada.php?id_parada=1918";

// Variabili globali
int tiempo_actual = -1;
unsigned long lastUpdate = 0;
unsigned long lastRequest = 0;

const unsigned long requestInterval = 10000; // 1 minuto
const unsigned long countdownInterval = 60000; // decremento ogni minuto

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
struct LineData {
  String id;
  int tiempo;
  unsigned long lastUpdate;
};

struct StopData {
  String stopId;
  LineData lines[LINESIZE];
  int lineCount;
  unsigned long lastUpdate;
};

StopData allStops[STOPSIZE];

String currentStopId = "";
int currentIndex = 0;

void scan(){
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(1000);

  Serial.println("Scan...");

  int n = WiFi.scanNetworks();
  Serial.println("Scan done");

  if (n == 0) {
    Serial.println("Nessuna rete trovata");
  } else {
    for (int i = 0; i < n; ++i) {
      Serial.println(WiFi.SSID(i));
    }
  }
}

void initDisplay(){
  // Initialize OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED not found at 0x3C!");
  } else {
    Serial.println("OLED initialized");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Display OK");
    display.display();
  }
}

void write(const char* text, bool clear = true){
  Serial.println(text);
  if(clear){
    display.clearDisplay();
    display.setCursor(1, 1);
  }
  display.println(text);
  display.display();
}

void writeStops(){
  //clear display
  display.clearDisplay();
  display.setCursor(1, 1);

  for (StopData busStop : allStops){
    if(busStop.stopId == ""){
      break;
    }
    write(busStop.stopId.c_str(), false);
    for(LineData line : busStop.lines){
      if(line.id == ""){
        break;
      }
      String resultado = "Linea " + line.id + ": " + String(line.tiempo) + " min";
      write(resultado.c_str(), false);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  //scan();
  initDisplay();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.disconnect(true);
  delay(1000);
  Serial.print("Status before begin: ");
  Serial.println(WiFi.status());
  WiFi.begin(ssid, password);

  int attempts = 0;

  write("Connecting...");
  Serial.print("Connessione WiFi");
  while (WiFi.status() != WL_CONNECTED and attempts < 10) {
    delay(500);
    Serial.print(".");
    ++attempts;
  } 
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnesso!");
    write("Connection succesful");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nConnessione fallita");
    write("Connection failed");
    Serial.print("Status: ");
    Serial.println(WiFi.status());
  }
}

void loop() {
  unsigned long now = millis();

  // Richiesta ogni minuto
  if (now - lastRequest > requestInterval) {
    lastRequest = now;
    fetchData(url1);
    fetchData(url2);
  }

  // Countdown stimato
  if (tiempo_actual > 0 && now - lastUpdate > countdownInterval) {
    tiempo_actual--;
    lastUpdate = now;

    Serial.print("Countdown stimato: ");
    Serial.print(tiempo_actual);
    Serial.println(" min");
  }
}

int getBusStop(const String & id){
  for (int i = 0; i < STOPSIZE; ++i){
    if(allStops[i].stopId == id){
      return i;
    }
  }
  return -1;
}

// Add a new stop
void addStop(const String& stopId) {
  // Find first empty slot or add to end
  for(int i = 0; i < STOPSIZE; i++) {
    if(allStops[i].stopId == "") {  // Empty slot found
      allStops[i].stopId = stopId;
      allStops[i].lineCount = 0;
      allStops[i].lastUpdate = millis();
      return;
    }
  }
  Serial.println("No space for new stop!");
}

int getLine(const StopData& stopData, const String& line){
  for (int i = 0; i < LINESIZE; ++i){
    if(stopData.lines[i].id == line){
      return i;
    }
  }
  return -1;
}

void addLine(const String& line, int time){
  // Find first empty slot or add to end
  for(int i = 0; i < LINESIZE; i++) {
    if(allStops[currentIndex].lines[i].id == "") {  // Empty line found
      allStops[currentIndex].lines[i].id = line;
      allStops[currentIndex].lines[i].tiempo = time;
      allStops[currentIndex].lines[i].lastUpdate = millis();
      return;
    }else if(allStops[currentIndex].lines[i].id == line) {  // Empty line found
      allStops[currentIndex].lines[i].tiempo = time;
      allStops[currentIndex].lines[i].lastUpdate = millis();
      return;
    }
  }
  Serial.println("No space for new line!");
}

void fetchData(const char* url) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(url);

    int httpCode = http.GET();

    if (httpCode > 0) {
      String payload = http.getString();

      StaticJsonDocument<2048> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (!error) {
        if (doc["success"] == true) {
          // Store stop ID globally
          currentStopId = doc["parada"]["id"].as<String>();
          String txt = "Parada " + currentStopId;

          if(getBusStop(currentStopId) == -1){
            //insert new bus stop
            addStop(currentStopId);
          }
          currentIndex = getBusStop(currentStopId);

          StopData stopData = allStops[currentIndex];
          
          JsonArray lineas = doc["lineas"].as<JsonArray>();
          
          if (lineas.size() > 0) {
            
            for (JsonObject linea : lineas) {
              String linea_id = linea["id"].as<String>();
              int nuevo_tiempo = linea["tiempo"].as<int>();
              
              if (nuevo_tiempo > 0) {
                addLine(linea_id, nuevo_tiempo);
              }
            }
          } else {
            Serial.println("No new data, using estimated times");
          }
        } else {
          Serial.println("API returned success=false");
        }
      } else {
        Serial.println("Error parsing JSON");
      }
    } else {
      Serial.print("HTTP error: ");
      Serial.println(httpCode);
    }

    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
  writeStops();
}
