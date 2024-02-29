// TODO

// MUST start up routine (close all systems)

// NICE display current stage
// NICE display current operation
// NICE current sensing from bts controllers for better error detection
// NICE track and display uptime
// NICE track and display min/max temps
// NICE furnace control

#include <EEPROM.h>
#include <DHT.h>
//#include <OneWire.h>
//#include <DallasTemperature.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <Keypad.h> // from  Mark Stanley and Alexander Brevig

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);


// sensors
#define DHT_PIN 22
// #define ONE_WIRE_BUS 23

// rollup controller pins and wiring colors
#define ROLLUP_RPWM 13  // green
#define ROLLUP_LPWM 12  // yellow
#define ROLLUP_R_IS 3  // analog blue
#define ROLLUP_L_IS 2  // analog purple




// actuator notes
// to extend: left RPM
// to retract: right RPM
// Actuator controller pins and wiring colors
#define ACTUATOR_RPWM 11  //green
#define ACTUATOR_LPWM 10  // yellow
#define ACTUATOR_R_IS 0  //analog white
#define ACTUATOR_L_IS 1  // analog grey

// limit switch wiring
// HIGH when untouched, LOW when touched
// C pin GND
// NO pin D25 pull-up
// NC pin not connected
#define ACTUATOR_LIMIT_SWITCH 23  // green INPUT PULLUP

// 120VAC fan relay pin and wiring color
#define FAN_RELAY 24      // brown


#define DHT_TYPE DHT22
#define TICK_TEMP_MS 10000 // tick length for temperature update
#define DEBUG_EN 1 // set to 1 or 0


// settings saved to eeprom

float currentTemp = -1;
float currentHumidity = -1;
bool dht22Working = true;
unsigned long sensorLastTickTime = 0; // Time at the beginning of the last tick
float currentStageTemp = 0;
int stageJumpTargetIndex = -1;

// Operation (async operation of motors)
enum Operation {CLOSE_WINDOW_OP, OPEN_WINDOW_OP, ROLLUP_OP, ROLLDOWN_OP, IDLING};
Operation currentOperation = IDLING;
unsigned long operationStartTime = 0;
unsigned long operationStopTime = 0;

DHT dht(DHT_PIN, DHT_TYPE);

// OneWire oneWire(ONE_WIRE_BUS);
//DallasTemperature sensors(&oneWire);

void printTx(String chars) {
    if (DEBUG_EN == 1) {
      Serial.println(chars);
    }
}

void checkIfOperationCompleted() {
    // Operation state (async operation of motors)
    if (currentOperation == IDLING) {
        // ignore
        return;
    }
    if (operationStopTime < millis()) {
        printTx("halting current operation due to timer");
        stopCurrentOperation();
    }
    if (!shouldCurrentOperationContinue()) {
        printTx("halting current operation due to it's conditions being met");
        stopCurrentOperation();
    }
}

void rollupStop() {
    printTx("done rollup operation");
    analogWrite(ROLLUP_RPWM, 0);
    analogWrite(ROLLUP_LPWM, 0);
}

void openRollUpAsync(bool fullyOpen) {
    printTx("opening rollup async...");
    currentOperation = ROLLUP_OP;
    operationStartTime = millis();

    if (!fullyOpen) {
        operationStopTime = ROLLUP_STAGE_DELAY_MS + operationStartTime;
    } else {
        operationStopTime = ROLLUP_STAGE_DELAY_MS * 4 + operationStartTime;
    }

    printTx(String(operationStartTime));
    printTx(String(operationStopTime));

    analogWrite(ROLLUP_RPWM, 0);
    analogWrite(ROLLUP_LPWM, 255);
}

void closeRollUpAsync(bool fullyClose) {
    printTx("closing rollup async...");
    currentOperation = ROLLDOWN_OP;

    operationStartTime = millis();

    if (!fullyClose) {
        operationStopTime = ROLLUP_STAGE_DELAY_MS + operationStartTime;
    } else {
        operationStopTime = ROLLUP_STAGE_DELAY_MS * 4 + operationStartTime;
    }

    printTx(String(operationStartTime));
    printTx(String(operationStopTime));

    analogWrite(ROLLUP_RPWM, 255);
    analogWrite(ROLLUP_LPWM, 0);
}

bool limitSwitchTouched() {
    return digitalRead(ACTUATOR_LIMIT_SWITCH) == LOW;
}

int readAnalogPinSample(int pin, int ms) {
    unsigned long startTime = micros();
    unsigned long sum = 0;
    int numReadings = 0;
    int targetMicros = ms * 1000;

    // Loop until sampling window is over
    while (micros() - startTime < targetMicros) {
        sum += analogRead(pin);
        numReadings++;
    }

    return sum / numReadings;
}

