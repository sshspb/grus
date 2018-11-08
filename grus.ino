#include <SoftwareSerial.h>
#include <OneWire.h>

#define DEVICE_COUNT 3
#define USERS_COUNT 3
#define ONE_WIRE_UNO 2
#define RX_UNO 7
#define TX_UNO 8
#define PWR_KEY_UNO 11
#define PWR_LED_UNO 12
#define TEMP_NULL 1598
#define CALL_CHECK_INTERVAL 2000L
#define HOUR 3600000L
#define MONITOR_RATE 9600L
#define MODEM_RATE 9600L

#define GSM_OK 0
#define GSM_SMS 1
#define GSM_NL "\r\n"
#define CTRL_Z '\x1A'

#define SCRATCHPAD_TEMP_LSB 0
#define SCRATCHPAD_TEMP_MSB 1
#define SCRATCHPAD_CRC 8

uint8_t deviceAddress[DEVICE_COUNT][8]  = {
  { 0x28, 0x6C, 0x8F, 0x53, 0x03, 0x00, 0x00, 0xB0 }, // 00m Sensor 0 m grey hub
  { 0x28, 0xF9, 0xCD, 0x53, 0x03, 0x00, 0x00, 0x80 }, // 15m Sensor 15 m white hub
  { 0x28, 0x27, 0x84, 0x53, 0x03, 0x00, 0x00, 0x4D }  // 05m Sensor  5m
};
int16_t currentTemperature[DEVICE_COUNT];
int16_t hourlyTemperature[DEVICE_COUNT][24];
const String deviceName[DEVICE_COUNT] = {"#1/0m ", "#2/15m ", "#3/5m "};
const String comrade[USERS_COUNT] = {"9219258698", "9214201935", "9213303129"};
bool pwrled = true;
unsigned long checkTime, measureTime;

SoftwareSerial modem(RX_UNO, TX_UNO);
OneWire ds(ONE_WIRE_UNO);

void getTemp() {
	ds.reset();     // Initialization
  ds.write(0xCC); // Command Skip ROM to address all devices
	ds.write(0x44); // Command Convert T, Initiates temperature conversion
  delay(1000);    // maybe 750ms is enough, maybe not
  for (uint8_t s = 0; s < DEVICE_COUNT; s++) {
   	if (ds.reset()) {       // Initialization
      ds.select(deviceAddress[s]); // Select a device based on its address
      ds.write(0xBE);  // Read Scratchpad, temperature: byte 0: LSB, byte 1: MSB
      uint8_t scratchPad[9];
      for (int i = 0; i < 9; i++) {
	      scratchPad[i] = ds.read();
      }
      if (ds.crc8(scratchPad, 8) == scratchPad[SCRATCHPAD_CRC]) {
        currentTemperature[s] = (scratchPad[SCRATCHPAD_TEMP_MSB] << 8) | scratchPad[SCRATCHPAD_TEMP_LSB];
      } else {
        currentTemperature[s] = TEMP_NULL;  // 99,9 * 16
      }
    } else {
      currentTemperature[s] = TEMP_NULL;  // 99,9 * 16
    } 
  }
}

bool isResponseModem (unsigned long timeout = 500L, int mode = GSM_OK) {
  unsigned long finish =  millis() + timeout;
  bool resOK = false;
  do {
    char str[32];
    int len = modem.readBytesUntil('\n', str, 32);
    if (len > 1) {
      switch (mode) {
        case GSM_OK:
          resOK = (str[0] == 'O' && str[1] == 'K');
          break;
        case GSM_SMS:
          resOK = (str[0] == '>');
          break;
        default:
          resOK = false;
      }
    }
  } while (!resOK && millis() < finish);
  return resOK;
}

bool performModem(String command = "AT") {
  // выдача модему команды и контроль её исполнения
  for (int i = 0; i < 3; i++) {
    delay(200);
    modem.print(command);
    modem.write(GSM_NL);
    if ( isResponseModem() ) {
      delay(100);
// Serial.println("performModem done " + command);
      return true;
    }
  }
// Serial.println("performModem failed " + command);
  return false;
}

