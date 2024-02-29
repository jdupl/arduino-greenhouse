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


// Configuration menu options
enum ConfigOption { MAX_TEMP, STAGE_JUMP, STAGE_FREEZE, VENTILATION, ROLLUP };
#define CONFIG_OPTION_COUNT 5 // UPDATE MANUALLY THIS COUNT

ConfigOption currentConfigOption = MAX_TEMP;

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

char lastChar = 'x';

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


void displayConfigMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  int numOptions = 4; // Number of configuration options to display at a time
  int startOption = static_cast<int>(currentConfigOption) - 1;
  if (startOption < 0) startOption = 0;
  if (startOption + numOptions > CONFIG_OPTION_COUNT) startOption = CONFIG_OPTION_COUNT - numOptions;

  for (int i = 0; i < numOptions; i++) {
    display.setCursor(10, 10 + (i * 15));
    ConfigOption option = static_cast<ConfigOption>(startOption + i);

    switch (option) {
      // case MIN_TEMP:
      //   display.print("Set Min Temp: ");
      //   display.print(minDesiredTemp);
      //   break;
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
        case MAX_TEMP:
            settings.maxDesiredTemp = promptNumericInput("Enter Max Temp: ", settings.maxDesiredTemp);
            saveSettigsEEPROM();
            break;
        case STAGE_JUMP:
            stageJumpTargetIndex = static_cast<int>(promptNumericInput(
                "0 closed 1 fans 2 1/4 rollup.. 5 100% rollup"
                , static_cast<float>(stageJumpTargetIndex)));
            // TODO SET STAGE
            break;
        case STAGE_FREEZE:
          toggleOption(settings.stageFreeze);
          saveSettigsEEPROM();
          break;
        case VENTILATION:
          toggleOption(settings.ventilationActivated);
          saveSettigsEEPROM();
          break;
        case ROLLUP:
          toggleOption(settings.rollupActivated);
          saveSettigsEEPROM();
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
}

void displayDHT22Error() {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 0);

    display.println("DHT22 FATAL ERROR");
}

void displayStats() {
  if (dht22Working) {
      displayDHT22();
  } else {
      displayDHT22Error();
  }

  display.print("stage: ");
  display.println(String(currentStage));

  if (currentOperation != IDLING) {
      display.print("Op... ");
      char c = '+';
      if (lastChar == '+') {
          c = 'x';
          lastChar = 'x';
      } else {
          lastChar = '+';
      }
      display.print(c);

  }
  display.display();
}

void handleDisplayState() {
  if (displayState == STATS) {
    displayStats();
  } else {
    displayConfigMenu();
  }
}

void handleUserKeyAndDisplay() {
    char key = keypad.getKey();

    if (key != NO_KEY) {
        handleKeyInput(key);
    }

    handleDisplayState();
}
