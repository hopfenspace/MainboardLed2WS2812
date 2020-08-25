#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <MCP23017.h>

#define PIN_INPUT_RED 35
#define PIN_INPUT_GREEN 34
#define PIN_INPUT_BLUE 33

#define PWM_INSPECTION_RANGE 1024
#define OUTPUT_PWM_FPS 120

TaskHandle_t task1;
TaskHandle_t task2;

TwoWire I2CInstance = TwoWire(0);
MCP23017 mcp = MCP23017(0x20, I2CInstance);
uint8_t mcpPinTriples[] = {
	0, 3, 8, 11,
};
#define MCP_PIN_TRIPES_COUNT (sizeof(mcpPinTriples) / sizeof(*mcpPinTriples))

Adafruit_NeoPixel pixels0(64, 4, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel pixels1(64, 5, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel pixels2(64, 18, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel pixels3(64, 19, NEO_GRB + NEO_KHZ800);

Adafruit_NeoPixel *pixels[] = {
	&pixels0,
	&pixels1,
	&pixels2,
	&pixels3,
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

inline void calculateABStates(uint8_t state, uint8_t &aState, uint8_t &bState)
{
	for(int i = 0; i < MCP_PIN_TRIPES_COUNT; i++)
	{
		uint8_t index = mcpPinTriples[i];
		uint8_t mask;
		if(index > 7)
			mask = state << (index - 8);
		else
			mask = state << index;

		if(index > 7)
			bState |= mask;
		else
			aState |= mask;
	}
}

void setMcpPinTiples(uint8_t state, uint8_t times = 3)
{
	uint8_t aState = 0;
	uint8_t bState = 0;

	calculateABStates(state, aState, bState);

	for(int i = 0; i < times; i++)
	{
		mcp.writePort(MCP23017Port::A, aState);
		mcp.writePort(MCP23017Port::B, bState);
	}
}

void pwmLedWorker(void *param)
{
	uint32_t lastUpdate = 0;

	uint8_t colorTriple = 0b111;
	uint32_t turnOffR = 0;
	uint32_t turnOffG = 0;
	uint32_t turnOffB = 0;

	uint32_t lastSet = 0;

	while(true)
	{
		uint32_t now = micros();
		uint32_t nowFrame = now / (1000000 / OUTPUT_PWM_FPS);
		uint32_t framePart = now % (1000000 / OUTPUT_PWM_FPS);

		if(lastUpdate != nowFrame)
		{
			uint8_t red = redPin.getPower();
			uint8_t green = greenPin.getPower();
			uint8_t blue = bluePin.getPower();

			colorTriple = 0;
			if(red != 0)
				colorTriple |= 0b010;
			if(green != 0)
				colorTriple |= 0b100;
			if(blue != 0)
				colorTriple |= 0b001;

			turnOffR = red * (1000000 / OUTPUT_PWM_FPS / 255);
			turnOffG = green * (1000000 / OUTPUT_PWM_FPS / 255);
			turnOffB = blue * (1000000 / OUTPUT_PWM_FPS / 255);

			setMcpPinTiples(colorTriple, 5);
			lastUpdate = nowFrame;
		}

		uint8_t newState = 0;
		if(framePart < turnOffR)
			newState |= 0b010;
		if(framePart < turnOffG)
			newState |= 0b100;
		if(framePart < turnOffB)
			newState |= 0b001;

		if(newState != colorTriple)
		{
			setMcpPinTiples(colorTriple, 5);
			colorTriple = newState;
		}
	}
}

void neoPixelWorker(void *param)
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

void setup()
{
	Serial.begin(115200);

	WiFi.mode(WIFI_OFF);

	I2CInstance.begin(21, 22, 100000);

	mcp.init();
	mcp.portMode(MCP23017Port::A, 0);
	mcp.portMode(MCP23017Port::B, 0);
	mcp.writeRegister(MCP23017Register::GPIO_A, 0x00);
	mcp.writeRegister(MCP23017Register::GPIO_B, 0x00);
	mcp.writeRegister(MCP23017Register::IPOL_A, 0x00);
	mcp.writeRegister(MCP23017Register::IPOL_B, 0x00);

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

		if((i & 31) == 0)
			setMcpPinTiples(0b111);
		else if((i & 31) == 16)
			setMcpPinTiples(0b000);

		delay(10);
	}

	delay(500);
	for(int i = 0; i < OUTPUT_PIN_COUNT; i++)
	{
		pixels[i]->fill(off);
		pixels[i]->show();
	}

	// callback, name, stackSize, parameter, proprity, task, core
	xTaskCreatePinnedToCore(pwmLedWorker, "PWM_LED_Worker", 4096, NULL, 1, &task1, 0);
	xTaskCreatePinnedToCore(neoPixelWorker, "NeoPixel_Worker", 4096, NULL, 1, &task2, 1);
}

void loop()
{
	delay(1000);
}
