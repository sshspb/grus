#include <SoftwareSerial.h>
#include <OneWire.h>
#include <LCDi2cW.h>                    

#define ONE_WIRE_UNO 2
#define RX_UNO 7
#define TX_UNO 8
#define PWR_KEY_UNO 11
#define PWR_LED_UNO 12
#define CALL_CHECK_INTERVAL 2000L
#define REPORT_INTERVAL 10000L
#define HOUR 3600000L
#define MODEM_RATE 9600L
#define DEVICE_COUNT 3
#define USERS_COUNT 3

#define GSM_OK 0
#define GSM_SMS 1
#define GSM_NL "\r\n"
#define CTRL_Z '\x1A'

#define SCRATCHPAD_TEMP_LSB 0
#define SCRATCHPAD_TEMP_MSB 1
#define SCRATCHPAD_CRC 8

byte deviceAddress[DEVICE_COUNT][8]  = {
  { 0x28, 0x6C, 0x8F, 0x53, 0x03, 0x00, 0x00, 0xB0 }, // #1/00m Sensor  0m grey hub
  { 0x28, 0xF9, 0xCD, 0x53, 0x03, 0x00, 0x00, 0x80 }, // #2/15m Sensor 15m white hub
  { 0x28, 0x27, 0x84, 0x53, 0x03, 0x00, 0x00, 0x4D }  // #3/05m Sensor  5m
};
int currentTemperature[DEVICE_COUNT];
int hourlyTemperature[DEVICE_COUNT][24];
const String comrade[USERS_COUNT] = {"9219258698", "9214201935", "9213303129"};
const int temp_null = 1598;  // (int) 99,9 * 16
bool pwrled = true;
unsigned long checkTime, reportTime, measureTime;
byte i, s, t;
String answer = "";

SoftwareSerial modem(RX_UNO, TX_UNO);
OneWire ds(ONE_WIRE_UNO);
LCDi2cW lcd = LCDi2cW(4,20,0x4C,0);

void getTemp() {
	ds.reset();     // Initialization
  ds.write(0xCC); // Command Skip ROM to address all devices
	ds.write(0x44); // Command Convert T, Initiates temperature conversion
  delay(1000);    // maybe 750ms is enough, maybe not
  for (s = 0; s < DEVICE_COUNT; s++) {
    currentTemperature[s] = temp_null;  // 99,9 * 16
   	if (ds.reset()) {  // Initialization
      ds.select(deviceAddress[s]); // Select a device based on its address
      ds.write(0xBE);  // Read Scratchpad, temperature: byte 0: LSB, byte 1: MSB
      byte scratchPad[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 , 0x00 };
      for (i = 0; i < 9; i++) {
       scratchPad[i] = ds.read();
      }
      if (ds.crc8(scratchPad, 8) == scratchPad[SCRATCHPAD_CRC]) {
        currentTemperature[s] = (scratchPad[SCRATCHPAD_TEMP_MSB] << 8) | scratchPad[SCRATCHPAD_TEMP_LSB];
      } 
    } 
  }

  long finish = millis() + 1000L;
  String csq = "";
  modem.print("AT+CSQ");
  modem.write(GSM_NL);
  do {
    if (modem.available()) {
      char str[32];
      str[0] = '\0';
      byte len = modem.readBytesUntil('\n', str, 32);
      if (len > 1 && str[0] == '+') {
        str[len-1] = '\0'; // на позиции len-1 символ CR, '\r'
        csq = String(str);
        csq = "SIGNAL QUALITY" + csq.substring(4, csq.indexOf(',')); 
        break;
      }
    } else {
      delay(100);
    }
  } while (millis() < finish);
  
  answer = csq + '\n';
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(csq.c_str());
  for (s = 0; s < DEVICE_COUNT; s++) {
    if (currentTemperature[s] != temp_null) {
      // минимальная за сутки температура по датчику
      int tmin = currentTemperature[s];
      for (t = 0; t < 24; t++) {
        if (hourlyTemperature[s][t] < tmin) {
          tmin = hourlyTemperature[s][t];
        }
      }
      String row = String(s+1)+": "+String((float) currentTemperature[s]/16, 1)+", min "+String((float) tmin/16, 1);
      answer += row + "\n";
      if (s < 4) {
        lcd.setCursor(s+1,0);
        lcd.print(row.c_str());
      }
    }
  }
}

