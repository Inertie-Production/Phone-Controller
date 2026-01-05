#include <Arduino.h>
#include "lib/ESP32-BLE-Gamepad-0.7.3/BleGamepad.h"
#include "lib/ESP32-BLE-Gamepad-0.7.3/Preferences.h"

/* ============================================================
 * ========================= DEFINES ==========================
 * ============================================================
 */

#define BATTERY_PIN				10
#define DIVIDER_RATIO			2.0f

#define BATTERY_UPDATE_INTERVAL	10000UL

#define STICK_AXIS_MIN			-32767
#define STICK_AXIS_MAX			32767

#define ADC_MAX_VALUE			4095
#define ADC_REF_VOLTAGE			3.3f

/* ============================================================
 * ========================= GLOBALS ==========================
 * ============================================================
 */

Preferences prefs;
BleGamepad bleGamepad("FMController", "FMC", 100);

/* -------------------- Button Matrix -------------------- */

#define MATRIX_ROWS	4
#define MATRIX_COLS	4

int RowPins[MATRIX_ROWS] = { 39, 40, 11, 41 };
int ColPins[MATRIX_COLS] = { 13, 12, 4, 44 };

/*
	row0: BumperL, StickL, BumperR, StickR
	row1: Select, Home, Start
	row2: A, B, X, Y
	row3: Down, Right, Up, Left
*/
int ButtonMatrix[MATRIX_ROWS][MATRIX_COLS] =
{
	{ BUTTON_5,  BUTTON_7,  BUTTON_6,  BUTTON_8  },
	{ BUTTON_11, BUTTON_9,  BUTTON_10, 0         },
	{ BUTTON_1,  BUTTON_2,  BUTTON_3,  BUTTON_4  },
	{ BUTTON_12, BUTTON_13, BUTTON_14, BUTTON_15 }
};

/* -------------------- Joysticks -------------------- */

#define STICK_LY	A0
#define STICK_LX	A1
#define STICK_RY	A9
#define STICK_RX	A8

int StickLMaxX = 880;
int StickLMinX = 143;
int StickLMaxY = 880;
int StickLMinY = 143;

int StickRMaxX = 880;
int StickRMinX = 143;
int StickRMaxY = 880;
int StickRMinY = 143;

int StickNeutralDeadzone = 4000;

/* -------------------- Triggers -------------------- */

#define TRIGGER_L	A5
#define TRIGGER_R	A10

bool UseAnalogTriggers = true;

int TriggerMinL = 540;
int TriggerMaxL = 790;
int TriggerMinR = 540;
int TriggerMaxR = 790;

int MappedTriggerL = 0;
int MappedTriggerR = 0;

/* -------------------- Battery -------------------- */

unsigned long LastBatteryUpdate = 0;

/* ============================================================
 * ========================= HELPERS ==========================
 * ============================================================
 */

// Clamp value inside bounds
int ClampValue(int value, int minValue, int maxValue)
{
	if (value < minValue) return minValue;
	if (value > maxValue) return maxValue;
	return value;
}

