//#include <SoftwareSerial.h>
#include <OneWire.h>
#include <LCDi2cW.h>                    

//#define RX_UNO 7
//#define TX_UNO 8
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
//#define MODEM_RATE 9600L

const char* comrade[USERS_COUNT] = {"+79219258698", "+79214201935", "+79213303129"};
const byte deviceAddress[DEVICE_COUNT][8]  = {
  { 0x28, 0x6C, 0x8F, 0x53, 0x03, 0x00, 0x00, 0xB0 }, // #1 Sensor  0m grey hub
  { 0x28, 0xF9, 0xCD, 0x53, 0x03, 0x00, 0x00, 0x80 }, // #2 Sensor 15m white hub
  { 0x28, 0x27, 0x84, 0x53, 0x03, 0x00, 0x00, 0x4D }, // #3 Sensor  5m
  { 0x28, 0xDA, 0xAF, 0x53, 0x03, 0x00, 0x00, 0xFD }  // #4 Sensor  5m
};
const int temp_null = 1598;  // (int) 99,9 * 16
int currentTemperature[DEVICE_COUNT];
int hourlyTemperature[DEVICE_COUNT][24];
unsigned long reportTime, measureTime, messageTime, timeout;
char creg1 = '?', creg2 = '?', creg3 = '?';
char buff[BUFF_SIZE];
char answer[DEVICE_COUNT+1][23];
bool isEven = true;
byte connected = 0;

//SoftwareSerial modem(RX_UNO, TX_UNO);
OneWire ds(ONE_WIRE_UNO);
LCDi2cW lcd = LCDi2cW(4,20,0x4C,0);