bool isResponseModem (unsigned long timeout = 5000L, byte mode = GSM_OK) {
  unsigned long finish =  millis() + timeout;
  bool resOK = false;
  do {
    if (modem.available()) {
      char str[32];
      byte len = modem.readBytesUntil('\n', str, 32);
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
    } else {
      delay(100);
    }
  } while (!resOK && millis() < finish);
  return resOK;
}

bool performModem(String command = "AT") {
  // выдача модему команды и контроль её исполнения
  lcd.setCursor(0,19);
  for (i = 0; i < 3; i++) {
    modem.print(command);
    modem.write(GSM_NL);
    if ( isResponseModem() ) {
      lcd.print("Y");
      return true;
    }
  }
  lcd.print("N");
  return false;
}

long sampleModem(long rate = MODEM_RATE) {
  // попытка связаться с модемом на скорости rate 
  lcd.clear();
  delay(100); 
  lcd.setCursor(0,0);
  lcd.print("sampleModem " + String(rate));
  modem.begin(rate);
  return performModem() ? rate : 0L;
}

long selectRate() {
  // перебор скоростей 
  long rateModem = 0;
  //long rates[] = { 115200L, 57600L, 38400L, 19200L, 9600L, 14400L, 28800L };
  long rates[] = { 9600L, 9600L};
  for (i = 0; i < sizeof(rates)/sizeof(rates[0]); i++) {
    if (sampleModem(rates[i])) {
      rateModem = rates[i];
      break;
    }
  }
  return rateModem;
}

void connectModem() {
  // обеспечить связь с модемом на скорости MODEM_RATE
  long rateModem = 0;
  // постоянным свечением PWR_LED обозначим режим инициализации связи с модемом
  digitalWrite(PWR_LED_UNO, HIGH);
  // время для инициализации модема
  delay(10000); 
  do { 
    // пока на установится связь с модемом на скорости MODEM_RATE
    i = 0;
    while (!rateModem && i < 3) {
      i++;
      rateModem = sampleModem();
      delay(1000); 
    }
    if (!rateModem) {
      // попытка на скорости MODEM_RATE неудачна
      do {
        // попытаемся связаться на разных скоростей
        rateModem = selectRate();
      } while (!rateModem);
      if (rateModem != MODEM_RATE) {
        // командуем модему работать на штатной скорости MODEM_RATE
        performModem(String("AT&F")); 
        performModem(String("AT+IPR=") + String(MODEM_RATE)); 
        performModem("AT&W");
      }
    }
  } while (rateModem != MODEM_RATE);
  //modem.print("AT&F"); modem.print(GSM_NL); isResponseModem(); // Set all current parameters to manufacturer defaults
  //modem.print("ATZ"); modem.print(GSM_NL); isResponseModem(); // Set all current parameters to user defined profile 
  //modem.print("AT+IPR=" + String(9600L)); modem.print(GSM_NL); isResponseModem(); // Set fixed local rate
  //modem.print("ATE0"); modem.print(GSM_NL); isResponseModem(); // Echo mode off 
  modem.print("AT+CLIP=1"); modem.print(GSM_NL); isResponseModem(); // Set caller ID on
  modem.print("AT+CMGF=1"); modem.print(GSM_NL); isResponseModem(); // Set SMS to text mode
  modem.print("AT+CSCS=GSM"); modem.print(GSM_NL); isResponseModem(); //  Character set of the mobile equipment
  //modem.print("AT&W"); modem.print(GSM_NL); isResponseModem(); // Stores current configuration to user defined profile
}

