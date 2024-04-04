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

void windowStop() {
    printTx("halting all window operation");
    analogWrite(ACTUATOR_RPWM, 0);
    analogWrite(ACTUATOR_LPWM, 0);
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

void triggerDhtUpdate() {
    if (dhtOperation == WAITING) {
        return;
    }
    // power on sensor and wait async for 2000ms
    printTx("starting dht sensors waiting 2s");
    digitalWrite(DHT_5V_PIN, HIGH);
    dhtUpdateReadyAt = millis() + 2000;
    dhtOperation = WAITING;
}

void triggerWindowOpen() {
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

void triggerWindowClose() {
    printTx("closing window request...");

    currentOperation = CLOSE_WINDOW_OP;
    operationStartTime = millis();
    operationStopTime = ACTUATOR_MAX_DELAY_MS + operationStartTime;

    analogWrite(ACTUATOR_LPWM, 255);
    analogWrite(ACTUATOR_RPWM, 0);
}

void triggerRolldown(bool fullyClose) {
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

void triggerRollUp(bool fullyOpen) {
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