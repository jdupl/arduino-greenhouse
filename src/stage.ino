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
                triggerWindowClose();
                break;
            case FAN:
                currentStage = WINDOW_OPEN;
                stopFan();
                break;
            case ROLLUP_1:
                currentStage = FAN;
                triggerRolldown(true);
                break;
            case ROLLUP_2:
                currentStage = ROLLUP_1;
                triggerRolldown(false);
                break;
            case ROLLUP_3:
                currentStage = ROLLUP_2;
                triggerRolldown(false);
                break;
            case ROLLUP_4:
                currentStage = ROLLUP_3;
                triggerRolldown(false);
                break;
        }
    } else {
        switch (oldStage) {
            case WINDOW_CLOSED:
                currentStage = WINDOW_OPEN;
                triggerWindowOpen();
                break;
            case WINDOW_OPEN:
                currentStage = FAN;
                startFan();
                break;
            case FAN:
                currentStage = ROLLUP_1;
                triggerRollUp(false);
                break;
            case ROLLUP_1:
                currentStage = ROLLUP_2;
                triggerRollUp(false);
                break;
            case ROLLUP_2:
                currentStage = ROLLUP_3;
                triggerRollUp(false);
                break;
            case ROLLUP_3:
                currentStage = ROLLUP_4;
                triggerRollUp(true);
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