bool hasCurrentFlowing(int pin) {
    // sample analog input for 10ms
    return readAnalogPinSample(pin, 10) > ACTUATOR_IS_VAl_THRESHOLD;
}

void stopCurrentOperation() {
    switch (currentOperation) {
        case OPEN_WINDOW_OP:
            windowStop();
        case CLOSE_WINDOW_OP:
            windowStop();
            break;
        case ROLLUP_OP:
            rollupStop();
            break;
        case ROLLDOWN_OP:
            rollupStop();
            break;
    }
    currentOperation = IDLING;
}

void openWindowAsync() {
    printTx("opening window...");
    currentOperation = OPEN_WINDOW_OP;

    operationStartTime = millis();
    operationStopTime = ACTUATOR_MAX_DELAY_MS + operationStartTime;

    printTx(String(operationStartTime));
    printTx(String(operationStopTime));

    analogWrite(ACTUATOR_RPWM, 255);
    analogWrite(ACTUATOR_LPWM, 0);
}

void windowStop() {
    printTx("halting all window operation");
    analogWrite(ACTUATOR_RPWM, 0);
    analogWrite(ACTUATOR_LPWM, 0);
}

void closeWindowAsync() {
    printTx("closing window request...");

    currentOperation = CLOSE_WINDOW_OP;
    operationStartTime = millis();
    operationStopTime = ACTUATOR_MAX_DELAY_MS + operationStartTime;

    analogWrite(ACTUATOR_LPWM, 255);
    analogWrite(ACTUATOR_RPWM, 0);
}

bool shouldCurrentOperationContinue() {
    switch (currentOperation) {
        case OPEN_WINDOW_OP:
            return !limitSwitchTouched() && hasCurrentFlowing(ACTUATOR_R_IS);
        case CLOSE_WINDOW_OP:
            return hasCurrentFlowing(ACTUATOR_L_IS);
        case ROLLUP_OP:
            return hasCurrentFlowing(ROLLUP_L_IS);
        case ROLLDOWN_OP:
            return hasCurrentFlowing(ROLLUP_R_IS);
        case IDLING:
            return true;
    }
}

void startFan() {
    printTx("starting fan");
    digitalWrite(FAN_RELAY, HIGH);
}

void stopFan() {
    printTx("stopping fan");
    digitalWrite(FAN_RELAY, LOW);
}

// float getExtTemperature() {
//     sensors.requestTemperatures();
//     float temp = sensors.getTempCByIndex(0);
//     return temp;
// }
void updateTemp() {
    // update dht22 sensor values
    int maxTries = 5;
    int tryDelayMs = 250;
    bool success = false;
    int currentTry = 0;

    while (!success && currentTry < maxTries) {
        printTx("reading dht22 try #" + String(currentTry));
        currentTemp = dht.readTemperature();
        currentHumidity = dht.readHumidity();

        if (isnan(currentTemp) || isnan(currentHumidity)) {
            printTx("Error while reading DHT22 data!");
            dht22Working = false;
            currentTry++;
            delay(tryDelayMs);
        } else {
            printTx("temp" + String(currentTemp));
            printTx("humidity" + String(currentHumidity));
            success = true;
            dht22Working = true;
        }
    }
    if (!success) {
        printTx("Could not read DHT22 after maxTries !!!");
    }
}

void updateSensorsIfNeeded() {
    unsigned long elapsedTimeSinceTempTick = millis() - sensorLastTickTime;
    // float tempExt = getExtTemperature();
    if (elapsedTimeSinceTempTick > TICK_TEMP_MS || sensorLastTickTime == 0) {
      updateTemp();
      sensorLastTickTime = millis();
    }
}

void loop() {
    updateSensorsIfNeeded();
    handleUserKeyAndDisplay();

    if (currentOperation == IDLING && !settings.stageFreeze) {
        checkForStageChange();
    } else if (currentOperation != IDLING) {
        checkIfOperationCompleted();
    }
    delay(50);
}

void pinSetup() {
    pinMode(FAN_RELAY, OUTPUT);

    pinMode(ACTUATOR_LPWM, OUTPUT);
    pinMode(ACTUATOR_RPWM, OUTPUT);
    pinMode(ACTUATOR_L_IS, INPUT);
    pinMode(ACTUATOR_R_IS, INPUT);

    pinMode(ROLLUP_LPWM, OUTPUT);
    pinMode(ROLLUP_RPWM, OUTPUT);
    pinMode(ROLLUP_L_IS, INPUT);
    pinMode(ROLLUP_R_IS, INPUT);

    pinMode(ACTUATOR_LIMIT_SWITCH, INPUT_PULLUP);
}

void setup() {
    Serial.begin(9600);
    printTx("booting up");

    pinSetup();
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;);
    }
    dht.begin();

    settings = readSettingsEEPROM();

    displayState = STATS;
    currentOperation = IDLING;
}
