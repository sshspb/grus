#include <SoftwareSerial.h>
#include <OneWire.h>
#include <LCDi2cW.h>                    

#define ONE_WIRE_UNO 2
#define RX_UNO 7
#define TX_UNO 8
#define PWR_KEY_UNO 11
#define PWR_LED_UNO 12
#define GSM_OK 0
#define GSM_SM 1
#define GSM_CR "\r\n"
#define CTRL_Z '\x1A'
#define BUFF_SIZE 64
#define REPORT_INTERVAL 10000L
#define MEASURE_INTERVAL 3600000L
#define DEVICE_COUNT 4
#define USERS_COUNT 3

const char* comrade[USERS_COUNT] = {"+79876543210", "+79876543210", "+79876543210"};
const byte deviceAddress[DEVICE_COUNT][8]  = {
  { 0x28, 0x6C, 0x8F, 0x53, 0x03, 0x00, 0x00, 0xB0 }, // #1 Sensor  0m grey hub
  { 0x28, 0xF9, 0xCD, 0x53, 0x03, 0x00, 0x00, 0x80 }, // #2 Sensor 15m white hub
  { 0x28, 0x27, 0x84, 0x53, 0x03, 0x00, 0x00, 0x4D }, // #3 Sensor  5m
  { 0x28, 0xDA, 0xAF, 0x53, 0x03, 0x00, 0x00, 0xFD }  // #4 Sensor  5m
};
const int temp_null = 1598;  // (int) 99,9 * 16
int currentTemperature[DEVICE_COUNT];
int hourlyTemperature[DEVICE_COUNT][24];
unsigned long reportTime, measureTime, timeout;

char csq[3] = {'\0', '\0', '\0'};
char answer[20*(DEVICE_COUNT+1)];

SoftwareSerial modem(RX_UNO, TX_UNO);
OneWire ds(ONE_WIRE_UNO);
LCDi2cW lcd = LCDi2cW(4,20,0x4C,0);

void getTemp() {
  char buff[BUFF_SIZE];
  byte connected = 0;
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
        connected++;
      } 
    } 
  }

  modem.write("AT+CSQ"); 
  modem.write(GSM_CR);
  buff[0] = answer[0] = '\0';
  timeout = millis() + 1000L;
  do {
    if (modem.available()) {
      byte len = modem.readBytesUntil('\n', buff, BUFF_SIZE);
      if (len > 8 && buff[0] == '+' && buff[1] == 'C' && buff[2] == 'S' && buff[3] == 'Q') {
        break;
      } else {
        buff[0] = '\0';
      }
    }
  } while (millis() < timeout);
  lcd.clear();
  byte row = 0;
  if (buff[0] == '\0') {
    const char* fail = "SIGNAL FAIL";
    lcd.setCursor(row++, 0);
    lcd.print(fail);
    sprintf(answer, "%s", fail);
  } else {
    csq[0] = buff[6]; csq[1] = buff[7]; csq[2] = '\0';
    sprintf(buff, "SIGNAL Q: %s", csq);
    if (connected < 4) {
      lcd.setCursor(row++, 0);
      lcd.print(buff);
    }
    sprintf(answer, "%s", buff);
  }

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
        lcd.setCursor(row++, 0);
        lcd.print(buff);
      }
    }
  }
//Serial.print("begin:::"); Serial.print(answer); Serial.println(":::end");
}