// TODO: Allow configurable voltage curve (Li-Ion is not linear)
int ReadBatteryPercentage()
{
	int raw = analogRead(BATTERY_PIN);

	float voltage = (raw / (float)ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
	voltage *= DIVIDER_RATIO;

#define BATTERY_MIN_VOLTAGE	3.2f
#define BATTERY_MAX_VOLTAGE	4.2f

	voltage = constrain(voltage, BATTERY_MIN_VOLTAGE, BATTERY_MAX_VOLTAGE);

	float percent = ((voltage - BATTERY_MIN_VOLTAGE) /
					(BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0f;

	return (int)roundf(percent);
}

// Apply deadzone and re-scale axis
int16_t ApplyDeadzone(int16_t value, int16_t deadzone)
{
	if (abs(value) < deadzone)
	{
		return 0;
	}

	if (value > 0)
	{
		return map(value, deadzone, STICK_AXIS_MAX, 0, STICK_AXIS_MAX);
	}

	return map(value, -deadzone, STICK_AXIS_MIN, 0, STICK_AXIS_MIN);
}

// Scan one button from matrix
bool IsButtonPressed(int row, int col)
{
	digitalWrite(RowPins[row], LOW);
	delayMicroseconds(5);

	bool pressed = (digitalRead(ColPins[col]) == LOW);

	digitalWrite(RowPins[row], HIGH);
	return pressed;
}

/* ============================================================
 * ======================= CALIBRATION ========================
 * ============================================================
 */

// TODO: Add timeout / cancel option via button
void CalibrateTriggers()
{
	Serial.println("Use analog triggers? [Y/N]");

	while (!Serial.available()) { }

	char input = Serial.read();

	if (input == 'n' || input == 'N')
	{
		UseAnalogTriggers = false;

		prefs.begin("calibration", false);
		prefs.putBool("UseAnalogTriggers", UseAnalogTriggers);
		prefs.end();

		return;
	}

	UseAnalogTriggers = true;

	TriggerMaxL = 0;
	TriggerMaxR = 0;

	Serial.println("Fully press both triggers for 3 seconds");

	unsigned long start = millis();

	while (millis() - start < 3000)
	{
		TriggerMaxL = max(TriggerMaxL, analogRead(TRIGGER_L));
		TriggerMaxR = max(TriggerMaxR, analogRead(TRIGGER_R));
		delay(100);
	}

	TriggerMinL = ADC_MAX_VALUE;
	TriggerMinR = ADC_MAX_VALUE;

	Serial.println("Release both triggers for 3 seconds");

	start = millis();

	while (millis() - start < 3000)
	{
		TriggerMinL = min(TriggerMinL, analogRead(TRIGGER_L));
		TriggerMinR = min(TriggerMinR, analogRead(TRIGGER_R));
		delay(100);
	}

	prefs.begin("calibration", false);
	prefs.putBool("UseAnalogTriggers", UseAnalogTriggers);
	prefs.putInt("TriggerMinL", TriggerMinL);
	prefs.putInt("TriggerMaxL", TriggerMaxL);
	prefs.putInt("TriggerMinR", TriggerMinR);
	prefs.putInt("TriggerMaxR", TriggerMaxR);
	prefs.end();
}

// TODO: Store neutral center calibration per stick
void CalibrateSticks()
{
	StickLMaxX = StickLMaxY = 0;
	StickRMaxX = StickRMaxY = 0;

	StickLMinX = StickLMinY = ADC_MAX_VALUE;
	StickRMinX = StickRMinY = ADC_MAX_VALUE;

	Serial.println("Rotate sticks fully for 5 seconds");

	unsigned long start = millis();

	while (millis() - start < 5000)
	{
		StickLMaxX = max(StickLMaxX, analogRead(STICK_LX));
		StickLMinX = min(StickLMinX, analogRead(STICK_LX));
		StickLMaxY = max(StickLMaxY, analogRead(STICK_LY));
		StickLMinY = min(StickLMinY, analogRead(STICK_LY));

		StickRMaxX = max(StickRMaxX, analogRead(STICK_RX));
		StickRMinX = min(StickRMinX, analogRead(STICK_RX));
		StickRMaxY = max(StickRMaxY, analogRead(STICK_RY));
		StickRMinY = min(StickRMinY, analogRead(STICK_RY));

		delay(50);
	}

	prefs.begin("calibration", false);
	prefs.putInt("StickLMaxX", StickLMaxX);
	prefs.putInt("StickLMinX", StickLMinX);
	prefs.putInt("StickLMaxY", StickLMaxY);
	prefs.putInt("StickLMinY", StickLMinY);
	prefs.putInt("StickRMaxX", StickRMaxX);
	prefs.putInt("StickRMinX", StickRMinX);
	prefs.putInt("StickRMaxY", StickRMaxY);
	prefs.putInt("StickRMinY", StickRMinY);
	prefs.end();
}

/* ============================================================
 * =========================== SETUP ==========================
 * ============================================================
 */

void setup()
{
	Serial.begin(115200);

	for (int i = 0; i < MATRIX_ROWS; i++)
	{
		pinMode(RowPins[i], OUTPUT);
		digitalWrite(RowPins[i], HIGH);
	}

	for (int i = 0; i < MATRIX_COLS; i++)
	{
		pinMode(ColPins[i], INPUT);
	}

	delay(100);

	if (IsButtonPressed(2, 0) && IsButtonPressed(3, 2))
	{
		Serial.println("== CALIBRATION MODE ==");
		CalibrateTriggers();
		CalibrateSticks();
		ESP.restart();
	}

	prefs.begin("calibration", true);

	UseAnalogTriggers = prefs.getBool("UseAnalogTriggers", true);

	TriggerMinL = prefs.getInt("TriggerMinL", TriggerMinL);
	TriggerMaxL = prefs.getInt("TriggerMaxL", TriggerMaxL);
	TriggerMinR = prefs.getInt("TriggerMinR", TriggerMinR);
	TriggerMaxR = prefs.getInt("TriggerMaxR", TriggerMaxR);

	StickNeutralDeadzone = prefs.getInt("StickNeutralDeadzone", StickNeutralDeadzone);

	prefs.end();

	bleGamepad.begin();
}

/* ============================================================
 * ============================ LOOP ==========================
 * ============================================================
 */

void loop()
{
	if (!bleGamepad.isConnected())
	{
		delay(10);
		return;
	}

	int lx = analogRead(STICK_LX);
	int ly = analogRead(STICK_LY);
	int rx = analogRead(STICK_RX);
	int ry = analogRead(STICK_RY);

	int16_t mappedLX = ApplyDeadzone(map(lx, StickLMinX, StickLMaxX, STICK_AXIS_MIN, STICK_AXIS_MAX), StickNeutralDeadzone);
	int16_t mappedLY = ApplyDeadzone(map(ly, StickLMinY, StickLMaxY, STICK_AXIS_MIN, STICK_AXIS_MAX), StickNeutralDeadzone);
	int16_t mappedRX = ApplyDeadzone(map(rx, StickRMinX, StickRMaxX, STICK_AXIS_MIN, STICK_AXIS_MAX), StickNeutralDeadzone);
	int16_t mappedRY = ApplyDeadzone(map(ry, StickRMinY, StickRMaxY, STICK_AXIS_MIN, STICK_AXIS_MAX), StickNeutralDeadzone);

	bleGamepad.setLeftThumb(mappedLX, mappedLY);
	bleGamepad.setRightThumb(mappedRX, mappedRY);

	if (UseAnalogTriggers)
	{
		MappedTriggerL = map(ClampValue(analogRead(TRIGGER_L), TriggerMinL, TriggerMaxL), TriggerMinL, TriggerMaxL, 0, STICK_AXIS_MAX);
		MappedTriggerR = map(ClampValue(analogRead(TRIGGER_R), TriggerMinR, TriggerMaxR), TriggerMinR, TriggerMaxR, 0, STICK_AXIS_MAX);
	}
	else
	{
		MappedTriggerL = digitalRead(TRIGGER_L) == LOW ? STICK_AXIS_MAX : 0;
		MappedTriggerR = digitalRead(TRIGGER_R) == LOW ? STICK_AXIS_MAX : 0;
	}

	bleGamepad.setLeftTrigger(MappedTriggerL);
	bleGamepad.setRightTrigger(MappedTriggerR);

	for (int row = 0; row < MATRIX_ROWS; row++)
	{
		digitalWrite(RowPins[row], LOW);

		for (int col = 0; col < MATRIX_COLS; col++)
		{
			int button = ButtonMatrix[row][col];

			if (!button) continue;

			if (digitalRead(ColPins[col]) == LOW)
			{
				bleGamepad.press(button);
			}
			else
			{
				bleGamepad.release(button);
			}
		}

		digitalWrite(RowPins[row], HIGH);
	}

	unsigned long now = millis();

	if (now - LastBatteryUpdate >= BATTERY_UPDATE_INTERVAL)
	{
		bleGamepad.setBatteryLevel(ReadBatteryPercentage());
		LastBatteryUpdate = now;
	}

	bleGamepad.sendReport();
	delay(10);
}
