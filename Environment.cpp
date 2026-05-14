 /*
 * Library to gather all environmental data like real time and temperature
 * (c) Cedric Millard 2020
 */

#include "Environment.h"
#include <time.h>
#include <SunRise.h>
#if defined(ESP8266)
#include <TZ.h>
#endif
#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>
#include <Arduino.h>
//#define _DEBUG_

Environment::Environment(PubSubClient *iMqttClientEnv, short iId) : MyMQTTClient(iMqttClientEnv, String(iId) + "_Env")
{
  addTopic(SENSOR_TOPIC);
  addTopic(WEATHER_TOPIC_DAILY);
  addTopic(WEATHER_TOPIC_HOURLY);
  addTopic("/net/sensor/#");
}

void Environment::handleMqttCallback(char* iTopic, byte* payload, unsigned int iLength)
{
#ifdef _DEBUG_
  Serial.print("Message arrived [");
  Serial.print(iTopic);
  Serial.print("] ");
  for (int i = 0; i < iLength; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
#endif

  String topic = iTopic;
  char *tempString = (char *) malloc(sizeof(char)*(iLength+1));
  memcpy(tempString, payload, iLength);
  tempString[iLength] = '\0';
  String sPayload(tempString);
  free(tempString);

  if(topic.indexOf("/sensor/")>=0)
  {
    //retrieve ID of the prise
    String sID = topic.substring(topic.indexOf("/sensor/")+8, topic.indexOf("/", topic.indexOf("/sensor/")+8));
    short iID = sID.toInt();
    if(topic.endsWith("/value"))
    {
      if(sPayload.length()>0)
        setSensorValue(iID, sPayload.toFloat());
      else
        setSensorValue(iID, -255);
    }
    if(topic.endsWith("/type"))
    {
      if(!sensorUnits.exists(iID))
      {
        switch(sPayload.toInt())
        {
          case 0:sensorUnits.add("*C", iID);break;
          case 2:sensorUnits.add("V", iID);break;
          case 3:sensorUnits.add("*C", iID);break;
          default: sensorUnits.add("", iID);break;
        }
      }
    }
  }

  if(topic==WEATHER_TOPIC_DAILY)
  {
    setWeatherDaily(sPayload);
  }

  if(topic==WEATHER_TOPIC_HOURLY)
  {
    setWeatherHourly(sPayload);
  }
}

bool Environment::update(bool iForce)
{
  bool result = true;

  checkMQTTConnection(iForce);

  if( lastNTPUpdate == 0 || (unsigned long)(millis()- lastNTPUpdate) > REALTIME_UPDATE_FREQ * 1000 || iForce)
  {
    if(!setRealTime()) result = false;
  }
  
  updateSunriseSunsetTime(iForce);

  return result;
}

bool Environment::setRealTime()
{
#if defined(ESP8266)
  configTime(TZ_Europe_Amsterdam, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
#else
  configTime(3600, 3600, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  //setenv("TZ", "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00", 1);
  setenv("TZ", "JST-9", 1);
  tzset();
#endif

  struct tm timeinfo;
  if(!getLocalTime(&timeinfo,5000U)){
#ifdef _DEBUG_
  Serial.println("Connection to NTP server failed");
#endif
    pLog->addLogEntry("Connection to NTP server failed");
    return false;
  }

#ifdef _DEBUG_
  Serial.println("Time updated from NTP");
#endif
  pLog->addLogEntry("Time updated from NTP");
  lastNTPUpdate = millis();
  return true;
}

void Environment::setLocation(float iLat, float iLong)
{
  weatherLat = iLat;
  weatherLong = iLong;
}

float Environment::getTemperatureExtFromWeather()
{
  return getWeatherHour(time(nullptr)).Tmax;
}

float Environment::getSensorValue(int iSensorId)
{
  if(sensorValues.exists(iSensorId))
    return sensorValues.getItem(iSensorId);
  else
    return -255;
}

void Environment::setSensorValue(int iSensorId, float iValue)
{
  if(!sensorValues.exists(iSensorId))
  {
    sensorValues.add(iValue, iSensorId);
  }
  else sensorValues.getItem(iSensorId) = iValue;
}

void Environment::setWeatherDaily(String iWeatherJson)
{
#ifdef _DEBUG_
  Serial.println("Environment::setWeatherDaily");
  Serial.println(iWeatherJson);
#endif
  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, iWeatherJson);
  if(err!=DeserializationError::Ok)
    Serial.printf("\nERROR ERROR ERROR DeserializationError %s %d\n%s\n\n", err.c_str(), doc.capacity(), iWeatherJson.c_str());

  JsonArray daily = doc["daily"];

  for(int i=0; i<5; i++)
  {
    JsonObject currentDay = daily[i];
    weather_d[i].Weather = (short) currentDay["weather"];
    weather_d[i].Icon = (short) currentDay["icon"];
    weather_d[i].Tmax = (float)currentDay["Tmax"];
    weather_d[i].Tmin = (float)currentDay["Tmin"];
    weather_d[i].Wind = (float)currentDay["wind"];
    weather_d[i].T6 = (float)currentDay["T6"];
    weather_d[i].updateTime = (long)currentDay["time"];
  }
#ifdef _DEBUG_
  Serial.println("Environment::setWeatherDaily done");
#endif
}

void Environment::setWeatherHourly(String iWeatherJson)
{
#ifdef _DEBUG_
  Serial.println("Environment::setWeatherHourly");
  Serial.println(iWeatherJson);
#endif  
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, iWeatherJson);
  if(err!=DeserializationError::Ok)
    Serial.printf("\nERROR ERROR ERROR DeserializationError %s %d\n%s\n\n", err.c_str(), doc.capacity(), iWeatherJson.c_str());

  JsonArray hourly = doc["hourly"];

  for(int i=0; i<24; i++)
  {
    JsonObject currentHour = hourly[i];
    weather_h[i].Weather = (short) currentHour["weather"];
    weather_h[i].Icon = (short) currentHour["icon"];
    weather_h[i].Tmax = weather_h[i].Tmin = (float)currentHour["Temp"];
    weather_h[i].Wind = (float)currentHour["wind"];
    weather_h[i].updateTime = (long)currentHour["time"];
  }
#ifdef _DEBUG_
  Serial.println("Environment::setWeatherHourly done");
#endif
}

String Environment::getSensorUnit(int iSensorId)
{
  if(sensorUnits.exists(iSensorId))
    return sensorUnits.getItem(iSensorId);
  else
    return "";
}

float Environment::getTemperatureMorning()
{
  return getTodayWeather().Tmin;
}

Weather Environment::getTodayWeather()
{
  return getWeatherDay(0);
}

Weather Environment::getTomorrowWeather()
{
  return getWeatherDay(1);
}

Weather Environment::getWeatherDay(int day)
{
  Weather emptyWeather;
  if (day>3 || weather_d[0].updateTime == 0)
    return emptyWeather;

  struct tm updatedDay = *localtime(&(weather_d[0].updateTime));
  //Reset hour to midnight
  updatedDay.tm_hour = 0; updatedDay.tm_min = 0; updatedDay.tm_sec = 0;

  /*long delta = difftime(time(nullptr), mktime(&updatedDay));

  //Weather updated today
  if (delta < 86400) return weather_d[day];
  //Weather was updated yesterday
  if (delta < 172800 && day<3) return weather_d[day+1];
  if (delta < 259200 && day<2) return weather_d[day+2];
  if (delta < 345600 && day<1) return weather_d[day+3];*/

  int delta = difftime(time(nullptr), mktime(&updatedDay))/86400;

  if (delta+day<5) return weather_d[delta+day];

  return emptyWeather;
}

Weather Environment::getWeatherHour(long hour)
{
  Weather emptyWeather;
  
  //Compute time index
  int index = (hour - weather_h[0].updateTime)/3600;

  if (index<0 || index > 23)
  {
    Serial.println("ERROR: Cannot find hourly weather index="+String(index) + " timestamp="+String(hour) + " weatherH1="+String(weather_h[0].updateTime));
    return emptyWeather;
  }

  return  weather_h[index];
}

bool Environment::isSunriseSunsetUptodate()
{
  //compare current day in local time with sunrise and sunset day
  time_t t = time (nullptr);
  tm srTM_now = *localtime(&t);
  tm srTM_sunrise = *localtime(&(sunriseTime));
  tm srTM_sunset = *localtime(&(sunsetTime));  
 
  return (sunriseTime>0 && srTM_sunrise.tm_yday == srTM_now.tm_yday && srTM_sunset.tm_yday == srTM_now.tm_yday);
}

void Environment::updateSunriseSunsetTime(bool iForce)
{
  if(!isSunriseSunsetUptodate() || iForce)
  {
#ifdef _DEBUG_    
    Serial.println("updateSunriseSunsetTime");
#endif
    pLog->addLogEntry("Update Sunrise and Sunset time");
    
    //Time in UTC
    time_t t = time (nullptr);
    
    SunRise sr_tday;
    sr_tday.calculate(weatherLat, weatherLong, t);

    SunRise sr_tmrw;
    sr_tmrw.calculate(weatherLat, weatherLong, t+86400);

    SunRise sr_ystd;
    sr_ystd.calculate(weatherLat, weatherLong, t-86400);

    //Get day of the year for the three times
    tm srTM_now = *localtime(&t);
    time_t riseTime = sr_tday.riseTime;
    time_t setTime = sr_tday.setTime;
    tm srTM_sunrise = *localtime(&riseTime);
    tm srTM_sunset = *localtime(&setTime);

    //In case sunrise is not same day, use a different date
    if(srTM_sunrise.tm_yday == srTM_now.tm_yday)
    {
      sunriseTime = sr_tday.riseTime;
    }
    else if(srTM_sunrise.tm_yday < srTM_now.tm_yday || srTM_sunrise.tm_year < srTM_now.tm_year)
    {
      sunriseTime = sr_tmrw.riseTime;
    }
    else
    {
      sunriseTime = sr_ystd.riseTime;
    }

    if(srTM_sunset.tm_yday == srTM_now.tm_yday)
    {
      sunsetTime = sr_tday.setTime;
    }
    else if(srTM_sunset.tm_yday < srTM_now.tm_yday || srTM_sunset.tm_year < srTM_now.tm_year)
    {
      sunsetTime = sr_tmrw.setTime;
    }
    else
    {
      sunsetTime = sr_ystd.setTime;
    }

#ifdef _DEBUG_    
    Serial.println(String("Sunset time = ")+String(sunsetTime));
    Serial.println(String("Sunrise time = ")+String(sunriseTime));
#endif
  }
}

bool Environment::isDay()
{
  //Time in UTC
  /*time_t t = time (nullptr);
  SunRise sr;
  sr.calculate(weatherLat, weatherLong, t);
  return sr.isVisible;*/
  time_t t = time (nullptr);
  return (t<sunsetTime && t>=sunriseTime);
}

time_t Environment::getSunsetTime()
{
  return sunsetTime;
}

time_t Environment::getSunriseTime()
{
  return sunriseTime;
}

String getWeekNumber()
{
  String out = "W";
  struct tm timeinfo;
  char sTime[5];
   if(!getLocalTime(&timeinfo,5000U)){
#ifdef _DEBUG_
  Serial.println("RealTime not set");
#endif
    return out;
  }
  strftime(sTime, 5, "%V", &timeinfo);
  out += sTime;
  return out;
}

String getTimeFr()
{
  String out= "";
  struct tm timeinfo;
  char sTime[10];
   if(!getLocalTime(&timeinfo,5000U)){
#ifdef _DEBUG_
  Serial.println("RealTime not set");
#endif
    return out;
  }
  strftime(sTime, 10, "%R", &timeinfo);
  out = sTime;
  return out;
}

long getTimeSec()
{
  long out = -1;
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo,5000U)){
#ifdef _DEBUG_
  Serial.println("RealTime not set");
#endif
    return out;
  }
  out = timeinfo.tm_sec + timeinfo.tm_min * 60 + timeinfo.tm_hour * 3600;

  return out;
}

