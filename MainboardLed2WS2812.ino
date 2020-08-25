#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <MCP23017.h>

#define PIN_INPUT_RED 35
#define PIN_INPUT_GREEN 34
#define PIN_INPUT_BLUE 33

#define PWM_INSPECTION_RANGE 1024

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

void setMcpPinTiples(uint8_t state)
{
	uint8_t aState = 0;
	uint8_t bState = 0;

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

	mcp.writePort(MCP23017Port::A, aState);
	mcp.writePort(MCP23017Port::B, bState);
}

void setup()
{
	Serial.begin(115200);

	I2CInstance.begin(21, 22, 100000);

	mcp.init();
	mcp.portMode(MCP23017Port::A, 0);
	mcp.portMode(MCP23017Port::B, 0);
	mcp.writeRegister(MCP23017Register::GPIO_A, 0x00);
	mcp.writeRegister(MCP23017Register::GPIO_B, 0x00);
	mcp.writeRegister(MCP23017Register::IPOL_A, 0x00);
	mcp.writeRegister(MCP23017Register::IPOL_B, 0x00);

	setMcpPinTiples(0b111);
	delay(3000);
	setMcpPinTiples(0b000);
	delay(3000);

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

		if((i & 7) == 0)
			setMcpPinTiples(0b111);
		else if((i & 7) == 4)
			setMcpPinTiples(0b000);

		delay(50);
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
	static uint32_t lastUpdate = 0;
	static uint32_t lastNeopixelUpdate = 0;

	static uint8_t colorTriple = 0b111;
	static uint32_t turnOffR = 0;
	static uint32_t turnOffG = 0;
	static uint32_t turnOffB = 0;

	uint32_t now = micros();
	uint32_t nowSec = millis() / 1000;
	uint32_t nowFrame = now / (1000000 / 200);
	uint32_t framePart = now % (1000000 / 200);

	if(lastUpdate != nowFrame)
	{
		uint8_t red = redPin.getPower();
		uint8_t green = greenPin.getPower();
		uint8_t blue = bluePin.getPower();
		uint32_t color = Adafruit_NeoPixel::Color(red, green, blue);

		colorTriple = 0;
		if(red != 0)
			colorTriple |= 0b010;
		if(green != 0)
			colorTriple |= 0b100;
		if(blue != 0)
			colorTriple |= 0b001;

		turnOffR = red * (1000000 / 200 / 255);
		turnOffG = green * (1000000 / 200 / 255);
		turnOffB = blue * (1000000 / 200 / 255);

		if(nowFrame % 120 == 0)
		{
			Serial.print("state : ");
			Serial.print(color, 16);
			Serial.print(" ");
			Serial.print(turnOffR);
			Serial.print(" ");
			Serial.print(turnOffG);
			Serial.print(" ");
			Serial.print(turnOffB);
			Serial.println();
		}

		setMcpPinTiples(colorTriple);

		if(nowSec != lastNeopixelUpdate)
		{
			for(int i = 0; i < OUTPUT_PIN_COUNT; i++)
			{
				pixels[i]->fill(color);
				pixels[i]->show();
			}
			lastNeopixelUpdate = nowSec;
		}

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
		setMcpPinTiples(newState);
		colorTriple = newState;
	}
}