String isCall() {
  // при входящем вызове модем выдает периодически, где то пару раз в секунду, строки
  // RING
  // после первой строки RING модем выдаст однократно строку типа 
  // +CLIP: "7XXXXXXXXXX",145,"",,"",0 
  // а если послать запрос AT+CLCC то модем выдаст строку типа
  // +CLCC: 1,1,4,0,0,"7XXXXXXXXXX",145
  long finish = millis() + 1000L;
  String number = "";
  do {
    if (modem.available()) {
      char str[32];
      str[0] = '\0';
      byte len = modem.readBytesUntil('\n', str, 32);
      if (len > 1) {
        str[len-1] = '\0'; // на позиции len-1 символ CR, '\r'
        String ring = String(str);
        int clip = ring.indexOf("CLIP");
        if (clip >= 0) {
          byte indexFrom = ring.indexOf(String('\"')) + 1;         //  8, если CLIP
          byte indexTill = ring.indexOf(String('\"'), indexFrom);  // 19, если CLIP
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
  lcd.init(); // Init the display, clears the display
  lcd.setCursor(0,0);
  lcd.print("Hello World!");
  // сброс в null значений температуры по датчикам за последние 24 часа
  for (s = 0; s < DEVICE_COUNT; s++) {
    for (t = 0; t < 24; t++) {
      hourlyTemperature[s][t] = temp_null; // 99,9 * 2^4
    }
  }
  // индикатор что под тумблером PWR выключим
  pinMode(PWR_LED_UNO, OUTPUT); 
  digitalWrite(PWR_LED_UNO, LOW);
  // ждём включения тумблера PWR по INPUT_PULLUP pin PWR_KEY_UNO значения LOW
  pinMode(PWR_KEY_UNO, INPUT_PULLUP);
  while (digitalRead(PWR_KEY_UNO)) {
    delay(200);
  }
  connectModem();
  getTemp();
  reportTime = millis() + REPORT_INTERVAL;
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
  if (millis() >= reportTime) {
    getTemp();
    reportTime = millis() + REPORT_INTERVAL;
  }
  if (!digitalRead(PWR_KEY_UNO)) {
    digitalWrite(PWR_LED_UNO, HIGH);
  }
  String number = isCall(); // timeout = 1000L
  digitalWrite(PWR_LED_UNO, LOW);
  if (number.length()) {
    // звонят, сверимся со списком телефонных номеров
    bool allow = false;
    String phone = "+7";
    for (i = 0; i < USERS_COUNT; i++ ) {
      if (number.endsWith(comrade[i])) {
        phone += comrade[i];
        allow = true;
        break;
      }
    }
    delay(3000);
    // вешаем трубку
    modem.print("ATH"); modem.print(GSM_NL); isResponseModem(); 
    if (allow) {
      // Если есть допуск высылаем SMS-отчёт 
      delay(100);
      char command[64];
      sprintf(command, "AT+CMGS=\"%s\"", phone.c_str());
      modem.write(command); modem.write(GSM_NL); isResponseModem(500L, GSM_SMS);
      modem.write(answer.c_str()); modem.write(GSM_NL);
      modem.write(CTRL_Z); modem.write(GSM_NL);
      modem.write(GSM_NL);
    }
  }
  if (millis() >= checkTime) {
    // проверка связи с модемом
    if (!performModem()) {
      // модем не отвечает, надо обеспечить соединение
      connectModem();
    }
    if (millis() >= measureTime) {
      // почасовые замеры за последние сутки
      for (s = 0; s < DEVICE_COUNT; s++) {
        for (t = 23; t > 0; t--) {
          hourlyTemperature[s][t] = hourlyTemperature[s][t-1];
        }
        hourlyTemperature[s][0] = currentTemperature[s];
      }
      measureTime = millis() + HOUR;
    }
    checkTime = millis() + CALL_CHECK_INTERVAL;
  }
}