void getSignalQuality () {
  Serial.write("AT+CSQ"); 
  Serial.write(GSM_CR);
  bool isReq = false;
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
  //sprintf(buff, "SIGNAL Q:%c%c R:%c,%c%c", csqm, csql, creg1, creg2, creg3);
  sprintf(buff, "SIGNAL Q:%c%c", csqm, csql);
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
      // на случай если больше трёх датчиков подключено
      // на дисплей первые три датчика / последние три
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
  for (byte i = 0; i < retry; i++) {
//    lcd.setCursor(0,19);
//    lcd.print(char(49+i));
    Serial.write(command); 
    Serial.write(GSM_CR);
    resOK = false;
    timeout = millis() + 1000L; // ждём ответа 1 сек
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
  delay(15000); 
  lcd.clear(); 
  lcd.setCursor(0,0);
  sprintf(buff, "sample %ld", MODEM_RATE);
  lcd.print(buff);

  digitalWrite(PWR_LED_UNO, HIGH);

  Serial.begin(MODEM_RATE);
  delay(5000);

  lcd.setCursor(1,0);
  lcd.print(F("connection"));
  while (!performModem("AT", GSM_OK, 20, 5)) {} 
  performModem("AT&F", GSM_OK, 1, 1);           
  while (!performModem("AT", GSM_OK, 20, 5)) {} 
 
  lcd.setCursor(2,0);
  lcd.print(F("registration"));
  while (!performModem("AT+CREG?", GSM_RG, 1, 1)) {
/*
    Serial.write("AT+COPS=4,2,\"25002\""); 
    Serial.write(GSM_CR);
    delay(5000);
*/
  } 

  lcd.setCursor(3,0);
  lcd.print(F("initial setup"));
  while (!performModem("ATE0", GSM_OK, 1, 1)) {}
  while (!performModem("AT+CLIP=1", GSM_OK, 1, 1)) {}
  while (!performModem("AT+CMGF=1", GSM_OK, 1, 1)) {}
  while (!performModem("AT+CSCS=\"GSM\"", GSM_OK, 1, 1)) {}
/*
  lcd.setCursor(3,0);
  lcd.print("initial setup");
  //performModem("AT+IPR=115200", GSM_OK, 1, 1); // Set fixed local rate
  performModem("ATE0", GSM_OK, 1, 1);        // Set echo mode off 
  performModem("AT+CLIP=1", GSM_OK, 1, 1);   // Set caller ID on
  performModem("AT+CMGF=1", GSM_OK, 1, 1);   // Set SMS to text mode
  performModem("AT+CSCS=\"GSM\"", GSM_OK, 1, 1); //  Character set of the mobile equipment
  //performModem("AT&W", GSM_OK, 1, 1);        // Stores current configuration to user defined profile
*/
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
  Serial.write("AT+CLCC"); 
  Serial.write(GSM_CR);
  char eol = '\0';
  char* number = &eol;
  byte len;
  bool allow;
  timeout = millis() + 1000L; 
  do {
    if (Serial.available()) {
      len = Serial.readBytesUntil('\n', buff, BUFF_SIZE - 1);
      buff[len] = '\0';
      if (len > 19 && buff[1] == 'C' && buff[2] == 'L' && buff[3] == 'I' && buff[4] == 'P') {
        buff[19] = '\0';
        number = &buff[8];
        break;
      }
      else if (len > 29 && buff[1] == 'C' && buff[2] == 'L' && buff[3] == 'C' && buff[4] == 'C') {
        buff[29] = '\0';
        number = &buff[18];
        break;
      }
    }
  } while (millis() < timeout);

  if (number[0] != '\0') {
    delay(4000);
    //Serial.write("ATH"); 
    //Serial.write(GSM_CR); 
    performModem("ATH", GSM_OK, 1, 1);
    for (byte i = 0; i < USERS_COUNT; i++ ) {
      allow = true;
      for (byte j = 1; j < 11; j++) {
        allow = allow && number[j] == comrade[i][j+1];
      }
      if (allow) {
        delay(4000);
        sprintf(buff, "AT+CMGS=\"%s\"", comrade[i]);
        if (performModem(buff, GSM_SM, 1, 1)) {
          for (byte i = 0; i <= connected; i++ ) {
            Serial.write(answer[i]); 
          }
          Serial.write(CTRL_Z); 
          Serial.write(GSM_CR); 
          Serial.write(GSM_CR); 
          delay(5000);
        } else {
          Serial.write(ESCAPE); 
          Serial.write(GSM_CR); 
          Serial.write(GSM_CR); 
        }
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
  reportTime = millis();
  measureTime = millis();
  messageTime = millis();
}

void loop(void) { 

  isCall(); // timeout 1000L
  
  if (millis() > reportTime) {
    digitalWrite(PWR_LED_UNO, LOW);
    // замер температуры и формирование отчёта answer
    getTemp(); 
    // проверка модема
    if (!performModem("AT", GSM_OK, 1, 1)) {
      connectModem(); 
    } 
    getSignalQuality ();

    if (!digitalRead(PWR_KEY_UNO)) digitalWrite(PWR_LED_UNO, HIGH);
    reportTime = millis() + REPORT_INTERVAL;
  }

  // почасовые замеры за последние сутки
  if (millis() > measureTime) {
    // проверка регистрации 
    creg1 = '?'; creg2 = '?'; creg3 = '?';
    if (!performModem("AT+CREG?", GSM_RG, 1, 1)) {
      connectModem(); 
    } 
 
    for (byte s = 0; s < DEVICE_COUNT; s++) {
      for (byte t = 23; t > 0; t--) {
        hourlyTemperature[s][t] = hourlyTemperature[s][t-1];
      }
      hourlyTemperature[s][0] = currentTemperature[s];
     }
    measureTime = millis() + MEASURE_INTERVAL;
  }

  if (millis() > messageTime) {
    if (performModem("AT+CMGS=\"+79219258698\"", GSM_SM, 1, 1)) {
      for (byte i = 0; i <= connected; i++ ) {
        Serial.write(answer[i]); 
      }
      Serial.write(CTRL_Z); 
      Serial.write(GSM_CR); 
      Serial.write(GSM_CR); 
      delay(3000);
    }
    messageTime = millis() + MESSAGE_INTERVAL;
  }

}
