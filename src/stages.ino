// stages
// 0: closed
// 1 : open ventilation
// 2 : open fan
// 3: open rollup 1/4
// 4: open rollup 1/2
// 5: open rollup 3/4
// 6: open rollup fully

// time to open rollup 1/4
#define ROLLUP_STAGE_DELAY_MS 30000

#define ACTUATOR_MAX_DELAY_MS 60000
#define ACTUATOR_IS_VAl_THRESHOLD 30 // gt than this threshold means current is flowing

#define STAGE_CHANGE_WAIT_MS 120000 // Duration between automatic stage changes (2 minutes)
#define STAGE_DELTA_CELCIUS 1

// Stage
enum Stage {WINDOW_CLOSED, WINDOW_OPEN, FAN, ROLLUP_1, ROLLUP_2, ROLLUP_3, ROLLUP_4};
Stage currentStage = WINDOW_CLOSED;
unsigned long stageStartTime = 0;
// unsigned long stageStopTime = 0;

int getStageIndex(Stage s) {
    switch (W) {
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
    currentStageTemp = currentTemp;
    stageStartTime = millis();
    Stage oldStage = currentStage;

    if (closing) {
        switch (oldStage) {
            case WINDOW_CLOSED :
                printTx("already at min stage");
                break;
            case WINDOW_OPEN :
                currentStage = WINDOW_CLOSED;
                closeWindowAsync();
                break;
            case FAN :
                currentStage = WINDOW_OPEN;
                stopFan();
                break;
            case ROLLUP_1 :
                currentStage = FAN;
                closeRollUpAsync(true);
                break;
            case ROLLUP_2 :
                currentStage = ROLLUP_1;
                closeRollUpAsync(false);
                break;
            case ROLLUP_3 :
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
            case WINDOW_CLOSED :
                currentStage = WINDOW_OPEN;
                openWindowAsync();
                break;
            case WINDOW_OPEN :
                currentStage = FAN;
                startFan();
                break;
            case FAN :
                currentStage = ROLLUP_1;
                openRollUpAsync(false);
                break;
            case ROLLUP_1 :
                currentStage = ROLLUP_2;
                openRollUpAsync(false);
                break;
            case ROLLUP_2 :
                currentStage = ROLLUP_3;
                openRollUpAsync(false);
                break;
            case ROLLUP_3 :
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
    if (elapsedTime - stageStartTime < STAGE_CHANGE_WAIT_MS) {
        printTx("Stage change must wait a bit...");
        return;
    }

    if (currentTemp > settings.maxDesiredTemp) {
        // reached min threshold to increase ventilation
        doStageUpdate(currentTemp, false);
    } else if (currentTemp <= settings.maxDesiredTemp) {
        // reached threshold to decrease ventilation
        doStageUpdate(currentTemp, true);
    }
}