String getDateFr()
{
  String out = "";
  struct tm timeinfo;
  char sTime[20];
   if(!getLocalTime(&timeinfo,5000U)){
#ifdef _DEBUG_
  Serial.println("RealTime not set");
#endif
    return out;
  }
  strftime(sTime, 20, "%a %d %b", &timeinfo);
  out = sTime;
  return out;
}

String getDateShort()
{
  String out = "";
  struct tm timeinfo;
  char sTime[6];
   if(!getLocalTime(&timeinfo,5000U)){
#ifdef _DEBUG_
  Serial.println("RealTime not set");
#endif
    return out;
  }
  strftime(sTime, 6, "%d/%m", &timeinfo);
  out = sTime;
  return out;
}

uint8_t getDay()
{
  uint8_t day;
  struct tm timeinfo;
  char sTime[2];
   if(!getLocalTime(&timeinfo,5000U)){
#ifdef _DEBUG_
  Serial.println("RealTime not set");
#endif
    return 0;
  }
  strftime(sTime, 2, "%w", &timeinfo);
  day = atoi(sTime);
  return day;
}

time_t timegm(tm * tm) {
  time_t tStampBadLocaltime = mktime(tm);

  struct tm tmUTC;
  struct tm tmLocaltime;
  gmtime_r(&tStampBadLocaltime, &tmUTC);
  localtime_r(&tStampBadLocaltime, &tmLocaltime);
  time_t tstampBadUTC = mktime(&tmUTC);
  time_t tstampLocaltime = mktime(&tmLocaltime);
  time_t tLocalOffset = tstampLocaltime - tstampBadUTC;
  return tStampBadLocaltime + tLocalOffset;
}

bool operator==(const Weather& lhs, const Weather& rhs)
{
    if(lhs.Tmax!=rhs.Tmax) return false;
    if(lhs.Tmin!=rhs.Tmin) return false;
    if(lhs.T6!=rhs.T6) return false;
    if(lhs.Weather!=rhs.Weather) return false;
    if(lhs.Wind!=rhs.Wind) return false;
    if(lhs.Icon!=rhs.Icon) return false;
    //if(lhs.Moon!=rhs.Moon) return false;

    return true;
}

bool operator!=(const Weather& lhs, const Weather& rhs)
{
    return !(lhs==rhs);
}

#if defined(ESP8266)
/*bool getLocalTime(struct tm * info, uint32_t ms=5000U)
{
    uint32_t start = millis();
    time_t now;
    while((millis()-start) <= ms) {
        time(&now);
        localtime_r(&now, info);
        if(info->tm_year > (2016 - 1900)){
            return true;
        }
        delay(10);
    }
    return false;
}*/
#endif
