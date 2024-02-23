#include <DHT.h>
//#include <OneWire.h>
//#include <DallasTemperature.h>
#include <SPI.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <U8x8lib.h>
#include <Keypad.h> // from  Mark Stanley and Alexander Brevig

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
#define ACTUATOR_LIMIT_SWITCH 25  // green INPUT PULLUP


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
#define ACTUATOR_DELAY_MS 10000
#define NB_STAGES 6
#define STAGE_DURATION 300000 // Duration of each stage in milliseconds (5 minutes)
#define STAGE_DELTA_CELCIUS 1

float currentTemp = -1;
float currentHumidity = -1;
bool dht22Working = true;

float minDesiredTemp = 24.0;
float maxDesiredTemp = 26.0;

int stageOverride = 10; // 10 for automatic
bool ventilationActivated = true;
bool rollupActivated = true;

int currentStage = 0; // Initialize the stage to 0 (closed)
float currentStageTemp = 0;

unsigned long sensorLastTickTime = 0; // Time at the beginning of the last tick
unsigned long stageStartTime = 0; // Time at the beginning of the stage
bool closingStages = false; // Flag to indicate if stages are closing


// Configuration menu options
enum ConfigOption { MIN_TEMP, MAX_TEMP, STAGE_OVERRIDE, VENTILATION, ROLLUP };
#define CONFIG_OPTION_COUNT 5 // UPDATE MANUALLY THIS COUNT

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

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

void scrollMenu(int direction) {
  currentConfigOption = static_cast<ConfigOption>((currentConfigOption + direction + CONFIG_OPTION_COUNT) % CONFIG_OPTION_COUNT);
}

void toggleOption(bool& option) {
  option = !option;
}

float promptNumericInput(const char* prompt, float currentValue) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_squeezed_b6_tr); // Choose a font
  u8g2.setCursor(0, 10);
  u8g2.print(prompt);
  u8g2.setCursor(0, 25);
  u8g2.print("Current Value: ");
  u8g2.print(currentValue);
  u8g2.sendBuffer();

  String input = "";
  while (true) {
    char key = keypad.getKey();
    if (key != NO_KEY) {
      if (key == '#') {
        return input.toFloat();
      } else if (key == '*') {
        return currentValue; // Cancel input
      } else if (isdigit(key) || key == '.') {
        input += key;
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_squeezed_b6_tr); // Choose a font
        u8g2.setCursor(0, 10);
        u8g2.print(prompt);
        u8g2.setCursor(0, 25);
        u8g2.print("Current Value: ");
        u8g2.print(input);
        u8g2.sendBuffer();
      }
    }
  }
}

void displayConfigMenu() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_squeezed_b6_tr);

  int numOptions = 4; // Number of configuration options to display at a time
  int startOption = static_cast<int>(currentConfigOption) - 1;
  if (startOption < 0) startOption = 0;
  if (startOption + numOptions > CONFIG_OPTION_COUNT) startOption = CONFIG_OPTION_COUNT - numOptions;

  for (int i = 0; i < numOptions; i++) {
    u8g2.setCursor(10, 10 + (i * 15));
    ConfigOption option = static_cast<ConfigOption>(startOption + i);

    switch (option) {
      case MIN_TEMP:
        u8g2.print("Set Min Temp: ");
        u8g2.print(minDesiredTemp);
        break;
      case MAX_TEMP:
        u8g2.print("Set Max Temp: ");
        u8g2.print(maxDesiredTemp);
        break;
      case STAGE_OVERRIDE:
        u8g2.print("Stage Override: ");
        u8g2.print(stageOverride);
        break;
      case VENTILATION:
        u8g2.print("Ventilation: ");
        u8g2.print(ventilationActivated ? "On" : "Off");
        break;
      case ROLLUP:
        u8g2.print("Rollup: ");
        u8g2.print(rollupActivated ? "On" : "Off");
        break;
    }
  }

  // Display selector
  u8g2.setCursor(0, 10 + ((currentConfigOption - startOption) * 15));
  u8g2.print(">");

  u8g2.sendBuffer();
}

