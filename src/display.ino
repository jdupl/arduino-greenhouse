

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

    display.print("s: ");
    display.print(String(currentStage));

    if (currentOperation != IDLING) {
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

void scrollMenu(int direction) {
    currentConfigOption = static_cast<ConfigOption>((currentConfigOption + direction + CONFIG_OPTION_COUNT) % CONFIG_OPTION_COUNT);
}

void toggleOption(bool &option) {
    option = !option;
}


void handleStatsKeyInput(char keyInput) {
    switch (keyInput) {
        case '#':
            displayState = CONFIG_MENU;
            break;
    }
}

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