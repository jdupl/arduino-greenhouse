// TODO

// MUST write to eeprom settings
// MUST calculate time to open rollups
// MUST start up routine (close all systems)
// MUST display current stage
// MUST display current operation

// NICE current sensing from bts controllers for better error detection
// NICE track and display uptime
// NICE track and display min/max temps
// NICE furnace control



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

// OLED pins and wiring colors
#define OLED_SDA 20     // green
#define OLED_SCL 21     // yellow


// Keypad pins and wiring colors
#define R1 41       // yellow
#define R2 40       // red
#define R3 39       // black
#define R4 38       // white
#define C1 37       // brown
#define C2 36       // green
#define C3 35       // blue
#define C4 34       // orange


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
#define TICK_MS // tick length for keypad
#define TICK_TEMP_MS 10000 // tick length for temperature update
#define DEBUG_EN 1 // set to 1 or 0



// stages
// 0: closed
// 1 : open ventilation
// 2: open rollup 1/4
// 3: open rollup 1/2
// 4: open rollup 3/4
// 5: open rollup fully

#define ROLUP_DELAY_MS 30000

#define ACTUATOR_MAX_DELAY_MS 60000
#define ACTUATOR_IS_VAl_THRESHOLD 30 // gt than this threshold means current is flowing

#define STAGE_CHANGE_WAIT_MS 120000 // Duration between automatic stage changes (2 minutes)
#define STAGE_DELTA_CELCIUS 1

float currentTemp = -1;
float currentHumidity = -1;
bool dht22Working = true;

float minDesiredTemp = 24.0;
float maxDesiredTemp = 26.0;

int stageJump = -1; // set to -1 when stage has been jumped
bool stageFreeze = false; // true will avoid changing stages to lock to the current one
bool ventilationActivated = true;
bool rollupActivated = true;

unsigned long sensorLastTickTime = 0; // Time at the beginning of the last tick

bool closingStages = false; // Flag to indicate if stages are closing
float currentStageIndexTemp = 0;


// Stage
enum Stage {VENT_CLOSED, VENT_OPEN, ROLLUP_1, ROLLUP_2, ROLLUP_3, ROLLUP_4};
Stage currentStage = VENT_CLOSED;
unsigned long stageStartTime = 0;
// unsigned long stageStopTime = 0;


// Operation state (async operation of motors)
enum OpState {CLOSE_WINDOW, OPEN_WINDOW, ROLLUP_OP, ROLLDOWN_OP, IDLING};
OpState currentOpState = IDLING;
unsigned long operationStartTime = 0;
unsigned long operationStopTime = 0;

// Configuration menu options
enum ConfigOption { MIN_TEMP, MAX_TEMP, STAGE_JUMP, STAGE_FREEZE, VENTILATION, ROLLUP };
#define CONFIG_OPTION_COUNT 6 // UPDATE MANUALLY THIS COUNT

ConfigOption currentConfigOption = MIN_TEMP;

// Display state
enum DisplayState { CONFIG_MENU, STATS };
DisplayState displayState = STATS;

char hexaKeys[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[4] = {R1, R2, R3, R4};
byte colPins[4] = {C1, C2, C3, C4};
Keypad keypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, 4, 4);

DHT dht(DHT_PIN, DHT_TYPE);

// OneWire oneWire(ONE_WIRE_BUS);
//DallasTemperature sensors(&oneWire);

// U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

void scrollMenu(int direction) {
  currentConfigOption = static_cast<ConfigOption>((currentConfigOption + direction + CONFIG_OPTION_COUNT) % CONFIG_OPTION_COUNT);
}

void toggleOption(bool& option) {
  option = !option;
}

void promptNumericInputDisplay(const char* prompt, String displayValue) {
    display.clearDisplay();

    display.setTextSize(1); // Draw 2X-scale text
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 0);

    display.println(prompt);

    display.print("Value: ");
    display.println(displayValue);
    display.display();
}

float promptNumericInput(const char* prompt, float currentValue) {
  String input = "";
  promptNumericInputDisplay(prompt, String(currentValue));

  while (true) {
    char key = keypad.getKey();
    if (key != NO_KEY) {
      if (key == '#') {
        return input.toFloat();
      } else if (key == '*') {
        return currentValue; // Cancel input
      } else if (isdigit(key) || key == '.') {
        input += key;
      }
      promptNumericInputDisplay(prompt, input);
    }
    delay(50);
  }
}

