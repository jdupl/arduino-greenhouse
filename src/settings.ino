#define SETTINGS_ADR 0



struct Settings {
    bool valid;
    float maxDesiredTemp;
    bool stageFreeze;
    bool ventilationActivated;
    bool rollupActivated;
};
Settings settings;


void saveSettigsEEPROM() {
    EEPROM.put(SETTINGS_ADR, settings);
}

Settings readSettingsEEPROM() {
    Settings eepromSettings;
    EEPROM.get(SETTINGS_ADR, eepromSettings);

    if (eepromSettings.valid) {
        printTx("Got valid settings from EEPROM");
        return eepromSettings;
    }

    printTx("!! No valid settings from EEPROM !! Switching to defaults.");
    Settings defaults = {
        true, // valid
        26.0f, // settings.maxDesiredTemp
        false, //  settings.stageFreeze
        true, // settings.ventilationActivated
        true // settings.rollupActivated
    };
    return defaults;
}
