// Excerpts from the real top-touch detection path.
void resetPettingDetection() {
    topReleased = false;
    topReleaseSamples = topTouchSamples = 0;
    if (!topSettleComplete) topSettleStarted = false;
}

void updatePetting(uint32_t now, bool eligible, bool stopped) {
    std::lock_guard<std::mutex> lock(labDevice.mutex);
    if (!pettingEnabled || !eligible || !pettingFaceEligible(now) || stopped || demoSensors.topError) {
        resetPettingDetection();
        return;
    }
    if (!demoSensors.topSampleFresh()) return;
    if (!topSettleComplete) {
        if (!topSettleStarted) { topSettleStarted = true; topSettleAt = now; }
        if (now - topSettleAt < 500) return;
        topSettleComplete = true;
    }
    const bool touched = demoSensors.top[0] || demoSensors.top[1] || demoSensors.top[2];
    if (!touched) {
        topTouchSamples = 0;
        if (!topReleased && ++topReleaseSamples >= 2) topReleased = true;
        return;
    }
    topReleaseSamples = 0;
    if (!topReleased) return;
    if (++topTouchSamples < 2) return;
    topReleased = false;
    topTouchSamples = 0;
    if (labDevice.runtime.petSequence(now, pettingSoundEnabled, esp_random()).execute)
        demoSensors.pulsePetting(now, labDevice.runtime.isQuiet(),
            labDevice.runtime.pettingAnimationState().animation == lab::PettingAnimation::Grumpy);
}
