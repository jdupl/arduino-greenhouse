// TODO

// MUST fix eeprom settings

// NICE current sensing from bts controllers for better error detection
// NICE track and display uptime
// NICE track and display min/max temps
// NICE furnace control

#include <DHT.h>
#include <EEPROM.h>
#include <avr/wdt.h>
// #include <OneWire.h>
// #include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h>  // from  Mark Stanley and Alexander Brevig
#include <SPI.h>
#include <Wire.h>

#define SCREEN_WIDTH 128     // OLED display width, in pixels
#define SCREEN_HEIGHT 64     // OLED display height, in pixels
#define SCREEN_ADDRESS 0x3C  ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/*
    ANALOG PINS
*/

#define ACTUATOR_R_IS 0  // analog white
#define ACTUATOR_L_IS 1  // analog grey
#define ROLLUP_L_IS 2    // analog purple
#define ROLLUP_R_IS 3    // analog blue

/*
    DIGITAL PINS
*/

// actuator notes
// to extend: left RPM
// to retract: right RPM
// Actuator controller pins and wiring colors
#define ACTUATOR_RPWM 11  // green
#define ACTUATOR_LPWM 10  // yellow

// rollup controller pins and wiring colors
#define ROLLUP_LPWM 12  // yellow
#define ROLLUP_RPWM 13  // green

// OLED pins and wiring colors
#define OLED_SDA 20  // green
#define OLED_SCL 21  // yellow

// limit switch wiring
// HIGH when untouched, LOW when touched
// 'C' pin GND  'NO' digital pin pulled up
#define ACTUATOR_LIMIT_SWITCH 23  // green INPUT PULLUP

// 120VAC fan relay pin and wiring color
#define FAN_RELAY 24  // brown

// Keypad pins and wiring colors
#define R1 41  // yellow
#define R2 40  // red
#define R3 39  // black
#define R4 38  // white
#define C1 37  // brown
#define C2 36  // green
#define C3 35  // blue
#define C4 34  // orange

#define DHT_DATA_PIN 42  // blue
#define DHT_5V_PIN 43    // white

/*
    DELAYS
*/
// time to open rollup 1/4
#define ROLLUP_STAGE_DELAY_MS 45000
#define ACTUATOR_MAX_DELAY_MS 60000
#define STAGE_CHANGE_WAIT_MS 120000  // Duration between automatic stage changes (2 minutes)
#define TICK_TEMP_MS 20000           // tick length for temperature update

/*
    CALIBRATION
*/

#define CURRENT_FLOW_THRESHOLD 60  // gt than this threshold means current is flowing
#define SETTINGS_ADR 0

#define DHT_TYPE DHT22
DHT dht(DHT_DATA_PIN, DHT_TYPE);

#define DEBUG_EN 1  // set to 1 or 0

// Operation (async operation of motors)
enum Operation {
    CLOSE_WINDOW_OP,
    OPEN_WINDOW_OP,
    ROLLUP_OP,
    ROLLDOWN_OP,
    IDLING
};

struct Settings {
    bool valid;
    float maxDesiredTemp;
    bool stageFreeze;
    bool ventilationActivated;
    bool rollupActivated;
};
Settings settings;

// stages
// 0: closed
// 1 : open ventilation
// 2 : open fan
// 3: open rollup 1/4
// 4: open rollup 1/2
// 5: open rollup 3/4
// 6: open rollup fully

// Stage
enum Stage {
    WINDOW_CLOSED,
    WINDOW_OPEN,
    FAN,
    ROLLUP_1,
    ROLLUP_2,
    ROLLUP_3,
    ROLLUP_4
};

enum DhtOperation {
    WAITING,
    DHT_IDLING
};

Stage currentStage = WINDOW_CLOSED;
unsigned long stageStartTime = 0;

Operation currentOperation = IDLING;
unsigned long operationStartTime = 0;
unsigned long operationStopTime = 0;

float currentTemp = -1;
float currentHumidity = -1;
bool dht22Working = true;
DhtOperation dhtOperation = DHT_IDLING;
unsigned long dhtLastUpdateTime = 0;  // Time at the beginning of the last tick
unsigned long dhtUpdateReadyAt = 0;
int dhtUpdateCurrentTry = 0;

int stageJumpTargetIndex = -1;

int getStageIndex(Stage s) {
    switch (s) {
        case WINDOW_CLOSED:
            return 0;
        case WINDOW_OPEN:
            return 1;
        case FAN:
            return 2;
        case ROLLUP_1:
            return 3;
        case ROLLUP_2:
            return 4;
        case ROLLUP_3:
            return 5;
        case ROLLUP_4:
            return 6;
    }
    return -1;
}