void printTx(String chars) {
    if (DEBUG_EN == 1) {
      Serial.println(chars);
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

void openWindow() {
    printTx("opening window...");
    analogWrite(ACTUATOR_RPWM, 255);
    analogWrite(ACTUATOR_LPWM, 0);

    delay(ACTUATOR_DELAY_MS);
    printTx("done opening window");

    analogWrite(ACTUATOR_RPWM, 0);
    analogWrite(ACTUATOR_LPWM, 0);
}

void closeWindow() {
    printTx("closing window...");
    analogWrite(ACTUATOR_RPWM, 0);
    analogWrite(ACTUATOR_LPWM, 255);

    delay(ACTUATOR_DELAY_MS);
    printTx("done closing window");

    analogWrite(ACTUATOR_RPWM, 0);
    analogWrite(ACTUATOR_LPWM, 0);
}

void startFan() {
    printTx("starting fan");
    digitalWrite(FAN_RELAY, HIGH);
}

void stopFan() {
    printTx("stopping fan");
    digitalWrite(FAN_RELAY, LOW);
}

void openVentilation() {
    openWindow();
    startFan();
}

void closeVentilation() {
    stopFan();
    closeWindow();
}

void closingStageUpdate() {
    if (currentStage == 0) {
        closeVentilation();
    } else {
        closeRollUp();
    }
}

void openingStageUpdate() {
    if (currentStage == 1) {
        openVentilation();
    } else {
        openRollUp();
    }
}

void doStageUpdate() {
    if (closingStages) {
        closingStageUpdate();
    } else {
        openingStageUpdate();
    }
}

void updateStageStatus(float currentTemp) {
    // update the current stage of ventilation
    unsigned long elapsedTime = millis() - stageStartTime;

    if (elapsedTime - stageStartTime < STAGE_DURATION) {
        // wait if stage wait time is not acheived
        return;
    }

    float deltaCurrentVsStage = currentTemp - currentStageTemp;

    if (deltaCurrentVsStage >= STAGE_DELTA_CELCIUS) {
        // reached threshold to increase ventilation

        if (currentStage < NB_STAGES) {
            // max stage already acheived
            return;
        }
        // Increase the current stage and reset the start time of the stage
        currentStage++;
        stageStartTime = elapsedTime;
        currentStageTemp = currentTemp;
        closingStages = false;
        doStageUpdate();

    } else if (deltaCurrentVsStage >= -STAGE_DELTA_CELCIUS) {
        // reached threshold to decrease ventilation

        if (currentStage == 0) {
            // min stage already acheived
            return;
        }

        // Decrease the current stage and reset the start time of the stage
        currentStage--;
        stageStartTime = elapsedTime;
        currentStageTemp = currentTemp;
        closingStages = true;
        doStageUpdate();
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
            minDesiredTemp = promptNumericInput("Enter Min Temp: ", minDesiredTemp);
            break;
        case MAX_TEMP:
          maxDesiredTemp = promptNumericInput("Enter Max Temp: ", maxDesiredTemp);
          break;
        case STAGE_OVERRIDE:
          stageOverride = static_cast<int>(promptNumericInput("Enter Stage Override \n 0 closed 6 automatic: ", static_cast<float>(stageOverride)));
          break;
        case VENTILATION:
          toggleOption(ventilationActivated);
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
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_squeezed_b6_tr);

    u8g2.setCursor(0, 10);
    u8g2.print("T: ");
    u8g2.print(currentTemp);

    u8g2.setCursor(0, 40);
    u8g2.print("H: ");
    u8g2.print(currentHumidity);

    u8g2.sendBuffer();
}

void displayDHT22Error() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_squeezed_b6_tr);
    u8g2.setCursor(0, 10);
    u8g2.print("DHT22 ERROR");
    u8g2.sendBuffer();
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
    // Check for keypad input
    char key = keypad.getKey();

    if (key != NO_KEY) {
        // Handle keypad input
        handleKeyInput(key);
    }

    handleDisplayState();

    delay(50);
}


void setup() {
    Serial.begin(9600);

    printTx("booting up");

    pinMode(FAN_RELAY, OUTPUT);

    u8g2.begin();
    dht.begin();

    displayState = STATS;

}