long sampleModem(long rate = MODEM_RATE) {
  // попытка связаться с модемом на скорости rate 
// Serial.println("sampleModem " + String(rate));
  modem.begin(rate);
  return performModem() ? rate : 0L;
}

long selectRate() {
  // перебор скоростей 
  long rateModem = 0;
  long rates[] = { 115200L, 57600L, 38400L, 19200L, 9600L, 14400L, 28800L };
  //long rates[] = { 9600L, 9600L};
  for (int i = 0; i < sizeof(rates)/sizeof(rates[0]); i++) {
    if (sampleModem(rates[i])) {
      rateModem = rates[i];
      break;
    }
  }
  return rateModem;
}

void connectModem() {
  // задача - обеспечить связь с модемом на скорости MODEM_RATE
  long rateModem = 0;
  // maximum ms to wait readBytesUntil(), по умолчанию 1000L
  modem.setTimeout(500L); 
  // постоянным свечением PWR_LED обозначим режим инициализации связи с модемом
  digitalWrite(PWR_LED_UNO, HIGH);
  // модему наверно нужно время для инициализации
  delay(5000); 
  // пока на установится связь с модемом на скорости MODEM_RATE
  do { 
    rateModem = sampleModem();
    if (!rateModem) {
      // первая попытка, на скорости MODEM_RATE, неудачна
      do {
        // попытаемся связаться на разных скоростей
        rateModem = selectRate();
      } while (!rateModem);
      if (rateModem != MODEM_RATE) {
        // командуем модему работать на штатной скорости MODEM_RATE
        performModem(String("AT&F"));
        performModem(String("AT+IPR=") + String(MODEM_RATE));
      }
    }
  } while (rateModem != MODEM_RATE);
  modem.print("AT&F"); modem.print(GSM_NL); isResponseModem(); // Set all current parameters to manufacturer defaults
  //modem.print("ATZ"); modem.print(GSM_NL); isResponseModem(); // Set all current parameters to user defined profile 
  //modem.print("AT+IPR=" + String(9600L)); modem.print(GSM_NL); isResponseModem(); // Set fixed local rate
  //modem.print("ATE0"); modem.print(GSM_NL); isResponseModem(); // Echo mode off 
  modem.print("AT+CLIP=1"); modem.print(GSM_NL); isResponseModem(); // Set caller ID on
  modem.print("AT+CMGF=1"); modem.print(GSM_NL); isResponseModem(); // Set SMS to text mode
  modem.print("AT+CSCS=GSM"); modem.print(GSM_NL); isResponseModem(); //  Character set of the mobile equipment
  //modem.print("AT&W"); modem.print(GSM_NL); isResponseModem(); // Stores current configuration to user defined profile
}

String isCall(unsigned long timeout = 500L, String command = "CLIP") {
  // при входящем вызове модем выдает периодически, где то пару раз в секунду, строки
  // RING
  // после первой строки RING модем выдаст однократно строку типа 
  // +CLIP: "7XXXXXXXXXX",145,"",,"",0 
  // а если послать запрос AT+CLCC то модем выдаст строку типа
  // +CLCC: 1,1,4,0,0,"7XXXXXXXXXX",145
  long finish = millis() + timeout;
  String number = "";
  do {
    if (modem.available()) {
      char str[32];
      str[0] = '\0';
      int len = modem.readBytesUntil('\n', str, 32);
      if (len) {
        str[len-1] = '\0'; // на позиции len-1 символ CR, '\r'
        String ring = String(str);
        int clip = ring.indexOf(String(command));
        if (clip >= 0) {
          int indexFrom = ring.indexOf(String('\"')) + 1;         //  8, если CLIP
          int indexTill = ring.indexOf(String('\"'), indexFrom);  // 19, если CLIP
          number = ring.substring(indexFrom, indexTill); 
          break;
        }
      }
    } else {
      delay(100);
    }
  } while (millis() < finish);
  return number;
}

// ==================== main =======================

