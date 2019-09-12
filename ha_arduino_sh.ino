/* ===============================================================================
Мультиплексор с поддержкой 8 датчиков температуры и влажности.
12 March 2019.
----------------------------------------------------------------------------
Licensed under the terms of the GPL version 3.
http://www.gnu.org/licenses/gpl-3.0.html
Copyright (c) 2019 by Artem Khomenko _mag12@yahoo.com.
=============================================================================== */

#include <Wire.h>
#include <Firmata.h>
#include <HTU21D.h>

#define TCAADDR 0x70
const uint8_t sensCount = 8;            // Восемь датчиков влажности и температуры.

HTU21D myHTU21D(HTU21D_RES_RH12_TEMP14);

unsigned long previousMillis = 0;       // will store last time sensors was updated.
const long interval = 1000;             // interval at which to update (milliseconds).

const int fanPin = 11;                  // the pin where fan is
const int voltagePin = A0;              // Датчик напряжения
const int currentPin = A2;              // Датчик тока
const int RPiOffPin = 9;                // Управление выключением RPi. 
const int RPiResetPin = 8;              // Управление перезагрузкой RPi. 

const int mVperAmp = 185;               // use 100 for 20A Module and 66 for 30A Module

float results[2][sensCount + 1];        // 1 for temperature and 2 for humidity плюс пара напряжение и ток.
const size_t resultsLen = sizeof(float) * 2 * (sensCount + 1);

bool IsRPiOff = false;                  // Когда истина, RPi отключили вручную и надо ждать повышения напряжения для её включения.
const float powerLowBound = 11.7;       // При падении напряжения ниже этой границы RPi надо отключить.
const float powerHiBound = 12.0;        // При росте напряжения выше этой границы RPi надо включить, если она была выключена.
int cyclesFromPowerOff = 0;             // Количество циклов, прошедших с момента отправки сигнала на отключение RPi.
int cyclesFromPowerOffLimit = 180;      // Ставим 3 минуты, чтобы RPi успела выключиться.

void tcaselect(uint8_t i) {
  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();  
}


void setup()
{
    pinMode(fanPin, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(RPiOffPin, OUTPUT);
    pinMode(RPiResetPin, OUTPUT);
    digitalWrite(RPiResetPin, HIGH);  // Позволяем RPi загружаться.

    Wire.begin();
    //Wire.setClock(1);
    
    // Посылаем команду на инициализацию устройств на всех портах.
    for (uint8_t t = 0; t < sensCount; t++) {
      tcaselect(t);
      myHTU21D.begin();
    }

    // Ждём установки связи по USB с хостом
    //while (!Serial);
    Serial.begin(115200);
    myBlink(5);
}



void loop() 
{
  // Текущее время.
  unsigned long currentMillis = millis();

  // Условие, отдельно для защиты от перехода через 0.
  unsigned long condition = currentMillis - previousMillis;

  if (condition >= interval) {
    // save the last time.
    previousMillis = currentMillis;

    // Открываем порт, если ещё не открыт.
    if (!Serial) {
      Serial.begin(115200);
      myBlink(2);
    } else
      myBlink(1);
    
    // В цикле по всем портам на мультиплексоре.
    for (uint8_t t = 0; t < sensCount; t++) {
      
      // Выбираем порт
      tcaselect(t);
  
      // Считываем температуру и влажность.
      results[0][t] = myHTU21D.readTemperature();                       // +-0.3C
      // results[0][t] = t;                       // +-0.3C
      results[1][t] = myHTU21D.readCompensatedHumidity(results[0][t]);  // +-2%
    }

    // Считываем напряжение (max 25V) http://henrysbench.capnfatz.com/henrys-bench/arduino-voltage-measurements/arduino-25v-voltage-sensor-module-user-manual/
    float voltage = analogRead(voltagePin) * 25.0 / 1024.0;
    results[0][sensCount] = voltage;

    // Считываем ток по http://henrysbench.capnfatz.com/henrys-bench/arduino-current-measurements/the-acs712-current-sensor-with-an-arduino/
    results[1][sensCount] = ((analogRead(currentPin) * 5000.0 / 1024.0) - 2500) / mVperAmp;
    //results[1][sensCount] = analogRead(currentPin);

    // Управляем питанием RPi. 
    powerControl(voltage);
  }
}


void serialEvent() {
  while (Serial.available()) {
    // get the new byte:
    char inChar = (char)Serial.read();

    // Если команда верная, отправляем значения.
    switch(inChar) {
      case 'D': Serial.write((uint8_t*)results, resultsLen); break; // Data
      case 'C': setHeater(HTU21D_ON); break;                        // Check heater
      case 'E': setHeater(HTU21D_OFF); break;                       // End check heater
      case 'S':                                                     // Start fan
        digitalWrite(fanPin, HIGH); 
        digitalWrite(LED_BUILTIN, HIGH); 
      break;                       
      case 'F':                                                     // Finish fan
        digitalWrite(fanPin, LOW); 
        digitalWrite(LED_BUILTIN, LOW); 
      break;                       
    }
  }
}

void setHeater(HTU21D_HEATER_SWITCH heaterSwitch) {
    for (uint8_t t = 0; t < sensCount; t++) {
      tcaselect(t);
      myHTU21D.setHeater(heaterSwitch);
    }
}

void myBlink(uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW); 
    delay(100);
  }
}


// Упрваляет питанием RPi.
void powerControl(float voltage){

  // У RPi когда на пине Run высокий уровень, она работает. Когда низкий, она перегружается после его отпускания.
  // Таким образом, сразу после включения Arduino она должна давать высокий сигнал на пин Run, чтобы RPi нормально работала.
  // После отключения RPi для её перезагрузки надо на короткое время подать низкий сигнал, а потом снова высокий.

  if (not IsRPiOff) {
    // Если напряжение упало низко, надо послать сигнал на выключение RPi.
    if (voltage < powerLowBound) {
      IsRPiOff = true;
      digitalWrite(RPiOffPin, HIGH);
      cyclesFromPowerOff = 0;
    }
  } else {
    // Если напряжение обратно выросло (выглянуло солнце), то надо отправить ресет на RPi для её загрузки, но только если прошло 
    // не менее 2-х минут во-избежание случайных колебаний.

    // В режиме выключения увеличиваем счётчик, если напряжение высокое.
    if (voltage > powerHiBound)
      cyclesFromPowerOff++;
    else
      cyclesFromPowerOff = 0;

    // Если напряжение выросло и счётчик достаточно отмотал, включаемся.
    if (cyclesFromPowerOff > cyclesFromPowerOffLimit) {
      digitalWrite(RPiOffPin, LOW);
      digitalWrite(RPiResetPin, HIGH);  // Нажимаем reset.
      myBlink(3);                       // Задержки в треть секунды будет достаточно.
      digitalWrite(RPiResetPin, LOW);   // Отпускаем reset и позволяем RPi загружаться.
      IsRPiOff = false;
    }
  }
}