void doStageUpdate(float currentTemp, bool closing) {
    stageStartTime = millis();
    Stage oldStage = currentStage;
    printTx("stage update");

    if (closing) {
        switch (oldStage) {
            case WINDOW_CLOSED:
                printTx("already at min stage");
                break;
            case WINDOW_OPEN:
                currentStage = WINDOW_CLOSED;
                closeWindowAsync();
                break;
            case FAN:
                currentStage = WINDOW_OPEN;
                stopFan();
                break;
            case ROLLUP_1:
                currentStage = FAN;
                closeRollUpAsync(true);
                break;
            case ROLLUP_2:
                currentStage = ROLLUP_1;
                closeRollUpAsync(false);
                break;
            case ROLLUP_3:
                currentStage = ROLLUP_2;
                closeRollUpAsync(false);
                break;
            case ROLLUP_4:
                currentStage = ROLLUP_3;
                closeRollUpAsync(false);
                break;
        }
    } else {
        switch (oldStage) {
            case WINDOW_CLOSED:
                currentStage = WINDOW_OPEN;
                openWindowAsync();
                break;
            case WINDOW_OPEN:
                currentStage = FAN;
                startFan();
                break;
            case FAN:
                currentStage = ROLLUP_1;
                openRollUpAsync(false);
                break;
            case ROLLUP_1:
                currentStage = ROLLUP_2;
                openRollUpAsync(false);
                break;
            case ROLLUP_2:
                currentStage = ROLLUP_3;
                openRollUpAsync(false);
                break;
            case ROLLUP_3:
                currentStage = ROLLUP_4;
                openRollUpAsync(true);
                break;
            case ROLLUP_4:
                printTx("already at max stage");
                break;
        }
    }
}

void checkForStageChange() {
    // jump to user input stage. don't wait between stages
    if (stageJumpTargetIndex != -1) {
        int currentStageIndex = getStageIndex(currentStage);

        if (stageJumpTargetIndex == currentStageIndex) {
            stageJumpTargetIndex = -1;
        } else {
            bool decreaseStage = currentStageIndex > stageJumpTargetIndex;
            doStageUpdate(currentTemp, decreaseStage);
        }
        return;
    }

    // TODO check for early stage change if temp changed too much

    unsigned long elapsedTime = millis() - stageStartTime;
    if (stageStartTime > 0 && elapsedTime < STAGE_CHANGE_WAIT_MS) {
        printTx("Stage change must wait a bit...");
        return;
    }

    if (currentTemp >= settings.maxDesiredTemp + 1) {
        printTx("we should increase stage");
        // reached min threshold to increase ventilation
        doStageUpdate(currentTemp, false);
    } else if (currentTemp <= settings.maxDesiredTemp - 1) {
        // reached threshold to decrease ventilation
        printTx("we should decrease stage");
        doStageUpdate(currentTemp, true);
    }
}

void saveSettingsEEPROM() {
    EEPROM.put(SETTINGS_ADR, settings);
}

Settings readSettingsEEPROM() {
    Settings eepromSettings;
    EEPROM.get(SETTINGS_ADR, eepromSettings);

    // if (eepromSettings.valid) {
    if (false) {
        printTx("Got valid settings from EEPROM");
        return eepromSettings;
    }

    printTx("!! No valid settings from EEPROM !! Switching to defaults.");
    Settings defaults = {
        true,   // valid
        26.0f,  // settings.maxDesiredTemp
        false,  //  settings.stageFreeze
        true,   // settings.ventilationActivated
        true    // settings.rollupActivated
    };
    return defaults;
}

// Configuration menu options
enum ConfigOption {
    MAX_TEMP,
    STAGE_JUMP,
    STAGE_FREEZE,
    VENTILATION,
    ROLLUP
};
#define CONFIG_OPTION_COUNT 5  // UPDATE MANUALLY THIS COUNT

ConfigOption currentConfigOption = MAX_TEMP;

// Display state
enum DisplayState {
    CONFIG_MENU,
    STATS,
    PROMPT
};
DisplayState displayState = STATS;

char hexaKeys[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}};

byte rowPins[4] = {R1, R2, R3, R4};
byte colPins[4] = {C1, C2, C3, C4};
Keypad keypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, 4, 4);

char lastChar = 'x';

void scrollMenu(int direction) {
    currentConfigOption = static_cast<ConfigOption>((currentConfigOption + direction + CONFIG_OPTION_COUNT) % CONFIG_OPTION_COUNT);
}

void toggleOption(bool &option) {
    option = !option;
}

