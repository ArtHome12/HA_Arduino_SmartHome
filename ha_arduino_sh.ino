/* ===============================================================================
Мультиплексор с поддержкой 8 датчиков температуры и влажности.
12 March 2019.
----------------------------------------------------------------------------
Licensed under the terms of the GPL version 3.
http://www.gnu.org/licenses/gpl-3.0.html
Copyright (c) 2019 by Artem Khomenko _mag12@yahoo.com.
=============================================================================== */

#include <Wire.h>
#include <EEPROM.h>
#include <HTU21D.h>
#include <INA226_asukiaaa.h>

const uint16_t ina226calib = INA226_asukiaaa::calcCalibByResisterMilliOhm(100); // Max 5120 milli ohm
#define INA226_ASUKIAAA_MAXAVERAGE_CONFIG 0x4F27                                // Default 0x4127 - for once average. Digit F for 1024 averages
INA226_asukiaaa voltCurrMeter(INA226_ASUKIAAA_ADDR_A0_GND_A1_GND, ina226calib, INA226_ASUKIAAA_MAXAVERAGE_CONFIG);

#define TCAADDR 0x70
const uint8_t HTUCount = 8;             // Восемь датчиков влажности и температуры.
uint8_t activeHTU = 0;                  // Индекс активного в текущий момент датчика.

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

float results[2][HTUCount + 1];         // 1 для температуры, 2 для влажности плюс пара напряжение и мощность.
const size_t resultsLen = sizeof(float) * 2 * (HTUCount + 1);

const int mVoltageLoBound = 11700;      // При падении напряжения в милливольтах ниже этой границы RPi надо отключить.
const int mVoltageHiBound = 12000;      // При росте напряжения в милливольтах выше этой границы RPi надо включить, если она была выключена.
const int mWattLoBound = 1500;          // Если энергопотребление упало ниже этой границы, считаем что RPi завершила работу и перешла в idle.

int cyclesPowerLow = 0;                 // Счётчик цикла для проверки падения энергопотребления.
const int cyclesPowerLowLimit = 5;      // Предел для счётчика цикла для проверки падения энергопотребления.
int cyclesVoltageLow = 0;               // Счётчик циклов для продолжительных проверок.
const int cyclesVoltageLowLimit = 30;   // Предел для счётчика циклов.
int cyclesVoltageHigh = 0;              // Счётчик циклов для продолжительных проверок.
const int cyclesVoltageHighLimit = 30;  // Предел для счётчика циклов.
int powerOffTimer = 0;                  // Счётчик циклов отключения питания RPi.
const int powerOffTimerLimit = 5*60;    // Предел для счётчика циклов отключения питания RPi.

const unsigned long maxWorkTime = 1000*60*60*24*2;  // Максимальное время непрерывной работы.
unsigned long currentMillis = 0;        // Текущее время работы с момента загрузки.

const int eepromAddrShutdown = 0;       // Адрес для хранения в EEPROM признака завершения работы.
const byte eepromSendShutdownMode = 1;  // Режим до сброса - отправлен сигнал на выключение.
const byte eepromPowerOffMode = 2;      // Режим до сброса - RPi выключена.


void setup()
{
  // По рекомендации неиспользуемые пины лучше подтянуть к земле, иначе будут потери электроэнергии при спонтанных переключениях от наводок.
  // У нас только у кнопки требуется отдельное состояние.
  for (int i = 0; i < NUM_DIGITAL_PINS; i++) {
    if (i == buttonPin)
      pinMode(i, INPUT_PULLUP);
    else
      pinMode(i, OUTPUT);
  }

  // Иногда arduino сбрасываетсяпри завершении работы RPi, иногда нет.
  // Восстановим из EEPROM информацию о состоянии до сброса.
  // RPi могла быть в режиме завершения работы и уже выключенной.
  switch (EEPROM.read(eepromAddrShutdown)) {
    case eepromSendShutdownMode:  powerOffTimer = 1; break;
    case eepromPowerOffMode:      powerOffTimer = 1; RPiTurnedOff = true; powerOff(); cyclesVoltageHigh = cyclesVoltageHighLimit - 1; break;
  }
  

  Wire.begin();

  voltCurrMeter.setWire(&Wire);
  voltCurrMeter.begin();

  // Посылаем команду на инициализацию устройств на всех портах.
  for (activeHTU = 0; activeHTU < HTUCount; activeHTU++) {
    tcaselect(activeHTU);
    myHTU21D.begin();

    // Заполним недействительными значениями во-избежание их появления у пользователя.
    results[0][activeHTU] = 255;
    results[1][activeHTU] = 255;
  }
  activeHTU = 0;

  // Напряжение и ток.
  results[0][HTUCount] = 255;
  results[1][HTUCount] = 255;

  Serial.begin(115200);

  // Индикация начала работы - мигнём 3 раза.
  blinkCountdown = 3;

  // Чтобы дать время на опрос датчиков.
  previousMillis = millis();
}