void displayConfigMenu() {
  display.clearDisplay();
  display.setTextSize(1); // Draw 2X-scale text
  display.setTextColor(SSD1306_WHITE);


  int numOptions = 4; // Number of configuration options to display at a time
  int startOption = static_cast<int>(currentConfigOption) - 1;
  if (startOption < 0) startOption = 0;
  if (startOption + numOptions > CONFIG_OPTION_COUNT) startOption = CONFIG_OPTION_COUNT - numOptions;

  for (int i = 0; i < numOptions; i++) {
    display.setCursor(10, 10 + (i * 15));
    ConfigOption option = static_cast<ConfigOption>(startOption + i);

    switch (option) {
      case MIN_TEMP:
        display.print("Set Min Temp: ");
        display.print(minDesiredTemp);
        break;
      case MAX_TEMP:
        display.print("Set Max Temp: ");
        display.print(maxDesiredTemp);
        break;
      case STAGE_JUMP:
        display.print("Jump stage: ");
        display.print(stageJump);
        break;
      case STAGE_FREEZE:
        display.print("Freeze stages: ");
        display.print(stageFreeze ? "On" : "Off");
        break;
      case VENTILATION:
        display.print("Ventilation: ");
        display.print(ventilationActivated ? "On" : "Off");
        break;
      case ROLLUP:
        display.print("Rollup: ");
        display.print(rollupActivated ? "On" : "Off");
        break;
    }
  }

  // Display selector
  display.setCursor(0, 10 + ((currentConfigOption - startOption) * 15));
  display.print(">");

  display.display();
}

void printTx(String chars) {
    if (DEBUG_EN == 1) {
      Serial.println(chars);
    }
}

void updateOperation() {
    // Operation state (async operation of motors)
    if (currentOpState == IDLING) {
        // ignore
        return;
    }
    if (operationStopTime < millis()) {
        printTx("halting current operation due to timer");
        stopCurrentOperation();
        return;
    }
    if (!shouldCurrentOperationContinue()) {
        printTx("halting current operation due to it's conditions being met");
        stopCurrentOperation();
    }
}

void openRollUp() {
    printTx("opening rollup...");
    analogWrite(ROLLUP_RPWM, 255);
    analogWrite(ROLLUP_LPWM, 0);

    delay(ROLUP_DELAY_MS);
    printTx("done opening rollup");

    analogWrite(ROLLUP_RPWM, 0);
    analogWrite(ROLLUP_LPWM, 0);
}

void closeRollUp() {
    printTx("closing rollup...");
    analogWrite(ROLLUP_RPWM, 0);
    analogWrite(ROLLUP_LPWM, 255);

    delay(ROLUP_DELAY_MS);
    printTx("done closing rollup");

    analogWrite(ROLLUP_RPWM, 0);
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
    switch (currentOpState) {
        case OPEN_WINDOW:
            windowStop();
            startFan();
        case CLOSE_WINDOW:
            windowStop();
            break;
        case ROLLUP_OP:
        case ROLLDOWN_OP:
            printTx("TO IMPLEMENT");
            break;
    }
    currentOpState = IDLING;
}

