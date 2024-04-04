/*
    Greenhouse controller

    featuring 'async like' operations powered with machine states

    rollup
    extractor fan and air intake
    oled display
    keypad input
    uptime tracker

*/

// TODO

// MUST track temperature delta from stage's start and trigger quicker change if needed

// NICE current sensing from bts controllers for better error detection
// NICE track and display min/max temps
// NICE furnace control

#include <DHT.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h>  // from  Mark Stanley and Alexander Brevig
#include <SPI.h>
#include <Wire.h>

/* // following includes are only for the IDE, Arduino concats all files
#include "eeprom.ino"
#include "sensors.ino"
#include "display.ino"
#include "stage.ino"
 */
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
#define STAGE_CHANGE_WAIT_MS 60000  // Duration between automatic stage changes (1 minute)
#define TICK_TEMP_MS 20000           // tick length for temperature update

/*
    CALIBRATION
*/

#define CURRENT_FLOW_THRESHOLD 60  // gt than this threshold means current is flowing
#define SETTINGS_ADR 0

#define DHT_TYPE DHT22
DHT dht(DHT_DATA_PIN, DHT_TYPE);

#define DEBUG_EN 1  // set to 1 or 0

// Display state
enum DisplayState {
    CONFIG_MENU,
    STATS,
    PROMPT
};

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

// Configuration menu options
enum ConfigOption {
    MAX_TEMP,
    STAGE_JUMP,
    STAGE_FREEZE,
    VENTILATION,
    ROLLUP
};
#define CONFIG_OPTION_COUNT 5  // UPDATE MANUALLY THIS COUNT


DisplayState displayState = STATS;

char lastChar = 'x';
String input = "";
const char *prompt = "";
String displayValue = "";

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


ConfigOption currentConfigOption = MAX_TEMP;


char hexaKeys[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}};

byte rowPins[4] = {R1, R2, R3, R4};
byte colPins[4] = {C1, C2, C3, C4};
Keypad keypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, 4, 4);

void printTx(String chars) {
    if (DEBUG_EN == 1) {
        Serial.println(chars);
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

    printTx("intializing pins");

    pinSetup();

    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;)
            ;
    }

    printTx("Ready to rock and roll");

    dht.begin();

    settings = readSettingsEEPROM();

    displayState = STATS;
    currentOperation = IDLING;

    // reset stages to 0
    currentStage = ROLLUP_4;
    stageJumpTargetIndex = 0;
}
