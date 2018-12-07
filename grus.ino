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
#define REPORT_INTERVAL 30000L
#define MEASURE_INTERVAL 3600000L
#define MESSAGE_INTERVAL 86400000L
#define DEVICE_COUNT 4
#define USERS_COUNT 3
#define MODEM_RATE 115200L

const char* comrade[USERS_COUNT] = {"+7ХХХХХХХХХХ", "+7ХХХХХХХХХХ", "+7ХХХХХХХХХХ"};
const byte deviceAddress[DEVICE_COUNT][8]  = {
  { 0x28, 0x6C, 0x8F, 0x53, 0x03, 0x00, 0x00, 0xB0 }, // #1 Sensor  0m grey hub
  { 0x28, 0xF9, 0xCD, 0x53, 0x03, 0x00, 0x00, 0x80 }, // #2 Sensor 15m white hub
  { 0x28, 0x27, 0x84, 0x53, 0x03, 0x00, 0x00, 0x4D }, // #3 Sensor  5m
  { 0x28, 0xDA, 0xAF, 0x53, 0x03, 0x00, 0x00, 0xFD }  // #4 Sensor  5m
};
const int temp_null = 1598;  // (int) 99,9 * 16
int currentTemperature[DEVICE_COUNT];
int hourlyTemperature[DEVICE_COUNT][24];
unsigned long measureTime, messageTime, timeout;
char creg1 = '?', creg2 = '?', creg3 = '?';
char buff[BUFF_SIZE];
char answer[DEVICE_COUNT+1][23];
bool isEven = true;
byte connected = 0;

OneWire ds(ONE_WIRE_UNO);
LCDi2cW lcd = LCDi2cW(4,20,0x4C,0);

void getSignalQuality () {
  bool isReq = false;
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
        isReq = true;
        break;
      }
    }
  } while (millis() < timeout);
  char csqm = isReq ? buff[6] : '?';
  char csql = isReq ? buff[7] : '?';
  if (creg3 == ' ') sprintf(buff, "SIGNAL Q:%c%c", csqm, csql);
  else sprintf(buff, "SIGNAL Q:%c%c R:%c,%c%c", csqm, csql, creg1, creg2, creg3);
  lcd.setCursor(0, 0);
  lcd.print(buff);
  sprintf(answer[0], "%s", buff);
}

void getTemp() {
  connected = 0;
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
  
  lcd.clear();
  byte row = 1;
  byte countDev = 0;
  byte firstDev = isEven || connected < 4 ? 0 : connected - 3;;
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
      sprintf(answer[row], "\n%s", buff);
      // на случай если больше трёх датчиков подключено отображаем первые три датчика / последние три
      if (++countDev > firstDev && row < 4) {
        lcd.setCursor(row++, 0);
        lcd.print(buff);
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
  digitalWrite(PWR_LED_UNO, HIGH);
  delay(15000); 
  lcd.clear(); 
  lcd.setCursor(0,0);
  sprintf(buff, "sample %ld", MODEM_RATE);
  lcd.print(buff);

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
  char eol = '\0';
  char* number = &eol;
  byte len;
  bool allow, first = true;

  timeout = millis() + REPORT_INTERVAL; 
  do {
    if (Serial.available()) {
      len = Serial.readBytesUntil('\n', buff, BUFF_SIZE);
      if (first && len > 3 && buff[0] == 'R' && buff[1] == 'I' && buff[2] == 'N' && buff[3] == 'G') {
        Serial.write("AT+CLCC"); 
        Serial.write(GSM_CR);
        first = false;
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
        sprintf(buff, "AT+CMGS=\"%s\"", comrade[i]);
        if (performModem(buff, GSM_SM, 1, 1)) {
          for (byte i = 0; i <= connected; i++ ) {
            Serial.write(answer[i]); 
          }
          Serial.write(CTRL_Z); 
        } else {
          Serial.write(ESCAPE); 
        }
        Serial.write(GSM_CR); 
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
  measureTime = millis();
  messageTime = millis();
}

void loop(void) { 

  isCall(); // timeout REPORT_INTERVAL
  
  digitalWrite(PWR_LED_UNO, LOW);
  // замер температуры и формирование отчёта answer
  getTemp(); // timeout 1 с
  // проверка модема
  if (!performModem("AT", GSM_OK, 1, 1)) {
    connectModem(); 
  } 
  getSignalQuality ();
  if (!digitalRead(PWR_KEY_UNO)) digitalWrite(PWR_LED_UNO, HIGH);

  if (millis() > measureTime) {
    // проверка регистрации в сети Мегафона
    creg1 = '?'; creg2 = '?'; creg3 = '?';
    if (!performModem("AT+CREG?", GSM_RG, 1, 1)) {
      connectModem(); 
    } 
    // запомним почасовые замеры за последние сутки
    for (byte s = 0; s < DEVICE_COUNT; s++) {
      for (byte t = 23; t > 0; t--) {
        hourlyTemperature[s][t] = hourlyTemperature[s][t-1];
      }
      hourlyTemperature[s][0] = currentTemperature[s];
     }
    measureTime += MEASURE_INTERVAL;
  }

  if (millis() > messageTime) {
    if (performModem("AT+CMGS=\"+7ХХХХХХХХХХ\"", GSM_SM, 1, 1)) {
      for (byte i = 0; i <= connected; i++ ) {
        Serial.write(answer[i]); 
      }
      Serial.write(CTRL_Z); 
    } else {
      Serial.write(ESCAPE); 
    }
    Serial.write(GSM_CR); 
    Serial.write(GSM_CR); 
    delay(3000);
    messageTime += MESSAGE_INTERVAL;
  }

}
