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

void updateSensorsIfNeeded() {
    unsigned long elapsedTimeSinceTempTick = millis() - dhtLastUpdateTime;
    // float tempExt = getExtTemperature();
    if (elapsedTimeSinceTempTick > TICK_TEMP_MS || dhtLastUpdateTime == 0) {
        triggerDhtUpdate();
    }
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
            triggerDhtUpdate();
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