void setup(void)
{
  delay(1000);
// Serial.begin(MONITOR_RATE);
  // значения температуры по датчикам за последние 24 часа в 99,9 С
  for (int s = 0; s < DEVICE_COUNT; s++) {
    for (int t = 0; t < 24; t++) {
      hourlyTemperature[s][t] = TEMP_NULL; // 99,9 * 2^4
    }
  }
  // индикатор что под тумблером PWR_KEY выключим
  pinMode(PWR_LED_UNO, OUTPUT); 
  digitalWrite(PWR_LED_UNO, LOW);
  // ждём включения тумблера по INPUT_PULLUP pin PWR_KEY_UNO значения LOW
  pinMode(PWR_KEY_UNO, INPUT_PULLUP);
  while (digitalRead(PWR_KEY_UNO)) {
    delay(200);
  }
  connectModem();
  
  checkTime = millis() + CALL_CHECK_INTERVAL;
  measureTime = millis() + HOUR;
}

void loop(void)
{ 
  if (millis() + HOUR < checkTime) {
    // в arduino счётчик миллисекунд millis() через 50 суток со старта сбрасывается в ноль
    checkTime = millis() + CALL_CHECK_INTERVAL;
    measureTime = millis() + HOUR;
  }
  if (millis() >= checkTime) {
    String number = isCall();
    if (number.length()) {
      // звонят, сверимся со списком телефонных номеров
      bool allow = false;
      String phone = "+7";
      for (int i = 0; i < USERS_COUNT; i++ ) {
        if (number.endsWith(comrade[i])) {
          phone += comrade[i];
          allow = true;
          break;
        }
      }
      delay(3000);
      // вешаем трубку
      modem.print("ATH"); modem.print(GSM_NL); isResponseModem(); 
      // Если есть допуск собираем показания датчиков и высылаем отчёт
      if (allow) {
        String answer = "";
        getTemp();
        for (int s = 0; s < DEVICE_COUNT; s++) {
          // минимальная за сутки температура по датчику
          int tmin = currentTemperature[s];
          for (int t = 0; t < 24; t++) {
            if (hourlyTemperature[s][t] < tmin) {
              tmin = hourlyTemperature[s][t];
            }
          }
          String tc = currentTemperature[s] == TEMP_NULL ? "---" : String((float) currentTemperature[s] / 16, 1);
          String tm = tmin == TEMP_NULL ? "---" : String((float) tmin / 16, 1);
          answer += deviceName[s] + tc + ", min " + tm + "\n";
        }
        // посылка SMS
        delay(100);
        char command[64];
        sprintf(command, "AT+CMGS=\"%s\"", phone.c_str());
        modem.write(command); modem.write(GSM_NL); isResponseModem(500L, GSM_SMS);
        modem.write(answer.c_str()); modem.write(GSM_NL);
        modem.write(CTRL_Z); modem.write(GSM_NL);
        modem.write(GSM_NL);
      }
    } else {
      // пока никто не звонит займёмся проверкой связи с модемом
      if (!performModem()) {
        // модем не отвечает, надо обеспечить соединие
        connectModem();
      }
      // модем на связи, индикатором мигаем
      if (digitalRead(PWR_KEY_UNO)) {
        // тумблер PWR_KEY отключён - нештатое положение - мигаем краткими импульсами
        digitalWrite(PWR_LED_UNO, HIGH);  delay(200);
        digitalWrite(PWR_LED_UNO, LOW); delay(200);
        digitalWrite(PWR_LED_UNO, HIGH); delay(200);
        digitalWrite(PWR_LED_UNO, LOW);
      } else {
        // тумблер PWR_KEY включён - штатое положение - мигаем длительными импульсами
        pwrled = !pwrled;
        digitalWrite(PWR_LED_UNO, pwrled);
      }
    }
    if (millis() >= measureTime) {
      // почасовые замеры за последние сутки
      getTemp();
      for (int s = 0; s < DEVICE_COUNT; s++) {
        for (int t = 23; t > 0; t--) {
          hourlyTemperature[s][t] = hourlyTemperature[s][t-1];
        }
        hourlyTemperature[s][0] = currentTemperature[s];
      }
      measureTime = millis() + HOUR;
    }
    checkTime = millis() + CALL_CHECK_INTERVAL;
  }
}
