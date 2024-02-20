#include <DHT.h>
//#include <OneWire.h>
//#include <DallasTemperature.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h> // from  Mark Stanley and Alexander Brevig

// sensors
#define DHT_PIN 22
#define ONE_WIRE_BUS 23

// rollup controller
#define ROLLUP_RPWM 13
#define ROLLUP_LPWM 12

#define OLED_SDA 20
#define OLED_SCL 21
#define OLED_ADDR   0x3C

#define R1 39
#define R2 41
#define R3 43
#define R4 45
#define C1 47
#define C2 49
#define C3 51
#define C4 53 

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_RESET     -1



// ventilation system

// actuator notes
// to extend: left RPM
// to retract: right RPM
#define ACTUATOR_RPWM 11
#define ACTUATOR_LPWM 10
#define FAN_RELAY 24


#define DHT_TYPE DHT22
#define TICK_MS // tick length for keypad
#define TICK_TEMP_MS 10000 // tick length for temperature update
#define DEBUG_EN 1 // set to 1 or 0



// stages
// 0: closed
// 1 : open ventilation
// 2: open rollup 30sec
// ...
// 5: open rollup 30sec

#define ROLUP_DELAY_MS 30000
#define ACTUATOR_DELAY_MS 10000
#define NB_STAGES 6
#define STAGE_DURATION 300000 // Duration of each stage in milliseconds (5 minutes)
#define STAGE_DELTA_CELCIUS 1

float desiredTemps[] = {23 ,25};

int currentStage = 0; // Initialize the stage to 0 (closed)
float currentStageTemp = 0;

unsigned long tickTime = 0; // Time at the beginning of the last tick
unsigned long stageStartTime = 0; // Time at the beginning of the stage
bool closingStages = false; // Flag to indicate if stages are closing

char hexaKeys[4][4] = {
  {'1', '2', '3', 'u'},
  {'4', '5', '6', 'd'},
  {'7', '8', '9', '>'},
  {'*', '0', '#', '<'}
};

byte rowPins[4] = {R1, R2, R3, R4}; 
byte colPins[4] = {C1, C2, C3, C4}; 
Keypad pad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, 4, 4); 

DHT dht(DHT_PIN, DHT_TYPE);

// OneWire oneWire(ONE_WIRE_BUS);
//DallasTemperature sensors(&oneWire);

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT);


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
    float tempInt = dht.readTemperature();
    float humidity = dht.readHumidity();

    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.print("T: ");
    display.println(tempInt);

    display.print("H: ");
    display.println(humidity);
    display.println("t set @");
    display.print(desiredTemps[0]);
    display.print(" to ");
    display.print(desiredTemps[1]);
    
    display.display();
}

void setTempMenu(String menu, int tempIndex) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.print("enter ");
    display.println(menu);
    display.display();

    String inputStr = "";

    while (true) {
        char key = pad.getKey();
        if (!key) {
          continue;
        }
        if (isDigit(key)) {
            inputStr += key;
               display.clearDisplay();
                display.setTextSize(3);
                display.setTextColor(WHITE);
                display.setCursor(0, 0);
                display.println(inputStr);
                display.display();
        }
        if (key == '>') {
            int inputInt = inputStr.toInt();
            desiredTemps[tempIndex] = inputInt;
            return;
        }
        if (key == '<') {
            return;
        }
        
        
    }
    
}

void goToMainMenu() {
    int selectedLine = 0;
    int menuCount = 2;
    String menus[menuCount] = { "min t", "max t", };
   
    
    while (true) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setTextColor(WHITE);
        display.setCursor(0, 0);

        for(int i = 0; i < menuCount; i++){
            display.print(menus[i]);
            if (selectedLine == i) {
                display.print('x');
            }
            display.println();   
        }
        display.display();

        char key = pad.getKey();
        
        if (key == 'u' && selectedLine > 0) {
            selectedLine--;
        } else if (key == 'd' && selectedLine  < menuCount) {
            selectedLine++;
        } else if (key == '<') {
            return;
        } else if (key == '>') {
            setTempMenu(menus[selectedLine], selectedLine);
          return;
        }
        delay(50);
        
    }

}

void readKey() {
    char customKey = pad.getKey();
    Serial.println(customKey);

    if (customKey == '>'){
        goToMainMenu();
    }
}

void loop() {
    unsigned long elapsedTimeSinceTempTick = millis() - tickTime;
    // float tempExt = getExtTemperature();
    if (elapsedTimeSinceTempTick > TICK_TEMP_MS || tickTime == 0) {
      updateTemp();
      tickTime = millis();
    }
    readKey();
    delay(10);
}


void setup() {
    Serial.begin(9600);
    Serial.println("test");

    if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;);
    }
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println("Initializing...");
    display.display();

    pinMode(FAN_RELAY, OUTPUT);



    dht.begin();
    //sensors.begin();
    delay(1000);
}
