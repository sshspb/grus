#include <OneWire.h>
#include <LCDi2cW.h>                    

#define ONE_WIRE_UNO 2
#define PWR_KEY_UNO 11
#define PWR_LED_UNO 12
#define GSM_OK 0
#define GSM_SM 1
#define GSM_RG 2
#define GSM_CR "\r\n"
#define CTRL_Z '\x1A'
#define ESCAPE '\x1B'
#define BUFF_SIZE 64
#define REPORT_INTERVAL 60000L
#define MEASURE_INTERVAL 14400000L
#define SENSORS_COUNT 5
#define USERS_COUNT 5
#define MODEM_RATE 115200L

const char* comrade[USERS_COUNT] = {"+7XXXXXXXXXX", "+7XXXXXXXXXX", "+7XXXXXXXXXX", "+7XXXXXXXXXX", "+7XXXXXXXXXX"};
/*
const byte deviceAddress[DEVICE_COUNT][8]  = {
  { 0x28, 0x6C, 0x8F, 0x53, 0x03, 0x00, 0x00, 0xB0 }, // #1 Sensor 0m grey hub
  { 0x28, 0xF9, 0xCD, 0x53, 0x03, 0x00, 0x00, 0x80 }, // #2 Sensor black 15m white hub
  { 0x28, 0x42, 0x9E, 0x53, 0x03, 0x00, 0x00, 0xA4 }, // #3 Sensor white 10m
  { 0x28, 0xDA, 0xAF, 0x53, 0x03, 0x00, 0x00, 0xFD }, // #4 Sensor black  5m
  { 0x28, 0xC2, 0x9A, 0x53, 0x03, 0x00, 0x00, 0x51 }  // #5 Sensor white 10m
};
*/
const unsigned int temp_null = 1584;  // (int) 99 * 16  = 0x063E
// int sensors [0] .. [5] температура с интервалом времени 4 часа
// int sensors [6] текущая температура
// int sensors [7] два младших байта серийного номера датчика
unsigned int sensors[SENSORS_COUNT][8]  = {
  { 1584, 1584, 1584, 1584, 1584, 1584, 1584, 36716}, // #1 { 0x6C, 0x8F } Sensor 0m grey hub
  { 1584, 1584, 1584, 1584, 1584, 1584, 1584, 52729}, // #2 { 0xF9, 0xCD } Sensor black 15m white hub
  { 1584, 1584, 1584, 1584, 1584, 1584, 1584, 40514}, // #3 { 0x42, 0x9E } Sensor white 10m
  { 1584, 1584, 1584, 1584, 1584, 1584, 1584, 45018}, // #4 { 0xDA, 0xAF } Sensor black  5m
  { 1584, 1584, 1584, 1584, 1584, 1584, 1584, 39618}  // #5 { 0xC2, 0x9A } Sensor white 10m
};
unsigned long measureTime, timeout;
bool isEven = true;
byte connected = 0;
char strCSQ[32];

OneWire ds(ONE_WIRE_UNO);
LCDi2cW lcd = LCDi2cW(4,20,0x4C,0);

void getSignalQuality (bool sms) {
  char buff[BUFF_SIZE];
  bool isResponse = false;
  timeout = millis() + 1000L;
  while (Serial.available() > 0 && millis() < timeout) {
    Serial.read();
  }
  Serial.write("AT+CSQ"); 
  Serial.write(GSM_CR);
  timeout = millis() + 1000L;
  do {
    if (Serial.available()) {
      byte len = Serial.readBytesUntil('\n', buff, BUFF_SIZE);
      if (len > 8 && buff[0] == '+' && buff[1] == 'C' && buff[2] == 'S' && buff[3] == 'Q') {
        isResponse = true;
        break;
      }
    }
  } while (millis() < timeout);
  sprintf(strCSQ, "SIGNAL Q:%c%c", isResponse ? buff[6] : '?', isResponse ? buff[7] : '?');
  if (sms) {
    Serial.write(strCSQ);
  } else {
    lcd.setCursor(0, 0);
    lcd.print(strCSQ);
  }
}

