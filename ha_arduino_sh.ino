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
const long updateInterval = 1000;		    // Интервал обновлений, мс.

const int fanPin = 3;                   // Пин с вентилятором.
const int buttonPin = 6;                // Кнопка включения/выключения.
const int RPiSendShutdownPin = 8;       // Управление выключением RPi. 
const int RPiPowerOffPin = 9;           // Отключение питания RPi. 

float results[2][sensCount + 1];        // 1 для температуры, 2 для влажности плюс пара напряжение и мощность.
const size_t resultsLen = sizeof(float) * 2 * (sensCount + 1);

bool InShuttingDown = false;            // Когда истина, RPi отправили сигнал на необходимость завершения работы.
bool RPIOffPower = false;               // Когда истина, с RPi снято питание (на пин PEN отправлен высокий сигнал).
const int16_t mVoltageLowBound = 11900; // При падении напряжения в милливольтах ниже этой границы RPi надо отключить.
const int16_t mVoltageHiBound = 12000;  // При росте напряжения в милливольтах выше этой границы RPi надо включить, если она была выключена.
const int16_t mWattLowBound = 1500;     // Если энергопотребление упало ниже этой границы, считаем что RPi завершила работу и перешла в idle.
int cyclesForPowerChange = 0;           // Количество циклов, прошедших с момента отправки сигнала на отключение RPi.
const int cyclesFromPowerOffLimit = 300;// Ставим 5 минут проверки наличия достаточного напряжения перед включением RPi.
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
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(RPiSendShutdownPin, OUTPUT);
  pinMode(RPiPowerOffPin, OUTPUT);

  // Если при подаче питания кнопка выключена, снимаем напряжение с RPi.
  if (digitalRead(buttonPin) == HIGH) {
    digitalWrite(RPiPowerOffPin, HIGH);
    InShuttingDown = true;
    RPIOffPower = true;
  }  

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

  // Индикация начала работы
  myBlink(3);

  // Чтобы дать время на опрос датчиков.
  previousMillis = millis();
}



void loop() 
{
	// Текущее время.
	unsigned long currentMillis = millis();

	// Условие вычисляем отдельно, для защиты от перехода через 0.
	unsigned long condition = currentMillis - previousMillis;

	// Интервал опроса датчиков.
	if (condition >= updateInterval) {

		// save the last time.
		previousMillis = currentMillis;

		// В цикле по всем портам на мультиплексоре.
		for (uint8_t t = 0; t < sensCount; t++) {
        
			// Выбираем порт
			tcaselect(t);
    
			// Считываем температуру и влажность.
			results[0][t] = myHTU21D.readTemperature();                       // +-0.3C
			results[1][t] = myHTU21D.readCompensatedHumidity(results[0][t]);  // +-2%
		}

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
void powerControl(int voltage, int power){

  // У RPi когда на пине Run высокий уровень, она работает. Когда низкий, она перегружается после его отпускания.
  // Таким образом, сразу после включения Arduino она должна давать высокий сигнал на пин Run, чтобы RPi нормально работала.
  // После отключения RPi для её перезагрузки надо на короткое время подать низкий сигнал, а потом снова высокий.

  // Если RPi в режиме выключения, но ещё не выключена, а энергопотребление упало, снимаем с неё питание.
  if (InShuttingDown && !RPIOffPower && power < mWattLowBound) {

    // Для защиты от случайных просадок сделаем несколько измерений.
    for (int i = 0; i < 12; i++) {
      delay(300);
      
      // При ошибке или при восстановлении энергопотребления выходим.
      if (voltCurrMeter.readMW(&power) != 0 || power >= mWattLowBound)
      return;
    }

    // Гасим RPi.
    digitalWrite(RPiPowerOffPin, HIGH);
    RPIOffPower = true;

    // Погасим светодиод.
    digitalWrite(LED_BUILTIN, LOW);

    // Выходим, чтобы прошёл минимум цикл перед любыми другими действиями.
    return;
  }

  // Проверка отжатия кнопки человеком.
  // Если кнопка питания отжата, то посылаем команду на выключение, если она ещё не выключена.
  if (digitalRead(buttonPin) == HIGH) {
    if (!InShuttingDown) {
      InShuttingDown = true;
      digitalWrite(RPiSendShutdownPin, HIGH);  // Сообщаем RPi о необходимости завершить работу.

      // Включаем светодиод для индикации, что RPi завершает работу.
      digitalWrite(LED_BUILTIN, HIGH);

      // Так как выключили вручную, то при последущем нажатии кнопки нелогично ждать как в случае с автовыключением. Уберём задержку.
      cyclesForPowerChange = cyclesFromPowerOffLimit;
    }
    // И выходим.
    return;
  }

  // Если кнопка питания нажата, действует логика по питанию:
  // - если RPi включена:
  //   - если напряжение высокое, то ничего не делаем;
  //   - при просадке напряжения в течение нескольких циклов посылаем команду на отключение RPi.
  // - если RPi выключена:
  //   - если напряжение низкое, то ничего не делаем;
  //   - если напряжение высокое в течение нескольких циклов, посылаем команду на включение.

  // Если RPi в режиме выключения.
  if (InShuttingDown) {

    // Если напряжение достаточно высокое (вышло солнце или нажали кнопку питания и перестало действовать условие в начале процедуры).
    if (voltage > mVoltageHiBound) {
      
      // Увеличиваем счётчик для подавления случайного дребезга
      cyclesForPowerChange++;

      // Если напряжение сохраняется высоким и счётчик достаточно отмотал, включаемся.
      if (cyclesForPowerChange > cyclesFromPowerOffLimit) {
        // Снимаем сигнал о необходимости завершения работы.
        digitalWrite(RPiSendShutdownPin, LOW);    
        InShuttingDown = false;                   

        // Если питание уже было снято, восстанавливаем его.
        if (RPIOffPower) {
          // Подаём питание на RPi.
          digitalWrite(RPiPowerOffPin, LOW);
          RPIOffPower = false;
        }
      }
    } else
      // Напряжение остаётся низким, обнуляем счётчик.
      cyclesForPowerChange = 0;

  } else {
    // Если RPi в обычном режиме.
  
    // Если напряжение упало низко, начинаем отматывать счётчик во-избежание случайных флуктуаций.
    if (voltage < mVoltageLowBound) {
      cyclesForPowerChange++;
      
      // Если напряжение остаётся низким достаточно долго, отправляем команду на завершение работы.
      if (cyclesForPowerChange > cyclesFromPowerOnLimit) {

        // Сообщаем RPi о необходимости завершить работу.
        InShuttingDown = true;
        digitalWrite(RPiSendShutdownPin, HIGH);  

        // Включаем светодиод для индикации, что RPi завершает работу.
        digitalWrite(LED_BUILTIN, HIGH);

        // Обнуляем счётчик для предотвращения включения без задержки, если вдруг на следующем цикле напряжение вырастет.
        cyclesForPowerChange = 0;
      }
    } else
      // Всё ok, напряжение в норме, обычный режим.
      cyclesForPowerChange = 0;

      // Мигнём один раз.
      myBlink(1);
  }
}