void loop() 
{
	// Текущее время.
	currentMillis = millis();

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

    // Считываем данные с мультиплексора. Выбираем порт
    tcaselect(activeHTU);
  
    // Считываем температуру (+-0.3C) и влажность (+-2%).
    float temp = myHTU21D.readTemperature();
    results[0][activeHTU] = temp;
    results[1][activeHTU] = myHTU21D.readCompensatedHumidity(temp);

		// Меняем порт на мультиплексоре.
    if (++activeHTU >= HTUCount)
      activeHTU = 0;

    // Значение напряжения mV и мощности mW
    int16_t mv, mw;
    if (voltCurrMeter.readMV(&mv) == 0)
      results[0][HTUCount] = mv / 1000.0;
    else
      results[0][HTUCount] = 255;
      
    if (voltCurrMeter.readMW(&mw) == 0)
      results[1][HTUCount] = mw / 1000.0;
    else
      results[1][HTUCount] = 255;

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
    for (uint8_t t = 0; t < HTUCount; t++) {
      tcaselect(t);
      myHTU21D.setHeater(heaterSwitch);
    }
}


// Отправляет сигнал ОС RPi о необходимости завершить работу.
void sendShutdown() {
  // Сообщаем RPi о необходимости завершить работу.
  digitalWrite(RPiSendShutdownPin, HIGH);  

  // Если это включение таймера (первое увеличение), запишем информацию в EEPROM на случай сброса ардуины при завершении работы RPi.
  if (!powerOffTimer)
    EEPROM.write(eepromAddrShutdown, eepromSendShutdownMode);

  // Если RPi уже выключена, больше ничего делать не надо. Ставим не в начало функции, так как могут быть ситуации,
  // когда RPi не выключена, а RPiTurnedOff истинна из-за сбросов ардуины.
  if (RPiTurnedOff)
    return;
  
  // Включаем либо увеличиваем таймер отключения питания.
  powerOffTimer++;

  // Включаем светодиод для индикации, что RPi завершает работу.
  digitalWrite(LED_BUILTIN, HIGH);
}

// Отключаем питание RPi.
void powerOff() {

  // Если это первый вызов выключения, запишем информацию в энергонезависимую память.
  if (!RPiTurnedOff) {
    EEPROM.write(eepromAddrShutdown, eepromPowerOffMode);
  
    // Флаг, что питание снято.
    RPiTurnedOff = true;
  }
    
  // Гасим RPi.
  digitalWrite(RPiPowerOffPin, HIGH);

  // Снимаем сигнал завершения работы для экономии электроэнергии (иногда зажигается сведодиод, если RPi обесточена, а этот сигнал есть).
  digitalWrite(RPiSendShutdownPin, LOW);

  // Погасим светодиод.
  digitalWrite(LED_BUILTIN, LOW);
}


// Управляет питанием RPi.
void powerControl(int voltage, int power){
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

  // 4. Проверяем, не отжата ли кнопка и заодно тут же на предельное время работы без перезагрузки.
  if (digitalRead(buttonPin) == HIGH || currentMillis > maxWorkTime) {
    // Посылаем сигнал завершения работы малины, если ещё не сделано.
    sendShutdown();

    // Выходим, иначе условие 5 на высокое напряжение аннулирует сигнал кнопки.
    return;
  }
  

  // 5. Проверяем не выросло ли напряжение источника питания в момент, когда ранее была команда на завершение работы.
  // Проверка на отключенность необязательна для логики, оставляем для быстродействия.
  if (voltage > mVoltageHiBound && powerOffTimer > 0) {
    // Увеличиваем счётчик и если достаточно отмотали, отправляем сигнал на включение.
    if (cyclesVoltageHigh++ > cyclesVoltageHighLimit) {
      // Отменяем таймер принудительного отключения.
      powerOffTimer = 0;

      // Флаг, что питание подано.
      RPiTurnedOff = false;

      // Заново запускаем счётчик низкого энергопотребления с форой на холостой цикл и раскачку.
      cyclesPowerLow = -2;

      // Снимаем сигнал о необходимости завершения работы.
      digitalWrite(RPiSendShutdownPin, LOW);    
      
      // Подаём питание на RPi.
      digitalWrite(RPiPowerOffPin, LOW);
      
      // Мигнём 5 раз (и шестой раз добавится в коде ниже).
      blinkCountdown += 5;

      // Снимем в энергонезависимой памяти все флаги.
      EEPROM.write(eepromAddrShutdown, 0);
    }
  } else
    // Напряжение недостаточно высокое, сбросим счётчик.
    cyclesVoltageHigh = 0;  

  // 6. Обычная работа.
  if (powerOffTimer == 0)
    // Мигнём один раз.
    blinkCountdown++;
}