void getTemp(bool sms) {
  char str[32];
  byte addr[8], scratchPad[9];
  byte snew, firstDev, countDev, row;
  unsigned int serialNumber, currentTemperature, tmin;
  byte snpp;
  snew = 0;       // счётчик нештатных датчиков
  firstDev = isEven || connected < 4 ? 0 : connected - 4; // первый отображаемый на LCD
  row = isEven || connected < 4 ? 1 : 0;  // строка на LCD
  countDev = 0;   // счётчик отображаемых на LCD
  connected = 0;  // счётчик подключённых датчиков
	ds.reset();     // Initialization
  ds.write(0xCC); // Command Skip ROM to address all devices
	ds.write(0x44); // Command Convert T, Initiates temperature conversion
  delay(1000);    // maybe 750ms is enough, maybe not
  if (!sms) lcd.clear();
  ds.reset_search();
  while (ds.search(addr)) {
    if (OneWire::crc8(addr, 7) == addr[7]) {
      connected++;
      serialNumber = (addr[2] << 8) | addr[1];
      snpp = SENSORS_COUNT;
      for (byte s = 0; s < SENSORS_COUNT; s++) {
        if (serialNumber == sensors[s][7]) {
          snpp = s;
          break;
        }
      }
      if (snpp == SENSORS_COUNT) {
        snpp = 0x10 + snew++;
      }
   	  ds.reset();
      ds.select(addr); // Select a device based on its address
      ds.write(0xBE);  // Read Scratchpad, temperature: byte 0: LSB, byte 1: MSB
      scratchPad[0] = 0x3E; // temp_null LSB
      scratchPad[1] = 0x06; // temp_null MSB
      scratchPad[8] = 0x00; // CRC
      for (byte i = 0; i < 9; i++) {
       scratchPad[i] = ds.read();
      }
      if (ds.crc8(scratchPad, 8) == scratchPad[8]) {
        currentTemperature = (scratchPad[1] << 8) | scratchPad[0];
      } else {
        currentTemperature = temp_null;  // 99,9 * 16
      }

      if (snpp < 0x10) { 
        // если датчик штатный 
        sensors[snpp][6] = currentTemperature;
        // минимальная температура за последний четырёхчасовой интервал
        if (sensors[snpp][5] > currentTemperature) {
          sensors[snpp][5] = currentTemperature;
        }
        // минимальная за сутки температура по датчику
        tmin = currentTemperature;
        for (byte t = 0; t < 8; t++) {
          if (sensors[snpp][t] < tmin) {
            tmin = sensors[snpp][t];
          }
        }
        sprintf(str, "%c:%3d, min%3d", (char) (0x31 + snpp), (int) floor(currentTemperature/16), (int) floor(tmin/16) );
      } else {
        // датчик не штатный
        sprintf(str, "%c:%3d", (char) (0x31 + snpp), (int) floor(currentTemperature/16));
      }
      if (sms) {
        Serial.write('\n');
        Serial.write(str);
      } else {
        // если больше трёх датчиков подключено отображаем
        // поочерёдно первые три датчика / последние три
        if (++countDev > firstDev && row < 4) {
          lcd.setCursor(row++, 0);
          lcd.print(str);
        }
      }
    }
  }
  isEven = !isEven;
}

/*
 * выдача модему команды и контроль её исполнения
 * command команда
 * mode    ожидаемый ответ
 * retry   количество запросов
 * waitok  количество подряд верных ответов
 */
bool performModem(const char* command, byte mode, byte retry, byte waitok) {
  char creg1, creg2, creg3;
  char buff[BUFF_SIZE];
  byte countok = 0;
  bool resOK = false;
  timeout = millis() + 1000L;
  while (Serial.available() > 0 && millis() < timeout) {
    Serial.read();
  }
  for (byte i = 0; i < retry; i++) {
    Serial.write(command); 
    Serial.write(GSM_CR);
    resOK = false;
    timeout = millis() + 2000L; // ждём ответа 2 сек
    do {
      if (Serial.available()) {
        byte len = Serial.readBytesUntil('\n', buff, BUFF_SIZE);
        if (len > 1) {
          switch (mode) {
            case GSM_OK:
              resOK = resOK || (buff[0] == 'O' && buff[1] == 'K');
              break;
            case GSM_SM:
              resOK = resOK || (buff[0] == '>');
              break;
            case GSM_RG:
              // Ждём ответа +CREG: 1,1
              if (len > 9 && buff[0] == '+' && buff[1] == 'C' && buff[2] == 'R' && buff[3] == 'E' && buff[4] == 'G') {
                creg1 = buff[7]; 
                creg2 = buff[9]; 
                creg3 = (len > 10 && buff[10] > 32) ? buff[10] : ' ';
                resOK = resOK || (creg1 == '1' && creg2 == '1' && creg3 == ' ');
                lcd.setCursor(0,17); lcd.print(creg2);
                lcd.setCursor(0,18); lcd.print(creg3);
              }
              break;
            default:
              resOK = false;
          }
        }
      }
    } while (!resOK && millis() < timeout);
    if (resOK) countok++; else countok = 0;
    if (countok >= waitok) break;
  }
  return resOK && countok >= waitok;
}

void connectModem() {
  char str[32];
  digitalWrite(PWR_LED_UNO, HIGH);
  delay(15000); 
  lcd.clear(); 
  lcd.setCursor(0,0);
  sprintf(str, "sample %ld", MODEM_RATE);
  lcd.print(str);

  Serial.begin(MODEM_RATE);
  delay(5000);

  lcd.setCursor(1,0);
  lcd.print(F("connection"));
  while (!performModem("AT", GSM_OK, 20, 5)) {} 
  performModem("AT&F", GSM_OK, 1, 1);           
  while (!performModem("AT", GSM_OK, 20, 5)) {} 
 
  lcd.setCursor(2,0);
  lcd.print(F("registration"));
//  Serial.write("AT+COPS=4,2,\"25002\""); Serial.write(GSM_CR); delay(5000);
  while (!performModem("AT+CREG?", GSM_RG, 1, 1)) {} 

  lcd.setCursor(3,0);
  lcd.print(F("initial setup"));
  while (!performModem("ATE0", GSM_OK, 1, 1)) {}            // Set echo mode off 
  while (!performModem("AT+CLIP=1", GSM_OK, 1, 1)) {}       // Set caller ID on
  while (!performModem("AT+CMGF=1", GSM_OK, 1, 1)) {}       // Set SMS to text mode
  while (!performModem("AT+CSCS=\"GSM\"", GSM_OK, 1, 1)) {} // Character set of the mobile equipment
  lcd.setCursor(3,17);
  lcd.print(F("OK"));
}

