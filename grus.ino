#include <SoftwareSerial.h>
#include <OneWire.h>
#include <LCDi2cW.h>                    

#define ONE_WIRE_UNO 2
#define RX_UNO 7
#define TX_UNO 8
#define PWR_KEY_UNO 11
#define PWR_LED_UNO 12
#define REPORT_INTERVAL 10000L
#define MEASURE_INTERVAL 3600000L
#define DEVICE_COUNT 3
#define USERS_COUNT 3
#define GSM_OK 0
#define GSM_SM 1
#define GSM_CR '\r'
#define CTRL_Z '\x1A'

const char* comrade[USERS_COUNT] = {"+79219258698", "+79214201935", "+79213303129"};
const byte deviceAddress[DEVICE_COUNT][8]  = {
  { 0x28, 0x6C, 0x8F, 0x53, 0x03, 0x00, 0x00, 0xB0 }, // #1/00m Sensor  0m grey hub
  { 0x28, 0xF9, 0xCD, 0x53, 0x03, 0x00, 0x00, 0x80 }, // #2/15m Sensor 15m white hub
  { 0x28, 0x27, 0x84, 0x53, 0x03, 0x00, 0x00, 0x4D }  // #3/05m Sensor  5m
};
const int temp_null = 1598;  // (int) 99,9 * 16
int currentTemperature[DEVICE_COUNT];
int hourlyTemperature[DEVICE_COUNT][24];
unsigned long reportTime, measureTime, timeout;
char buff[32];
char answer[128];

SoftwareSerial modem(RX_UNO, TX_UNO);
OneWire ds(ONE_WIRE_UNO);
LCDi2cW lcd = LCDi2cW(4,20,0x4C,0);

void getTemp() {
	ds.reset();     // Initialization
  ds.write(0xCC); // Command Skip ROM to address all devices
	ds.write(0x44); // Command Convert T, Initiates temperature conversion
  delay(1000);    // maybe 750ms is enough, maybe not
  for (byte s = 0; s < DEVICE_COUNT; s++) {
    currentTemperature[s] = temp_null;  // 99,9 * 16
   	if (ds.reset()) {  // Initialization
      ds.select(deviceAddress[s]); // Select a device based on its address
      ds.write(0xBE);  // Read Scratchpad, temperature: byte 0: LSB, byte 1: MSB
      byte scratchPad[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 , 0x00 };
      for (byte i = 0; i < 9; i++) {
       scratchPad[i] = ds.read();
      }
      if (ds.crc8(scratchPad, 8) == scratchPad[8]) {
        currentTemperature[s] = (scratchPad[1] << 8) | scratchPad[0];
      } 
    } 
  }

  modem.write("AT+CSQ"); 
  modem.write(GSM_CR);
  buff[0] = answer[0] = '\0';
  timeout = millis() + 1000L;
  do {
    if (modem.available()) {
      byte len = modem.readBytesUntil('\n', buff, 32);
      if (len > 8 && buff[0] == '+' && buff[1] == 'C' && buff[2] == 'S' && buff[3] == 'Q') {
        break;
      } else {
        buff[0] = '\0';
      }
    }
  } while (millis() < timeout);
  lcd.clear();
  lcd.setCursor(0,0);
  if (buff[0] == '\0') {
    const char* fail = "SIGNAL FAIL";
    lcd.print(fail);
    sprintf(answer, "%s", fail);
  } else {
    char csq[3] = {buff[6], buff[7], '\0'};
    sprintf(buff, "SIGNAL QUALITY: %s", csq);
    lcd.print(buff);
    sprintf(answer, "%s", buff);
  }

  byte row = 1;
  for (byte s = 0; s < DEVICE_COUNT; s++) {
    if (currentTemperature[s] != temp_null) {
      // минимальная за сутки температура по датчику
      int tmin = currentTemperature[s];
      for (byte t = 0; t < 24; t++) {
        if (hourlyTemperature[s][t] < tmin) {
          tmin = hourlyTemperature[s][t];
        }
      }
      sprintf(buff, "%d: %3d,  min %3d", s+1, (int) floor(currentTemperature[s]/16), (int) floor(tmin/16) );
      sprintf(answer, "%s\n%s", answer, buff);
      if (row < 4) {
        lcd.setCursor(row++,0);
        lcd.print(buff);
      }
    }
  }
}

bool performModem(const char* command, byte mode, byte retry) {
  // выдача модему команды и контроль её исполнения
  bool resOK = false;
  for (byte i = 0; i < retry; i++) {
    lcd.setCursor(1,0);
    lcd.print(char(49+i));
    modem.write(command); 
    modem.write(GSM_CR);
    timeout = millis() + 2000L; 
    do {
      if (modem.available()) {
        byte len = modem.readBytesUntil('\n', buff, 32);
        if (len > 1) {
          switch (mode) {
            case GSM_OK:
              resOK = (buff[0] == 'O' && buff[1] == 'K');
              break;
            case GSM_SM:
              resOK = (buff[0] == '>');
              break;
            default:
              resOK = false;
          }
        }
        if (resOK) {
          return true;
        }
      }
    } while (millis() < timeout);
  }   
  return false;
}

void beginModem(long rate) {
  lcd.clear(); 
  lcd.setCursor(0,0);
  sprintf(buff, "sampleModem %ld", rate);
  lcd.print(buff);
//  Serial.println(buff);
  modem.begin(rate);
}