void displayConfigMenu() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    int numOptions = 4;  // Number of configuration options to display at a time
    int startOption = static_cast<int>(currentConfigOption) - 1;
    if (startOption < 0)
        startOption = 0;
    if (startOption + numOptions > CONFIG_OPTION_COUNT)
        startOption = CONFIG_OPTION_COUNT - numOptions;

    for (int i = 0; i < numOptions; i++) {
        display.setCursor(10, 10 + (i * 15));
        ConfigOption option = static_cast<ConfigOption>(startOption + i);

        switch (option) {
            case MAX_TEMP:
                display.print("Set Max Temp: ");
                display.print(settings.maxDesiredTemp);
                break;
            case STAGE_JUMP:
                display.print("Jump stage: ");
                display.print(stageJumpTargetIndex);
                break;
            case STAGE_FREEZE:
                display.print("Freeze stages: ");
                display.print(settings.stageFreeze ? "On" : "Off");
                break;
            case VENTILATION:
                display.print("Ventilation: ");
                display.print(settings.ventilationActivated ? "On" : "Off");
                break;
            case ROLLUP:
                display.print("Rollup: ");
                display.print(settings.rollupActivated ? "On" : "Off");
                break;
        }
    }

    // Display selector
    display.setCursor(0, 10 + ((currentConfigOption - startOption) * 15));
    display.print(">");

    display.display();
}

void handleStatsKeyInput(char keyInput) {
    switch (keyInput) {
        case '#':
            displayState = CONFIG_MENU;
            break;
    }
}

String input = "";
const char *prompt = "";
String displayValue = "";

void processCompleteNumericInput(float v) {
    switch (currentConfigOption) {
        case MAX_TEMP:
            settings.maxDesiredTemp = v;
            saveSettingsEEPROM();
            break;
        case STAGE_JUMP:
            stageJumpTargetIndex = static_cast<int>(v);
            break;
    }
}

void handlePromptKeyInput(char key) {
    if (key == '*') {
        input = "";
        displayState = CONFIG_MENU;
    } else if (key == '#') {
        float value = input.toFloat();
        input = "";
        displayState = CONFIG_MENU;
        // Process input value
        processCompleteNumericInput(value);
    } else if (isdigit(key) || key == '.') {
        input += key;
        displayValue = input;
    }
}

void startNumericPromptAsync(char *p, String displayVal) {
    prompt = p;
    displayValue = displayVal;
    displayState = PROMPT;
}

void handleConfigMenuKeyInput(char keyInput) {
    switch (keyInput) {
        case 'A':  // Scroll up
            scrollMenu(-1);
            displayConfigMenu();
            break;
        case 'B':  // Scroll down
            scrollMenu(1);
            displayConfigMenu();
            break;
        case '*':  // back to stats
            displayState = STATS;
            displayStats();
            break;
        case '#':  // Choose option
            switch (currentConfigOption) {
                case MAX_TEMP:
                    startNumericPromptAsync("Enter Max Temp: ", String(settings.maxDesiredTemp));
                    // saveSettingsEEPROM();
                    break;
                case STAGE_JUMP:
                    startNumericPromptAsync("0, 1 window 2 fans 3 1/4 rollup.. 6 100% rollup", String(stageJumpTargetIndex));
                    break;
                case STAGE_FREEZE:
                    settings.stageFreeze = !settings.stageFreeze;
                    saveSettingsEEPROM();
                    break;
                case VENTILATION:
                    toggleOption(settings.ventilationActivated);
                    saveSettingsEEPROM();
                    break;
                case ROLLUP:
                    toggleOption(settings.rollupActivated);
                    saveSettingsEEPROM();
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
        case PROMPT:
            handlePromptKeyInput(keyInput);
            break;
    }
}

void displayDHT22() {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 0);

    display.print("T: ");
    display.print(currentTemp);

    display.print(" H: ");
    display.println(currentHumidity);
}


void displayUptime() {
    unsigned long ms = millis();
    long days = 0;
    long hours = 0;
    long mins = 0;
    long secs = 0;
    String secs_o = ":";
    String mins_o = ":";
    String hours_o = ":";
    secs = ms / 1000; 
    mins = secs / 60; 
    hours = mins / 60;
    days = hours / 24;
    secs = secs - (mins * 60);
    mins = mins - (hours * 60);
    hours = hours - (days * 24);
    if (secs < 10) {
        secs_o = ":0";
    }
    if (mins < 10) {
        mins_o = ":0";
    }
    if (hours < 10) {
        hours_o = ":0";

    }

    display.println(days + hours_o + hours + mins_o + mins + secs_o + secs);
}

void displayDHT22Error() {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 0);

    display.println("DHT ERR");
}