void openWindowStart() {
    printTx("opening window...");
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

void closeWindowStart() {
    printTx("closing window request...");
    printTx("shutting down fan");
    stopFan();
    printTx("closing window...");
    operationStartTime = millis();
    operationStopTime = ACTUATOR_MAX_DELAY_MS + operationStartTime;

    analogWrite(ACTUATOR_LPWM, 255);
    analogWrite(ACTUATOR_RPWM, 0);
}

bool shouldCurrentOperationContinue() {
    switch (currentOpState) {
        case OPEN_WINDOW:
            return !limitSwitchTouched() && hasCurrentFlowing(ACTUATOR_R_IS);
        case CLOSE_WINDOW:
            return hasCurrentFlowing(ACTUATOR_L_IS);
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

void doStageUpdate(float currentTemp, bool closing) {
    currentStageTemp = currentTemp;
    closingStages = closing;
    stageStartTime = millis();

    if (closing) {
        switch (currentStage) {
            case VENT_CLOSED :
                printTx("already at min stage");
                break;
            case VENT_OPEN :
                currentStage = VENT_CLOSED;
                closeWindowStart();
            case ROLLUP_1 :
                currentStage = VENT_OPEN;
            case ROLLUP_2 :
                currentStage = ROLLUP_1;
            case ROLLUP_3 :
                currentStage = ROLLUP_2;
            case ROLLUP_4:
                currentStage = ROLLUP_3;
        }
    } else {
        switch (currentStage) {
            case VENT_CLOSED :
                currentStage = VENT_OPEN;
                openWindowStart();
            case VENT_OPEN :
                currentStage = ROLLUP_1;
            case ROLLUP_1 :
                currentStage = ROLLUP_2;
            case ROLLUP_2 :
                currentStage = ROLLUP_3;
            case ROLLUP_3 :
                currentStage = ROLLUP_4;
            case ROLLUP_4:
                printTx("already at max stage");
        }
    }
}

void updateStageStatus(float currentTemp) {
    // TODO check for early stage change if temp changed too much

    unsigned long elapsedTime = millis() - stageStartTime;
    if (elapsedTime - stageStartTime < STAGE_CHANGE_WAIT_MS) {
        printTx("Stage change must wait a bit...")
        return;
    }


    if (currentTemp > maxDesiredTemp) {
        // reached min threshold to increase ventilation
        doStageUpdate(currentTemp, false);
    } else if (currentTemp <= maxDesiredTemp) {
        // reached threshold to decrease ventilation
        doStageUpdate(currentTemp, true);
    }
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

void handleStatsKeyInput(char keyInput) {
  switch (keyInput) {
    case '#':
      displayState = CONFIG_MENU;
      break;
  }
}

void handleConfigMenuKeyInput(char keyInput) {
  switch (keyInput) {
    case 'A': // Scroll up
      scrollMenu(-1);
      displayConfigMenu();
      break;
    case 'B': // Scroll down
      scrollMenu(1);
      displayConfigMenu();
      break;
    case '*': // back to stats
      displayState = STATS;
      displayStats();
      break;
    case '#': // Choose option
        switch (currentConfigOption) {
        case MIN_TEMP:
            minDesiredTemp = promptNumericInput("Enter Min Temp: " , minDesiredTemp);
            break;
        case MAX_TEMP:
            maxDesiredTemp = promptNumericInput("Enter Max Temp: ", maxDesiredTemp);
            break;
        case STAGE_JUMP:
            stageJump = static_cast<int>(promptNumericInput(
                "0 closed 1 fans 2 1/4 rollup.. 4 100% rollup"
                , static_cast<float>(stageJump)));
            break;
        case STAGE_FREEZE:
          toggleOption(stageFreeze);
          break;
        case VENTILATION:
          toggleOption(ventilationActivated);

          // test code
          if (ventilationActivated) {
              currentOpState = OPEN_WINDOW;
              openWindowStart();

          } else {
              currentOpState = CLOSE_WINDOW;
              closeWindowStart();
          }
          ///
          break;
        case ROLLUP:
          toggleOption(rollupActivated);
          break;
      }

      displayConfigMenu();
      break;
  }
}

void handleKeyInput(char keyInput) {
  switch (displayState) {
    case STATS:
      handleStatsKeyInput(keyInput);
      break;
    case CONFIG_MENU:
      handleConfigMenuKeyInput(keyInput);
      break;
  }
}

void displayDHT22() {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 0);

    display.print("T: ");
    display.println(currentTemp);

    display.print("H: ");
    display.println(currentHumidity);

    display.display();
}

void displayDHT22Error() {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 0);

    display.println("DHT22 FATAL ERROR");

    display.display();
}

void displayStats() {
  if (dht22Working) {
      displayDHT22();
  } else {
      displayDHT22Error();
  }
}

void handleDisplayState() {
  if (displayState == STATS) {
    displayStats();
  } else {
    displayConfigMenu();
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

    char key = keypad.getKey();

    if (key != NO_KEY) {
        // Handle keypad input
        handleKeyInput(key);
    }

    handleDisplayState();
    updateOperation();

    delay(50);
}

void pinSetup() {
    // pinMode(FAN_RELAY, OUTPUT);

    pinMode(ACTUATOR_LPWM, OUTPUT);
    pinMode(ACTUATOR_RPWM, OUTPUT);
    pinMode(ACTUATOR_L_IS, INPUT);
    pinMode(ACTUATOR_R_IS, INPUT);
    pinMode(ACTUATOR_LIMIT_SWITCH, INPUT_PULLUP);
}


void setup() {
    Serial.begin(9600);

    printTx("booting up");
    pinSetup();

    dht.begin();

    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;);
    }

    displayState = STATS;
}