bool performModem(const char* command, byte mode, byte retry) {
  // выдача модему команды и контроль её исполнения
  char buff[BUFF_SIZE];
  bool resOK = false;
  for (byte i = 0; i < retry; i++) {
    lcd.setCursor(0,19);
    lcd.print(char(49+i));
    modem.write(command); 
    modem.write(GSM_CR);
    delay(10);
    timeout = millis() + 1000L; 
    do {
      if (modem.available()) {
        byte len = modem.readBytesUntil('\n', buff, BUFF_SIZE);
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
  char buff[BUFF_SIZE];
  lcd.clear(); 
  lcd.setCursor(0,0);
  sprintf(buff, "sample %ld", rate);
  lcd.print(buff);
  modem.begin(rate);
  delay(10);
}

void connectModem() {
  digitalWrite(PWR_LED_UNO, HIGH);
  delay(10000); 
  while (true) { 
    // обеспечить связь с модемом на скорости 9600
    beginModem(9600L);
    if (performModem("AT", GSM_OK, 10)) {
      break;
    } else {
      // на 9600 не вышло, пробуем другие
      bool responseModem = false;
      while (!responseModem) {
        long rates[7] = { 115200L, 57600L, 38400L, 19200L, 9600L, 14400L, 28800L };
        for (byte i = 0; i < 7; i++) {
          beginModem(rates[i]);
          responseModem = performModem("AT", GSM_OK, 5);
          if (responseModem) {
            performModem("AT&F", GSM_OK, 1);  // Factory
            performModem("AT+IPR=9600", GSM_OK, 1); // Set fixed local rate
            break;
          }
        }
      }
    }
  }
  //performModem("AT&F", GSM_OK, 1);      // Set all current parameters to manufacturer defaults
  //performModem("ATZ", GSM_OK, 1);       // Set all current parameters to user defined profile 
  //performModem("AT&FZE0", GSM_OK, 1);     // Factory + Reset + Echo Off 
  performModem("AT+IPR=9600", GSM_OK, 1); // Set fixed local rate
  performModem("ATE0", GSM_OK, 1);        // Echo mode off 
  performModem("AT+CLIP=1", GSM_OK, 1);   // Set caller ID on
  performModem("AT+CMGF=1", GSM_OK, 1);   // Set SMS to text mode
  performModem("AT+CSCS=GSM", GSM_OK, 1); //  Character set of the mobile equipment
  //performModem("AT&W", GSM_OK, 1);        // Stores current configuration to user defined profile
}

void isCall() {
  // при входящем вызове модем выдает несколько раз в секунду строку
  // RING
  // после первой строки RING модем выдаст однократно строку типа 
  // +CLIP: "7XXXXXXXXXX",145,"",,"",0 
  timeout = millis() + 1000L; 
  char buff[BUFF_SIZE];
  char eol = '\0';
  char* number = &eol;
  byte len; 
  bool allow;
  do {
    if (modem.available()) {
      len = modem.readBytesUntil('\n', buff, BUFF_SIZE - 1);
      buff[len] = '\0';
//Serial.print(":::"); Serial.print(buff); Serial.print(":::"); Serial.println(len);
      if (len > 19 && buff[1] == 'C' && buff[2] == 'L' && buff[3] == 'I' && buff[4] == 'P') {
        buff[19] = '\0';
        number = &buff[8];
        break;
      }
    }
  } while (millis() < timeout);

  if (number[0] != '\0') {
    delay(3000);
    modem.write("ATH\r\n"); 
    for (byte i = 0; i < USERS_COUNT; i++ ) {
      allow = true;
      for (byte j = 1; j < 11; j++) {
        allow = allow && number[j] == comrade[i][j+1];
      }
      if (allow) {
        sprintf(buff, "AT+CMGS=\"%s\"", comrade[i]);
//Serial.println(buff); Serial.print(answer); Serial.println("--end of answer--");

        if (performModem(buff, GSM_SM, 1)) {
          modem.write(answer); 
          modem.write(CTRL_Z); 
          modem.write("\r\n\r\n"); 
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
//Serial.begin(9600);
  delay(1000);
  lcd.init();
  lcd.setCursor(0,0);
  lcd.print("Hello World!");
  // сброс значений температуры по датчикам за последние 24 часа
  for (byte s = 0; s < DEVICE_COUNT; s++) {
    for (byte t = 0; t < 24; t++) {
      hourlyTemperature[s][t] = temp_null; // 99,9 * 2^4
    }
  }
  // индикатор что под правым тумблером
  pinMode(PWR_LED_UNO, OUTPUT); 
  digitalWrite(PWR_LED_UNO, LOW);
  // ждём включения правого тумблера по INPUT_PULLUP pin PWR_KEY_UNO значению LOW
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

  isCall(); // timeout 1000L
  
  if (millis() > reportTime) {
    digitalWrite(PWR_LED_UNO, LOW);
    if (!performModem("AT", GSM_OK, 1)) connectModem(); // проверка связи с модемом
    getTemp(); // замер температуры и формирование отчёта answer
    if (millis() > measureTime) {
      // почасовые замеры за последние сутки
      for (byte s = 0; s < DEVICE_COUNT; s++) {
        for (byte t = 23; t > 0; t--) {
          hourlyTemperature[s][t] = hourlyTemperature[s][t-1];
        }
        hourlyTemperature[s][0] = currentTemperature[s];
       }
      measureTime = millis() + MEASURE_INTERVAL;
    }
    if (!digitalRead(PWR_KEY_UNO)) digitalWrite(PWR_LED_UNO, HIGH);
    reportTime = millis() + REPORT_INTERVAL;
  }
}
