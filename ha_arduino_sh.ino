/* ===============================================================================
Мультиплексор с поддержкой 8 датчиков температуры и влажности.
12 March 2019.
----------------------------------------------------------------------------
Licensed under the terms of the GPL version 3.
http://www.gnu.org/licenses/gpl-3.0.html
Copyright (c) 2019 by Artem Khomenko _mag12@yahoo.com.
=============================================================================== */

#include <Wire.h>
#include <HTU21D.h>
#include <INA226_asukiaaa.h>

const uint16_t ina226calib = INA226_asukiaaa::calcCalibByResisterMilliOhm(2); // Max 5120 milli ohm
// const uint16_t ina226calib = INA226_asukiaaa::calcCalibByResisterMicroOhm(2000);
INA226_asukiaaa voltCurrMeter(INA226_ASUKIAAA_ADDR_A0_GND_A1_GND, ina226calib);

#define TCAADDR 0x70
const uint8_t sensCount = 8;            // Восемь датчиков влажности и температуры.

HTU21D myHTU21D(HTU21D_RES_RH12_TEMP14);

unsigned long previousMillis = 0;       // Момент последнего обновления
const long updateInterval = 1000;		    // Интервал обновлений, мс.

const int fanPin = 11;                  // Пин с вентилятором
const int RPiOffPin = 8;                // Управление выключением RPi. 
const int RPiResetPin = 9;              // Управление перезагрузкой RPi. 

float results[2][sensCount + 1];        // 1 для температуры, 2 для влажности плюс пара напряжение и мощность.
const size_t resultsLen = sizeof(float) * 2 * (sensCount + 1);

bool IsRPiOff = false;                  // Когда истина, RPi отключили вручную и надо ждать повышения напряжения для её включения.
const int16_t powerLowBound = 11200;    // При падении напряжения в милливольтах ниже этой границы RPi надо отключить.
const int16_t powerHiBound = 11600;     // При росте напряжения в милливольтах выше этой границы RPi надо включить, если она была выключена.
int cyclesForPowerChange = 0;           // Количество циклов, прошедших с момента отправки сигнала на отключение RPi.
const int cyclesFromPowerOffLimit = 300;// Ставим 5 минут, чтобы RPi успела выключиться перед повторной подачей питания.
const int cyclesFromPowerOnLimit = 30;  // Если напряжение низкое свыше 30 секунд, RPi надо выключать.

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

    voltCurrMeter.setWire(&Wire);
    voltCurrMeter.begin();

    // Посылаем команду на инициализацию устройств на всех портах.
    for (uint8_t t = 0; t < sensCount; t++) {
      tcaselect(t);
      myHTU21D.begin();

      // Заполним недействительными значениями во-избежание их появления у пользователя.
      results[0][t] = 255;
      results[1][t] = 255;
    }

    // Наряжение и ток.
    results[0][sensCount] = 255;
    results[1][sensCount] = 255;

    // Индикация начала работы
    myBlink(3);

    Serial.begin(115200);

    // Чтобы дать время на опрос датчиков напряжения.
    previousMillis = millis();
}



void loop() 
{
	// Текущее время.
	unsigned long currentMillis = millis();

	// Условие, отдельно для защиты от перехода через 0.
	unsigned long condition = currentMillis - previousMillis;

	// Интервал опроса датчиков.
	if (condition >= updateInterval) {

		// save the last time.
		previousMillis = currentMillis;

		// Открываем порт, если ещё не открыт.
		if (!Serial) {
			Serial.begin(115200);
			myBlink(2);
		}
      
		// В цикле по всем портам на мультиплексоре.
		for (uint8_t t = 0; t < sensCount; t++) {
        
			// Выбираем порт
			tcaselect(t);
    
			// Считываем температуру и влажность.
			results[0][t] = myHTU21D.readTemperature();                       // +-0.3C
			results[1][t] = myHTU21D.readCompensatedHumidity(results[0][t]);  // +-2%
		}

    // Значение напряжения mV и мощности mW
    int16_t mv, mw;
    if (voltCurrMeter.readMV(&mv) == 0)
      results[0][sensCount] = mv / 1000.0;
    else
      results[0][sensCount] = 255;
      
    if (voltCurrMeter.readMW(&mw) == 0)
      results[1][sensCount] = mw / 1000.0;
    else
      results[1][sensCount] = 255;

		// Управляем питанием RPi. 
		powerControl(mv);
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


// Управляет питанием RPi.
void powerControl(int16_t voltage){

  // У RPi когда на пине Run высокий уровень, она работает. Когда низкий, она перегружается после его отпускания.
  // Таким образом, сразу после включения Arduino она должна давать высокий сигнал на пин Run, чтобы RPi нормально работала.
  // После отключения RPi для её перезагрузки надо на короткое время подать низкий сигнал, а потом снова высокий.

  if (IsRPiOff) {
    // Если напряжение обратно выросло (выглянуло солнце), то надо отправить ресет на RPi для её загрузки, но только если прошло 
    // не менее какого-то времени во-избежание случайных колебаний.

    // В режиме выключения увеличиваем счётчик, если напряжение высокое.
    if (voltage > powerHiBound)
      cyclesForPowerChange++;
    else
      cyclesForPowerChange = 0;

    // Если напряжение выросло и счётчик достаточно отмотал, включаемся.
    if (cyclesForPowerChange > cyclesFromPowerOffLimit) {
      digitalWrite(RPiOffPin, HIGH);
      digitalWrite(RPiResetPin, HIGH);  // Нажимаем reset.
      myBlink(3);                       // Задержки в треть секунды будет достаточно.
      digitalWrite(RPiResetPin, LOW);   // Отпускаем reset и позволяем RPi загружаться.
      IsRPiOff = false;
    }
  } else {
    // Если напряжение упало низко, надо послать сигнал на выключение RPi.
    if (voltage < powerLowBound)
      cyclesForPowerChange++;
    else
      cyclesForPowerChange = 0;

      
    if (cyclesForPowerChange > cyclesFromPowerOnLimit) {
      IsRPiOff = true;
      digitalWrite(RPiOffPin, LOW);
      cyclesForPowerChange = 0;
    }
  }
}
