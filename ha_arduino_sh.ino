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
const int mVperAmp = 185;               // use 100 for 20A Module and 66 for 30A Module

float results[2][sensCount + 1];            // 1 for temperature and 2 for humidity плюс пара напряжение и ток.
const size_t resultsLen = sizeof(float) * 2 * (sensCount + 1);


void tcaselect(uint8_t i) {
  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();  
}


void setup()
{
    pinMode(fanPin, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);

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
    myBlink(12);
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
      myBlink(3);
    }
      myBlink(1);
    
    // В цикле по всем портам на мультиплексоре.
    for (uint8_t t = 0; t < sensCount; t++) {
      
      // Выбираем порт
      tcaselect(t);
  
      // Считываем температуру и влажность.
      results[0][t] = myHTU21D.readTemperature();                       // +-0.3C
      results[1][t] = myHTU21D.readCompensatedHumidity(results[0][t]);  // +-2%
    }

    // Считываем напряжение (max 25V)
    results[0][sensCount] = analogRead(voltagePin) * 25.0 / 1024.0;

    // Считываем ток по http://henrysbench.capnfatz.com/henrys-bench/arduino-current-measurements/the-acs712-current-sensor-with-an-arduino/
    results[1][sensCount] = ((analogRead(currentPin) / 1024.0 * 5000) - 2500) / mVperAmp;
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

