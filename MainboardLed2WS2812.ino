#include <Adafruit_NeoPixel.h>

#define PIN_INPUT_RED 35
#define PIN_INPUT_GREEN 33
#define PIN_INPUT_BLUE 32

#define PWM_INSPECTION_RANGE 1024

Adafruit_NeoPixel pixels0(64, 19, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel pixels1(64, 21, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel pixels2(64, 22, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel pixels3(64, 23, NEO_GRB + NEO_KHZ800);

Adafruit_NeoPixel *pixels[] = {
	&pixels0,
	&pixels1,
	&pixels2,
	&pixels3,
};

#define OUTPUT_PIN_COUNT sizeof(pixels) / sizeof(*pixels)

#define POWER_HISTORY_LEN 256
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
		uint32_t maxAge = micros() - 500 * 1000;

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
	for(int i = 0; i < OUTPUT_PIN_COUNT; i++)
		pixels[i]->begin();

	redPin.init(onRedChange);
	greenPin.init(onGreenChange);
	bluePin.init(onBlueChange);
}

void loop()
{
	uint32_t color = Adafruit_NeoPixel::Color(
		redPin.getPower(),
		greenPin.getPower(),
		bluePin.getPower()
	);
	Serial.println(color, 16);

	for(int i = 0; i < OUTPUT_PIN_COUNT; i++)
	{
		pixels[i]->fill(color);
		pixels[i]->show();
	}

	delay(500);
}