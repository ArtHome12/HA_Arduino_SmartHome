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

const uint16_t ina226calib = INA226_asukiaaa::calcCalibByResisterMilliOhm(100); // Max 5120 milli ohm
#define INA226_ASUKIAAA_MAXAVERAGE_CONFIG 0x4F27                                // Default 0x4127 - for once average. Digit F for 1024 averages
INA226_asukiaaa voltCurrMeter(INA226_ASUKIAAA_ADDR_A0_GND_A1_GND, ina226calib, INA226_ASUKIAAA_MAXAVERAGE_CONFIG);

#define TCAADDR 0x70
const uint8_t sensCount = 8;             // Восемь датчиков влажности и температуры.

HTU21D myHTU21D(HTU21D_RES_RH12_TEMP14);

unsigned long previousMillis = 0;       // Момент последнего обновления
const long minDelay = 100;              // Минимально необходимый интервал для работы внутри loop(), мс.
int delaysCount = 0;                    // Количество прошедших минимальных интервалов.
const int delaysCountLimit = 10;        // Максимальное количество интервалов, при котором срабатывает логика - 100*10=1000мс или один раз в секунду.
int blinkCountdown = 0;                 // Количество оставшихся миганий светодиода.
bool lightIsOn = false;                 // Истина, если в текущем цикле светодиод зажжён.
bool RPiTurnedOff = false;              // Истина, когда с RPi снято питание.


const int fanPin = 3;                   // Пин с вентилятором.
const int buttonPin = 6;                // Кнопка включения/выключения. При отжатии кнопки RPi выключается, а при нажатии включается, но только если питание высокое
const int RPiSendShutdownPin = 8;       // Управление выключением RPi. 
const int RPiPowerOffPin = 9;           // Когда на пине низкий уровень, RPi работает. Когда высокий, она обесточена.

float results[2][sensCount + 1];        // 1 для температуры, 2 для влажности плюс пара напряжение и мощность.
const size_t resultsLen = sizeof(float) * 2 * (sensCount + 1);

const int mVoltageLoBound = 11900;      // При падении напряжения в милливольтах ниже этой границы RPi надо отключить.
const int mVoltageHiBound = 12000;      // При росте напряжения в милливольтах выше этой границы RPi надо включить, если она была выключена.
const int mWattLoBound = 1500;          // Если энергопотребление упало ниже этой границы, считаем что RPi завершила работу и перешла в idle.

int cyclesPowerLow = 0;                 // Счётчик циклов для продолжительных проверок.
const int cyclesPowerLowLimit = 3;      // Предел для счётчика циклов.
int cyclesVoltageLow = 0;               // Счётчик циклов для продолжительных проверок.
const int cyclesVoltageLowLimit = 30;   // Предел для счётчика циклов.
int powerOffTimer = 0;                  // Счётчик циклов отключения питания RPi.
const int powerOffTimerLimit = 5*60;    // Предел для счётчика циклов отключения питания RPi.



