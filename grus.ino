#include <A6lib.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define TEMPERATURE_PRECISION 9
#define DEVICE_COUNT 2
#define ONE_WIRE_UNO 2
#define RX_UNO 7
#define TX_UNO 8
#define USERS_COUNT 3
#define CALL_CHECK_INTERVAL 2000L
#define HOUR 3600000L

A6lib A6l(TX_UNO, RX_UNO);
SMSmessage sms;
OneWire oneWire(ONE_WIRE_UNO);
DallasTemperature sensors(&oneWire);
DeviceAddress deviceAddress[DEVICE_COUNT]  = {
  { 0x28, 0x42, 0x9E, 0x53, 0x3, 0x0, 0x0, 0xA4 },
  { 0x28, 0xDA, 0xAF, 0x53, 0x3, 0x0, 0x0, 0xFD }
};
float hourlyTemperature[DEVICE_COUNT][24];
const String deviceName[DEVICE_COUNT] = {"\nSensor 10m", "\nSensor  1m"};
const String comrade[USERS_COUNT] = {"91112223333", "91112224444", "91112225555"};
unsigned long checkTime, measureTime;

void setup(void)
{
  delay(5000);
  A6l.blockUntilReady(28800);
  
  sensors.begin();
  for (int i = 0; i < DEVICE_COUNT; i++) {
    sensors.setResolution(deviceAddress[i], TEMPERATURE_PRECISION);
  }
  sensors.requestTemperatures();
  for (int s = 0; s < DEVICE_COUNT; s++) {
     for (int t = 0; t < 24; t++) {
       hourlyTemperature[s][t] = sensors.getTempC(deviceAddress[s]);
     }
  }
    
  checkTime = millis() + CALL_CHECK_INTERVAL;
  measureTime = millis() + HOUR;
}

void loop(void)
{ 
  if (millis() + HOUR < checkTime) {
    // счётчик миллисекунд millis() через 50 суток со старта сбрасывается в ноль
    checkTime = millis() + CALL_CHECK_INTERVAL;
    measureTime = millis() + HOUR;
  }
  if (millis() >= checkTime) {
    callInfo cinfo = A6l.checkCallStatus();
    if (cinfo.direction == DIR_INCOMING) {
      // звонят, сверимся со списком телефонных номеров
      bool allow = false;
      String phone = "+7";
      for (int i = 0; i < USERS_COUNT; i++ ) {
        if (cinfo.number.endsWith(comrade[i])) {
          phone += comrade[i];
          allow = true;
          break;
        }
      }
      delay(5000);
      A6l.hangUp();
      if (allow) {
        sensors.requestTemperatures();
        float tmin[DEVICE_COUNT] = {99, 99};
        String answer = "Current temperature:";
        for (int s = 0; s < DEVICE_COUNT; s++) {
          float temperature = sensors.getTempC(deviceAddress[s]);
          // минимальная за сутки температура по датчику
          tmin[s] = temperature;
          for (int t = 1; t < 24; t++) {
            if (hourlyTemperature[s][t] < tmin[s]) {
              tmin[s] = hourlyTemperature[s][t];
            }
          }
          answer += deviceName[s] + " t=" + String(temperature, 1) + " C";
        }
        answer += "\nLast 24 hours minimum:";
        for (int s = 0; s < DEVICE_COUNT; s++) {
          answer += deviceName[s] + " t=" + String(tmin[s], 1) + " C";
        }
        A6l.sendSMS(phone, answer);
      }
    }
    if (millis() >= measureTime) {
      // храним почасовые замеры за последние сутки
      measureTime = millis() + HOUR;
      sensors.requestTemperatures();
      for (int s = 0; s < DEVICE_COUNT; s++) {
        for (int t = 1; t < 24; t++) {
          hourlyTemperature[s][t] = hourlyTemperature[s][t-1];
        }
        hourlyTemperature[s][0] = sensors.getTempC(deviceAddress[s]);
      }
    }
    checkTime = millis() + CALL_CHECK_INTERVAL;
  }
}