void connectModem() {
  digitalWrite(PWR_LED_UNO, HIGH);
  delay(10000); 
  while (true) { 
    // обеспечить связь с модемом на скорости 9600
    beginModem(9600L);
    if (performModem("AT", GSM_OK, 5)) {
      break;
    } else {
      // на 9600 не вышло, пробуем другие
      bool responseModem = false;
      while (!responseModem) {
        long rates[7] = { 115200L, 57600L, 38400L, 19200L, 9600L, 14400L, 28800L };
        for (byte i = 0; i < 7; i++) {
          beginModem(rates[i]);
          responseModem = performModem("AT", GSM_OK, 1);
          if (!responseModem) responseModem = performModem("AT", GSM_OK, 1);
          if (!responseModem) responseModem = performModem("AT", GSM_OK, 1);
          if (!responseModem) responseModem = performModem("AT", GSM_OK, 1);
          if (responseModem) {
            performModem("AT&F", GSM_OK, 1); // Set all current parameters to manufacturer defaults
            performModem("AT+IPR=9600", GSM_OK, 1); // Set fixed local rate
            performModem("AT&W", GSM_OK, 1); // Stores current configuration to user defined profile
            break;
          }
        }
      }
    }
  }
  //performModem("AT&F", GSM_OK, 1);      // Set all current parameters to manufacturer defaults
  //performModem("ATZ", GSM_OK, 1);       // Set all current parameters to user defined profile 
  performModem("ATE0", GSM_OK, 1);        // Echo mode off 
  performModem("AT+CLIP=1", GSM_OK, 1);   // Set caller ID on
  performModem("AT+CMGF=1", GSM_OK, 1);   // Set SMS to text mode
  performModem("AT+CSCS=GSM", GSM_OK, 1); //  Character set of the mobile equipment
  performModem("AT+IPR=9600", GSM_OK, 1); // Set fixed local rate
  performModem("AT&W", GSM_OK, 1);      // Stores current configuration to user defined profile
}

void isCall() {
  // при входящем вызове модем выдает периодически, где то пару раз в секунду, строки
  // RING
  // после первой строки RING модем выдаст однократно строку типа 
  // +CLIP: "7XXXXXXXXXX",145,"",,"",0 
  // а если послать запрос AT+CLCC то модем выдаст строку типа
  // +CLCC: 1,1,4,0,0,"7XXXXXXXXXX",145
  timeout = millis() + 1000L; 
  char eol = '\0';
  char* number = &eol;
  char buf[64];
  byte len; 
  bool allow;
  do {
    if (modem.available()) {
      len = modem.readBytesUntil('\n', buff, 64);
      if (len > 19 && buff[1] == 'C' && buff[2] == 'L' && buff[3] == 'I' && buff[4] == 'P') {
        buff[19] = '\0';
        number = &buff[8];
        break;
      }
    } else {
      delay(10);
    }
  } while (millis() < timeout);

  if (number[0] != '\0') {
    delay(3000);
    modem.write("ATH"); 
    modem.write(GSM_CR);
    for (byte i = 0; i < USERS_COUNT; i++ ) {
      allow = true;
      for (byte j = 1; j < 11; j++) {
        allow = allow && number[j] == comrade[i][j+1];
      }
      if (allow) {
        sprintf(buff, "AT+CMGS=\"%s\"", comrade[i]);
        if (performModem(buff, GSM_SM, 1)) {
          modem.write(answer); 
          modem.write(CTRL_Z); 
          delay(9000);
        }
        break;
      }
    }
  }
}

// ==================== main =======================

void setup(void)
{
//  Serial.begin(9600);
  delay(1000);
  lcd.init(); // Init the display, clears the display
  lcd.setCursor(0,0);
  lcd.print("Hello World!");
  // сброс в null значений температуры по датчикам за последние 24 часа
  for (byte s = 0; s < DEVICE_COUNT; s++) {
    for (byte t = 0; t < 24; t++) {
      hourlyTemperature[s][t] = temp_null; // 99,9 * 2^4
    }
  }
  // индикатор что под тумблером PWR выключим
  pinMode(PWR_LED_UNO, OUTPUT); 
  digitalWrite(PWR_LED_UNO, LOW);
  // ждём включения тумблера PWR по INPUT_PULLUP pin PWR_KEY_UNO значения LOW
  pinMode(PWR_KEY_UNO, INPUT_PULLUP);
  while (digitalRead(PWR_KEY_UNO)) {
    delay(100);
  }
  connectModem();
  getTemp();
  reportTime = millis() + REPORT_INTERVAL;
  measureTime = millis() + MEASURE_INTERVAL;
}

void loop(void)
{ 
  if (millis() + MEASURE_INTERVAL + MEASURE_INTERVAL < measureTime) {
    // в arduino счётчик миллисекунд millis() через 50 суток со старта сбрасывается в ноль
    reportTime = millis() + REPORT_INTERVAL;
    measureTime = millis() + MEASURE_INTERVAL;
  }

  if (millis() > reportTime) {
    getTemp();
    reportTime = millis() + REPORT_INTERVAL;
  }

  if (!digitalRead(PWR_KEY_UNO)) digitalWrite(PWR_LED_UNO, HIGH);
  isCall(); // timeout 1000L
  digitalWrite(PWR_LED_UNO, LOW);

  if (millis() > measureTime) {
    if (!performModem("AT", GSM_OK, 1)) connectModem(); // проверка связи с модемом
    // почасовые замеры за последние сутки
    for (byte s = 0; s < DEVICE_COUNT; s++) {
      for (byte t = 23; t > 0; t--) {
        hourlyTemperature[s][t] = hourlyTemperature[s][t-1];
      }
      hourlyTemperature[s][0] = currentTemperature[s];
     }
    measureTime = millis() + MEASURE_INTERVAL;
  }
}