void setup()
{
  pinMode(fanPin, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(RPiSendShutdownPin, OUTPUT);
  pinMode(RPiPowerOffPin, OUTPUT);

  // Если при подаче питания кнопка выключена, сразу завершаем работу.
//  if (digitalRead(buttonPin) == HIGH) {
//    sendShutdown();
//    powerOff();
//  }  

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

  Serial.begin(115200);

  // Индикация начала работы - мигнём 3 раза.
  blinkCountdown = 3;

  // Чтобы дать время на опрос датчиков.
  previousMillis = millis();
}



void loop() 
{
	// Текущее время.
	unsigned long currentMillis = millis();

	// Условия вычисляем отдельно, для защиты от перехода через 0.
	unsigned long condition = currentMillis - previousMillis;

  // Если слишком мало времени прошло с предыдущего раза.
  if (condition < minDelay)
    return;

  // Сохраним время срабатывания.
  previousMillis = currentMillis;


  // Обработка логики мигания светодиодом. Если он горел, его надо погасить в этот раз.
  //
  if (lightIsOn) {
    lightIsOn = false;
    digitalWrite(LED_BUILTIN, LOW);
  } else {
    // Если ещё осталось количество миганий
    if (blinkCountdown > 0) {
      // Включаем светодиод и уменьшаем счётчик миганий.
      lightIsOn = true;
      digitalWrite(LED_BUILTIN, HIGH);
      blinkCountdown--;
    }
  }


	// Основная логика.
  //
	if (++delaysCount >= delaysCountLimit) {
    // Начинаем заново отсчитывать количество маленьких циклов до захода сюда.
    delaysCount = 0;

//		// В цикле по всем портам на мультиплексоре.
//		for (uint8_t t = 0; t < sensCount; t++) {
//        
//			// Выбираем порт
//			tcaselect(t);
//    
//			// Считываем температуру и влажность.
//			results[0][t] = myHTU21D.readTemperature();                       // +-0.3C
//			results[1][t] = myHTU21D.readCompensatedHumidity(results[0][t]);  // +-2%
//		}

    // Значение напряжения mV и мощности mW
    int mv, mw;
    if (voltCurrMeter.readMV(&mv) == 0)
      results[0][sensCount] = mv / 1000.0;
    else
      results[0][sensCount] = 255;
      
    if (voltCurrMeter.readMW(&mw) == 0)
      results[1][sensCount] = mw / 1000.0;
    else
      results[1][sensCount] = 255;

		// Управляем питанием RPi. 
		powerControl(mv, mw);
	}
}


// Входящая информация от RPi.
void serialEvent() {
  while (Serial.available()) {
    // get the new byte:
    char inChar = (char)Serial.read();

    // Если команда верная, отправляем значения.
    switch(inChar) {
      case 'D': Serial.write((uint8_t*)results, resultsLen); break; // Data
      case 'C': setHeater(HTU21D_ON); break;                        // Check heater
      case 'E': setHeater(HTU21D_OFF); break;                       // End check heater
      case 'S': digitalWrite(fanPin, HIGH); break;                  // Start fan
      case 'F': digitalWrite(fanPin, LOW); break;                   // Stop fan
    }
  }
}


// Управление мультиплексором TCA9548A - выбор активного устройства.
void tcaselect(uint8_t i) {
  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();  
}

// Включение подогрева на всех присоединённых датчиках HTU21D.
void setHeater(HTU21D_HEATER_SWITCH heaterSwitch) {
    for (uint8_t t = 0; t < sensCount; t++) {
      tcaselect(t);
      myHTU21D.setHeater(heaterSwitch);
    }
}


// Отправляет сигнал ОС RPi о необходимости завершить работу.
void sendShutdown() {
  // Сообщаем RPi о необходимости завершить работу.
  digitalWrite(RPiSendShutdownPin, HIGH);  

  // Включаем либо увеличиваем таймер отключения питания.
  powerOffTimer++;

  // Включаем светодиод для индикации, что RPi завершает работу.
  digitalWrite(LED_BUILTIN, HIGH);
}

// Отключаем питание RPi.
void powerOff() {
  // Флаг, что питание снято.
  RPiTurnedOff = true;
    
  // Гасим RPi.
  digitalWrite(RPiPowerOffPin, HIGH);

  // Погасим светодиод.
  digitalWrite(LED_BUILTIN, LOW);
}


// Управляет питанием RPi.
void powerControl(int voltage, int power){
  // Проверки с 1 по 3 выполняем только если питание не снято.
  if (!RPiTurnedOff) {
  
    // 1. Проверяем, не упало ли энергопотребление RPi.
    if (power < mWattLoBound) {
      // Увеличиваем счётчик и если достаточно отмотали, выключаем RPi.
      if (cyclesPowerLow++ > cyclesPowerLowLimit)
        powerOff();
    } else
      // Энергопотребление выше минимального, сбрасываем счётчик.
      cyclesPowerLow = 0;
  
    // 2. Проверяем, не сработал ли таймер отключения.
    if (powerOffTimer > powerOffTimerLimit)
        powerOff();
  
    // 3. Проверяем не упало ли напряжение источника питания.
    if (voltage < mVoltageLoBound) {
      // Увеличиваем счётчик и если достаточно отмотали, отправляем сигнал на завершение работы.
      if (cyclesVoltageLow++ > cyclesVoltageLowLimit)
        sendShutdown();
    } else
      // Энергопотребление выше минимального, сбрасываем счётчик.
      cyclesVoltageLow = 0;
  }

  // 4. Проверяем, не отжата ли кнопка. 
  if (digitalRead(buttonPin) == HIGH) {
    // Посылаем сигнал завершения работы малины, если ещё не сделано.
    if (!RPiTurnedOff)
      sendShutdown();

    // Выходим, иначе условие 5 на высокое напряжение аннулирует сигнал кнопки.
    return;
  }
  

  // 5. Проверяем не выросло ли напряжение источника питания в момент, когда ранее была команда на завершение работы.
  // Данное условие может отменить сигнал кнопки. Проверка на таймер необязательна для логики, оставляем для быстродействия.
  if (voltage > mVoltageHiBound && powerOffTimer > 0) {
    // Отменяем таймер принудительного отключения.
    powerOffTimer = 0;

    // Флаг, что питание подано.
    RPiTurnedOff = false;

    // Снимаем сигнал о необходимости завершения работы.
    digitalWrite(RPiSendShutdownPin, LOW);    
    
    // Подаём питание на RPi.
    digitalWrite(RPiPowerOffPin, LOW);
  }

  // 6. Обычная работа.
  if (powerOffTimer == 0)
    // Мигнём один раз.
    blinkCountdown = 1;
}
