#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>

/*
#define PIN_INPUT_RED 35
#define PIN_INPUT_GREEN 34
#define PIN_INPUT_BLUE 33
*/
#define PIN_INPUT_RED 26
#define PIN_INPUT_GREEN 27
#define PIN_INPUT_BLUE 13

TaskHandle_t task1;
TaskHandle_t task2;

Adafruit_NeoPixel pixels0(64, 4, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel pixels1(64, 5, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel pixels2(64, 18, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel pixels3(64, 19, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel pixels4(4, 25, NEO_GRB + NEO_KHZ800);

Adafruit_NeoPixel *pixels[] = {
	&pixels0,
	&pixels1,
	&pixels2,
	&pixels3,
	&pixels4,
};

#define OUTPUT_PIN_COUNT (sizeof(pixels) / sizeof(*pixels))

#define POWER_HISTORY_LEN 1
class LedInputPin
{
private:
	uint8_t pin;
	bool isHigh;
	uint8_t historyPos = 0;
	uint8_t powerHistory[POWER_HISTORY_LEN] = {0};
	uint32_t lastRising = 0;
	uint32_t lastFalling = 0;

public:
	LedInputPin(uint8_t pin)
	{
		this->pin = pin;
	}

	void init(void (*onChangeFunc)())
	{
		pinMode(pin, INPUT_PULLUP);
		attachInterrupt(pin, onChangeFunc, CHANGE);
		isHigh = digitalRead(pin);
	}

	void onChange()
	{
		isHigh = !isHigh;

		uint32_t now = micros();
		if(isHigh)
		{
			uint32_t onTime = lastFalling - lastRising;
			uint32_t offTime = now - lastFalling;
			uint32_t totalTime = onTime + offTime;

			if(totalTime > 0)
			{
				powerHistory[historyPos] = (255 * offTime) / totalTime;
				historyPos++;

				if(historyPos >= POWER_HISTORY_LEN)
					historyPos = 0;
			}

			lastRising = now;
		}
		else
		{
			lastFalling = now;
		}
	}

	uint8_t getPower()
	{
		uint32_t maxAge = micros() - 10 * 1000;

		if(lastRising < maxAge)
		{
			if(digitalRead(pin) == LOW)
				return 255;
			else
				return 0;
		}

		uint32_t sum = 0;
		for(int i = 0; i < POWER_HISTORY_LEN; i++)
			sum += powerHistory[i];

		return sum / POWER_HISTORY_LEN;
	}
};

LedInputPin redPin(PIN_INPUT_RED);
LedInputPin greenPin(PIN_INPUT_GREEN);
LedInputPin bluePin(PIN_INPUT_BLUE);

void onRedChange() { redPin.onChange(); }
void onGreenChange() { greenPin.onChange(); }
void onBlueChange() { bluePin.onChange(); }

void setup()
{
	Serial.begin(115200);
	WiFi.mode(WIFI_OFF);

	for(int i = 0; i < OUTPUT_PIN_COUNT; i++)
		pixels[i]->begin();

	redPin.init(onRedChange);
	greenPin.init(onGreenChange);
	bluePin.init(onBlueChange);

	uint32_t off = Adafruit_NeoPixel::Color(0, 0, 0);
	for(int i = 0; i < OUTPUT_PIN_COUNT; i++)
	{
		pixels[i]->fill(off);
		pixels[i]->show();
	}

	for(int i = 0; i < pixels[0]->numPixels(); i++)
	{
		uint32_t color = Adafruit_NeoPixel::Color(
			(i % 7 + 1) & 1 ? 32 : 0,
			(i % 7 + 1) & 2 ? 32 : 0,
			(i % 7 + 1) & 4 ? 32 : 0
		);

		for(int j = 0; j < OUTPUT_PIN_COUNT; j++)
		{
			pixels[j]->setPixelColor(i, color);
			pixels[j]->show();
		}

		delay(10);
	}

	delay(500);
	for(int i = 0; i < OUTPUT_PIN_COUNT; i++)
	{
		pixels[i]->fill(off);
		pixels[i]->show();
	}
}

void loop()
{
	uint32_t lastUpdate = 0;

	uint32_t lastChange = 0;
	uint32_t lastChangeColor = 0;
	uint32_t activeColor = 0;

	while(true)
	{
		uint8_t red = redPin.getPower();
		uint8_t green = greenPin.getPower();
		uint8_t blue = bluePin.getPower();
		uint32_t color = Adafruit_NeoPixel::Color(red, green, blue);

		uint32_t now = millis();
		if(lastChangeColor != color)
		{
			lastChangeColor = color;
			lastChange = now;
		}

		if(now - lastChange < 20 || color == activeColor)
			continue;

		Serial.print("state : ");
		Serial.print(color, 16);
		Serial.println();

		for(int i = 0; i < OUTPUT_PIN_COUNT; i++)
		{
			pixels[i]->fill(color);
			pixels[i]->show();
		}

		activeColor = color;
	}
}
