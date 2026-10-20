 // ESP32-WROOM-32 / Arduino ESP32 3.3.11
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <time.h>
#include "Config.h"
#include "RootCA.h"
#include "WeatherLogic.h"
struct City {const char* name;const char* lat;const char* lon;};
const City cities[]={{"Zanjan","36.67642","48.49628"},{"Qaen","33.72654","59.18439"}};
struct Reading {float temperature,humidity,wind;uint32_t received;bool valid,failed;};
struct Update {uint8_t city;bool ok;float temperature,humidity,wind;};
Reading readings[2]={};
QueueHandle_t resultQueue;
hd44780_I2Cexp lcd;
bool lcdOK=false;
uint8_t page=0;
uint32_t pageAt=0,drawAt=0;
// Only this worker accesses HTTP/TLS. LCD stays responsive during slow requests.
bool fetchWeather(uint8_t city,Update& out){
 WiFiClientSecure client;client.setCACert(ROOT_CA);client.setHandshakeTimeout(10);
 HTTPClient http;http.setConnectTimeout(8000);http.setTimeout(8000);http.useHTTP10(true);
 String url="https://api.open-meteo.com/v1/forecast?latitude=";
 url+=cities[city].lat;url+="&longitude=";url+=cities[city].lon;
 url+="&current=temperature_2m,relative_humidity_2m,wind_speed_10m&temperature_unit=celsius&wind_speed_unit=kmh&timezone=Asia%2FTehran";
 if(!http.begin(client,url))return false;
 int status=http.GET();
 if(status!=HTTP_CODE_OK){Serial.printf("%s: HTTP %d\n",cities[city].name,status);http.end();return false;}
 StaticJsonDocument<1536> doc;
 auto error=deserializeJson(doc,http.getStream());http.end();
 if(error){Serial.printf("JSON: %s\n",error.c_str());return false;}
 JsonObject current=doc["current"];
 if(!current["temperature_2m"].is<float>()||!current["relative_humidity_2m"].is<float>()||!current["wind_speed_10m"].is<float>())return false;
 out.temperature=current["temperature_2m"];
 out.humidity=current["relative_humidity_2m"];
 out.wind=current["wind_speed_10m"];
 if(!validWeather(out.temperature,out.humidity,out.wind))return false;
 Serial.printf("%s: %.1f C / %.0f %% / %.1f km/h; API time: %s\n",cities[city].name,out.temperature,out.humidity,out.wind,current["time"] | "unknown");
 return true;
}
void networkTask(void*){
 uint32_t attemptAt[2]={};bool attempted[2]={},success[2]={};
 bool previousConnected=false;
 for(;;){
  bool connected=WiFi.status()==WL_CONNECTED;
  if(connected&&!previousConnected){attempted[0]=attempted[1]=false;Serial.println("WiFi connected");}
  previousConnected=connected;
  // Certificate verification requires a correct clock. Never disable TLS verification.
  if(connected && time(nullptr)>1700000000){
   for(uint8_t i=0;i<2;i++){
    uint32_t now=millis();
    if(!attempted[i] || uint32_t(now-attemptAt[i])>=(success[i]?REFRESH_MS:RETRY_MS)){
     Update update={};update.city=i;update.ok=fetchWeather(i,update);
     attemptAt[i]=millis();attempted[i]=true;success[i]=update.ok;
     xQueueSend(resultQueue,&update,portMAX_DELAY);
    }
   }
  }
  vTaskDelay(pdMS_TO_TICKS(250));
 }
}
void printLine(uint8_t row,const char* text){
 char padded[17];memset(padded,' ',16);padded[16]=0;
 size_t n=strlen(text);if(n>16)n=16;memcpy(padded,text,n);
 lcd.setCursor(0,row);lcd.print(padded);
}
void draw(){
 uint8_t city=page/2;Reading &r=readings[city];
 char a[32],b[32];
 bool offline=WiFi.status()!=WL_CONNECTED;
 bool stale=r.valid&&(offline||r.failed||expired(millis(),r.received));
 if(!r.valid){
  snprintf(a,sizeof(a),"%s",cities[city].name);
  snprintf(b,sizeof(b),"%s",offline?"WiFi connecting":time(nullptr)<1700000000?"Syncing clock":r.failed?"API retry...":"Loading weather");
 }else if(page%2==0){
  snprintf(a,sizeof(a),"%s%s",cities[city].name,stale?" *OLD":"");
  snprintf(b,sizeof(b),"Temp: %.1f C",r.temperature);
 }else{
  snprintf(a,sizeof(a),"Humidity:%3.0f%%%s",r.humidity,stale?" *":"");
  snprintf(b,sizeof(b),"Wind:%.1f km/h",r.wind);
 }
 if(lcdOK){printLine(0,a);printLine(1,b);}
}
void setup(){
 Serial.begin(115200);delay(200);
 Wire.begin(LCD_SDA,LCD_SCL);
 int status=lcd.begin(16,2);lcdOK=status==0;
 if(lcdOK){lcd.backlight();printLine(0,"Weather Display");printLine(1,"Starting...");}
 else Serial.printf("LCD initialization error: %d\n",status);
 resultQueue=xQueueCreate(4,sizeof(Update));
 if(!resultQueue){Serial.println("Queue allocation failed");if(lcdOK)printLine(1,"Memory error");return;}
 WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);
 WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
 configTime(0,0,"pool.ntp.org","time.google.com","time.cloudflare.com");
 if(xTaskCreate(networkTask,"weather",12288,nullptr,1,nullptr)!=pdPASS){
  Serial.println("Worker allocation failed");if(lcdOK)printLine(1,"Task error");
 }
}
void loop(){
 Update update;
 if(resultQueue)while(xQueueReceive(resultQueue,&update,0)==pdTRUE){
  Reading &r=readings[update.city];r.failed=!update.ok;
  if(update.ok){r.temperature=update.temperature;r.humidity=update.humidity;r.wind=update.wind;r.received=millis();r.valid=true;}
 }
 uint32_t now=millis();
 if(uint32_t(now-pageAt)>=PAGE_MS){page=(page+1)%4;pageAt=now;draw();}
 if(uint32_t(now-drawAt)>=500){drawAt=now;draw();}
 // Retry initial failed Wi-Fi association as well as later disconnections.
 static uint32_t reconnectAt=0;
 if(WiFi.status()!=WL_CONNECTED && uint32_t(now-reconnectAt)>=20000){reconnectAt=now;WiFi.reconnect();}
 delay(10);
}
