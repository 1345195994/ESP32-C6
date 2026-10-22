#pragma once
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <math.h>

struct WeatherSettings {
  char city[97] = {};
  float latitude = 0, longitude = 0;
  uint8_t enabled = 0;
};
struct WeatherResult { uint32_t version; bool valid; float temperature; int code; };
static WeatherSettings weatherSettings;
static QueueHandle_t weatherResults = nullptr;
static bool weatherBusy = false, weatherAttempted = false, weatherValid = false;
static uint32_t weatherVersion = 1, weatherAttemptAt = 0;
static float weatherTemperature = 0;
static int weatherCode = 0;
struct WeatherJob { WeatherSettings settings; uint32_t version; };

static bool weatherNumber(const String &json, const char *key, float &value) {
  int at=json.indexOf(String('"')+key+'"');
  if(at<0) return false;
  at=json.indexOf(':',at); if(at<0) return false;
  const char *start=json.c_str()+at+1;
  while(*start==' ') ++start;
  char *end=nullptr; value=strtof(start,&end);
  return end!=start && isfinite(value) && (*end==',' || *end=='}' || isspace((unsigned char)*end));
}
static void weatherWorker(void *arg) {
  WeatherJob *job=static_cast<WeatherJob *>(arg);
  WeatherResult result={job->version,false,0,0};
  {
    WiFiClientSecure tls;
    // Public read-only weather data; no credentials are sent to this endpoint.
    tls.setInsecure(); tls.setTimeout(6); tls.setHandshakeTimeout(6);
    HTTPClient http;
    http.setConnectTimeout(5000); http.setTimeout(6000); http.useHTTP10(true);
    String url="https://api.open-meteo.com/v1/forecast?latitude="+String(job->settings.latitude,5)+
      "&longitude="+String(job->settings.longitude,5)+"&current=temperature_2m,weather_code&forecast_days=1";
    if(http.begin(tls,url)) {
      if(http.GET()==200 && (http.getSize()<0 || http.getSize()<=4096)) {
        String data=http.getString();
        // Isolate current values: current_units contains identical key names.
        int start=data.indexOf("\"current\":");
        if(start>=0) {
          int end=data.indexOf('}',start);
          String current=data.substring(start,end+1); float code;
          result.valid=weatherNumber(current,"temperature_2m",result.temperature) &&
            weatherNumber(current,"weather_code",code) && code>=0 && code<=99;
          if(result.valid) result.code=(int)code;
        }
      }
      http.end();
    }
  }
  xQueueOverwrite(weatherResults,&result);
  delete job; vTaskDelete(nullptr);
}
static const char *weatherDescription(int code) {
  if(code==0) return "晴";
  if(code<=2) return "多云";
  if(code==3) return "阴";
  if(code==45||code==48) return "雾";
  if(code>=95) return "雷雨";
  if((code>=71&&code<=77)||code==85||code==86) return "雪";
  return "雨";
}
static String weatherText() {
  if(!weatherSettings.enabled) return "请在网页设置天气地址";
  if(weatherValid) return String(weatherDescription(weatherCode))+" "+String(weatherTemperature,1)+" C";
  if(WiFi.status()!=WL_CONNECTED) return "天气等待联网";
  return weatherBusy ? "天气更新中" : "天气暂不可用";
}
static void weatherTick() {
  WeatherResult result;
  if(weatherResults && xQueueReceive(weatherResults,&result,0)==pdTRUE) {
    weatherBusy=false;
    if(result.version==weatherVersion) {
      weatherValid=result.valid;
      if(result.valid) { weatherTemperature=result.temperature; weatherCode=result.code; }
    } else weatherAttempted=false;
  }
  if(weatherBusy || uploadData || !weatherSettings.enabled || !timeSynced || WiFi.status()!=WL_CONNECTED) return;
  uint32_t interval=weatherValid?1800000UL:120000UL;
  if(weatherAttempted && millis()-weatherAttemptAt<interval) return;
  if(!weatherResults) weatherResults=xQueueCreate(1,sizeof(WeatherResult));
  if(!weatherResults || ESP.getMaxAllocHeap()<48000UL || ESP.getFreeHeap()<70000UL) return;
  WeatherJob *job=new(std::nothrow) WeatherJob{weatherSettings,weatherVersion};
  if(!job) return;
  weatherAttemptAt=millis(); weatherAttempted=true; weatherBusy=true;
  if(xTaskCreate(weatherWorker,"weather",6144,job,1,nullptr)!=pdPASS) { weatherBusy=false; delete job; }
}