void isCall() {
  // при входящем вызове модем выдает несколько раз в секунду строку
  // RING
  // после первой строки RING модем выдаст однократно строку типа 
  // +CLIP: "7XXXXXXXXXX",145,"",,"",0 
  // а если послать запрос AT+CLCC то модем выдаст строку типа
  // +CLCC: 1,1,4,0,0,"7XXXXXXXXXX",145
  char buff[BUFF_SIZE];
  char str[32];
  char eol = '\0';
  char* number = &eol;
  byte len, ring = 0;
  bool allow;

  timeout = millis() + REPORT_INTERVAL; 
  do {
    if (Serial.available()) {
      len = Serial.readBytesUntil('\n', buff, BUFF_SIZE);
      if (len > 3 && buff[0] == 'R' && buff[1] == 'I' && buff[2] == 'N' && buff[3] == 'G') {
        ring++;
        if (ring == 2) {
          Serial.write("AT+CLCC"); 
          Serial.write(GSM_CR);
        }
      } else if (len > 19 && buff[1] == 'C' && buff[2] == 'L' && buff[3] == 'I' && buff[4] == 'P') {
        buff[19] = '\0';
        number = &buff[8];
        lcd.setCursor(0,0); lcd.print(' ');
        lcd.setCursor(0,1); lcd.print(number);
        break;
      } else if (len > 29 && buff[1] == 'C' && buff[2] == 'L' && buff[3] == 'C' && buff[4] == 'C') {
        buff[29] = '\0';
        number = &buff[18];
        lcd.setCursor(1,0); lcd.print(' ');
        lcd.setCursor(1,1); lcd.print(number);
        break;
      }
    }
  } while (millis() < timeout);

  if (number[0] != '\0') {
    delay(5000);
    performModem("ATH", GSM_OK, 1, 1);
    for (byte i = 0; i < USERS_COUNT; i++ ) {
      allow = true;
      for (byte j = 1; j < 11; j++) {
        allow = allow && number[j] == comrade[i][j+1];
      }
      if (allow) {
        lcd.setCursor(2,0); lcd.print(comrade[i]);
        delay(5000);
        sprintf(str, "AT+CMGS=\"%s\"", comrade[i]);
        if (performModem(str, GSM_SM, 1, 1)) {
          Serial.write(strCSQ); 
          getTemp(true);
          Serial.write(CTRL_Z); 
        } else {
          Serial.write(ESCAPE); 
        }
        Serial.write(GSM_CR); 
        delay(5000);
        break;
      }
    }
  }

}

// ==================== main =======================

void setup(void)
{
  lcd.init();
  lcd.setCursor(0,0);
  lcd.print(F("Hello World!"));
  strCSQ[0] = '\0';
  // индикатор что под правым тумблером
  pinMode(PWR_LED_UNO, OUTPUT); 
  digitalWrite(PWR_LED_UNO, LOW);
  // ждём включения правого тумблера по INPUT_PULLUP pin PWR_KEY_UNO значению LOW
  pinMode(PWR_KEY_UNO, INPUT_PULLUP);
  while (digitalRead(PWR_KEY_UNO)) {
    delay(100);
  }
  connectModem();
  measureTime = millis();
}

void loop(void) { 

  isCall(); // timeout REPORT_INTERVAL
  
  digitalWrite(PWR_LED_UNO, LOW);
  // замер температуры и формирование отчёта answer
  getTemp(false); // timeout 1 с
  // проверка модема
  if (!performModem("AT", GSM_OK, 1, 1)) {
    connectModem(); 
  } 
  if (connected < 4 || !isEven) getSignalQuality(false);
  if (!digitalRead(PWR_KEY_UNO)) digitalWrite(PWR_LED_UNO, HIGH);

  if (millis() > measureTime) {
    // проверка регистрации в сети Мегафона
    if (!performModem("AT+CREG?", GSM_RG, 1, 1)) {
      connectModem(); 
    } 
    for (byte s = 0; s < SENSORS_COUNT; s++) {
      for (byte t = 0; t < 5; t++) {
        // сдвигаем замеры пяти интервалов за последние сутки
        sensors[s][t] = sensors[s][t+1];
      }
      // в шестой интервал вносим текущую температуру
      sensors[s][5] = sensors[s][6];
     }
    measureTime += MEASURE_INTERVAL;
  }

}