void displayStats() {
    if (dht22Working) {
        displayDHT22();
    } else {
        displayDHT22Error();
    }

    display.print("stage: ");
    display.print(String(currentStage));

    if (currentOperation != IDLING) {
        display.print(" Op ");
        char c = '+';
        if (lastChar == '+') {
            c = 'x';
            lastChar = 'x';
        } else {
            lastChar = '+';
        }
        display.println(c);
    } else {
        display.println();
    }
    displayUptime();
    display.display();
}

void promptNumericInputDisplay() {
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 0);

    display.println(prompt);

    display.print("Value: ");
    display.println(displayValue);
    display.display();
}

void handleDisplayState() {
    switch (displayState) {
        case STATS:
            displayStats();
            break;
        case CONFIG_MENU:
            displayConfigMenu();
            break;
        case PROMPT:
            promptNumericInputDisplay();
            break;
    }
}

void handleUserKeyAndDisplay() {
    char key = keypad.getKey();

    if (key != NO_KEY) {
        handleKeyInput(key);
    }

    handleDisplayState();
}

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

    analogWrite(ROLLUP_RPWM, 255);
    analogWrite(ROLLUP_LPWM, 0);
}

bool limitSwitchTouched() {
    return digitalRead(ACTUATOR_LIMIT_SWITCH) == LOW;
}

int readAnalogPinSample(int pin, int targetMs) {
    unsigned long startTime = millis();
    unsigned long sum = 0;
    int numReadings = 0;

    while (millis() - startTime < targetMs) {
        int v = analogRead(pin);
        sum += v;
        numReadings++;
        delay(1);
    }
    return sum / numReadings;
}

bool hasCurrentFlowing(int pin) {
    // sample analog input for 50ms
    int meanVal = readAnalogPinSample(pin, 50);
    printTx(String(meanVal));
    return meanVal > CURRENT_FLOW_THRESHOLD;
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
    if (limitSwitchTouched()) {
        printTx("limit switch already touched");
        return;
    }
    currentOperation = OPEN_WINDOW_OP;

    operationStartTime = millis();
    operationStopTime = ACTUATOR_MAX_DELAY_MS + operationStartTime;

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

void handleDhtState() {
    if (dhtOperation == DHT_IDLING || millis() < dhtUpdateReadyAt) {
        printTx("not ready yet");
        // no reading needed or still powering on
        return;
    }

    printTx("reading dht");

    currentTemp = dht.readTemperature();
    currentHumidity = dht.readHumidity();

    dhtOperation = DHT_IDLING;
    // power off sensor
    digitalWrite(DHT_5V_PIN, LOW);

    // failed
    if (isnan(currentTemp) || isnan(currentHumidity)) {
        dhtUpdateCurrentTry++;
        printTx("Error while reading DHT22 data!");

        if (dhtUpdateCurrentTry > 3) {
            dht22Working = false;
            printTx("DHT failed to read after retrying !!");
            dhtLastUpdateTime = millis();
            dhtUpdateCurrentTry = 0;
        } else {
            startDhtUpdate();
        }
    } else {
        // success
        dht22Working = true;
        dhtUpdateCurrentTry = 0;
        dhtLastUpdateTime = millis();
        printTx("temp" + String(currentTemp));
        printTx("humidity" + String(currentHumidity));
    }
}

void startDhtUpdate() {
    if (dhtOperation == WAITING) {
        return;
    }
    // power on sensor and wait async for 2000ms
    printTx("starting dht sensors waiting 2s");
    digitalWrite(DHT_5V_PIN, HIGH);
    dhtUpdateReadyAt = millis() + 2000;
    dhtOperation = WAITING;
}

void updateSensorsIfNeeded() {
    unsigned long elapsedTimeSinceTempTick = millis() - dhtLastUpdateTime;
    // float tempExt = getExtTemperature();
    if (elapsedTimeSinceTempTick > TICK_TEMP_MS || dhtLastUpdateTime == 0) {
        startDhtUpdate();
    }
}

void loop() {
    wdt_reset();
    updateSensorsIfNeeded();
    handleDhtState();
    handleUserKeyAndDisplay();

    if (currentOperation == IDLING && !settings.stageFreeze) {
        checkForStageChange();
    } else if (currentOperation != IDLING) {
        checkIfOperationCompleted();
    }
    delay(100);
}

void pinSetup() {
    pinMode(DHT_5V_PIN, OUTPUT);

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

    wdt_disable();
    delay(2000);

    // watchdog reboot after 1s
    wdt_enable(WDTO_1S);

    pinSetup();

    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;)
            ;
    }

    dht.begin();

    settings = readSettingsEEPROM();

    displayState = STATS;
    currentOperation = IDLING;

    // reset stages to 0
    currentStage = ROLLUP_4;
    stageJumpTargetIndex = 0;
}
