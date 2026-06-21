#include "SCSend.hpp"
#include <cmath>
#include <algorithm>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

// Open namespace
START_NAMESPACE_DISTRHO

SCSend::SCSend()
    :Plugin(DISTRHO_PLUGIN_NUM_PARAMS, 0, 0),
    fThreshold(0.5f),
    fGain(1.0f),
    fAttackMs(10.0f),
    fReleaseMs(100.0f),
    fLink(1.0f),
    fSampleRate(48000.0f),
    fEnvelopeL(0.0f),
    fEnvelopeR(0.0f)
{
    // ask linux kernel to create/open a shared block of RAM
    fSharedFd = shm_open("/s2400_sb", O_CREAT | O_RDWR, 0666);

    if (fSharedFd >= 0) {
        // set the size to exactly 2 floats (8 bytes)
        ftruncate(fSharedFd, sizeof(float) * 2);

        // map that memory directly to pointer
        fSharedControl = (float*)mmap(0, sizeof(float) * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fSharedFd, 0);

        // init volume to 1.0 so SCRecv isn't muted by default
        if (fSharedControl != (float*)MAP_FAILED) {
            fSharedControl[0] = 1.0f;
            fSharedControl[1] = 1.0f;
        }
    } else {
        fSharedControl = (float*)MAP_FAILED;
    }
    
    activate();
}

SCSend::~SCSend() {
    // unlink and close memory safely when deleted
    if (fSharedControl != (float*)MAP_FAILED) {
        munmap(fSharedControl, sizeof(float) * 2);
    }
    if (fSharedFd >= 0) {
        close(fSharedFd);
    }
}

void SCSend::initParameter(uint32_t index, Parameter& parameter) {
    parameter.hints = kParameterIsAutomatable;

    switch (index) {
        case 0:
        parameter.name = "Threshold";
        parameter.symbol = "threshold";
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 1.0f;
        parameter.ranges.def = 0.5f;
        break;

        case 1:
        parameter.name = "Gain";
        parameter.symbol = "gain";
        parameter.ranges.min = 1.0f;
        parameter.ranges.max = 12.0f;
        parameter.ranges.def = 1.0f;
        break;

        case 2:
        parameter.name = "Attack (ms)";
        parameter.symbol = "attack";
        parameter.ranges.min = 1.0f;
        parameter.ranges.max = 100.0f;
        parameter.ranges.def = 10.0f;
        break;

        case 3:
        parameter.name = "Release (ms)";
        parameter.symbol = "release";
        parameter.ranges.min = 10.0f;
        parameter.ranges.max = 1000.0f;
        parameter.ranges.def = 100.0f;
        break;

        case 4:
        parameter.name = "Stereo Link";
        parameter.symbol = "link";

        // is an int with restricted list
        parameter.hints = kParameterIsAutomatable | kParameterIsInteger;
        parameter.ranges.min = 0;
        parameter.ranges.max = 1;
        parameter.ranges.def = 1;

        // define labels
        parameter.enumValues.count = 2;
        parameter.enumValues.restrictedMode = true;
        parameter.enumValues.values = new ParameterEnumerationValue[2];

        parameter.enumValues.values[0].value = 0;
        parameter.enumValues.values[0].label = "Off";

        parameter.enumValues.values[1].value = 1;
        parameter.enumValues.values[1].label = "On";
        break;

    }
}

float SCSend::getParameterValue(uint32_t index) const {
    switch (index) {
        case 0: return std::sqrt(fThreshold);
        case 1: return fGain;
        case 2: return fAttackMs;
        case 3: return fReleaseMs;
        case 4: return fLink;
        default: return 0.0f;
    }
}

void SCSend::setParameterValue(uint32_t index, float value) {
    // get samplerate from framework
    double sampleRate = getSampleRate();
    if (sampleRate <= 0.0) sampleRate = 48000.0; //safety net

    switch (index) {
        case 0:
        // Square value to make more sensitive in bottom range
        fThreshold = value * value;
        break;

        case 1:
        fGain = value;
        break;

        case 2:
        fAttackMs = value;
        fAttackCoef = 1.0f - std::exp(-1.0f / ((fAttackMs * 0.001f) * sampleRate));
        break;

        case 3:
        fReleaseMs = value;
        fReleaseCoef = 1.0f - std::exp(-1.0f / ((fReleaseMs * 0.001f) * sampleRate));
        break;

        case 4:
        fLink = value;
        break;
    }
}

void SCSend::activate() {
    fEnvelopeL = 0.0f;
    fEnvelopeR = 0.0f;
    // retrigger coef safely
    setParameterValue(2, fAttackMs);
    setParameterValue(3, fReleaseMs);
}

void SCSend::run(const float** inputs, float** outputs, uint32_t frames) {
    const float* inL = inputs[0]; // left audio in
    const float* inR = inputs[1]; //(inputs[1] != nullptr) ? inputs[1] : inputs[0]; // right audio in
    float* outL = outputs[0]; // left audio out
    float* outR = outputs[1];
    //bool hasRightOutput = (outputs[1] != nullptr); // right audio out

    for (uint32_t i = 0; i < frames; ++i) {
        // find loudest peak of either channel
        float absL = std::abs(inL[i]) * fGain;
        float absR = std::abs(inR[i]) * fGain;
        
        // stereo link
        if (fLink > 0.5f) {
            // linked
            float peak = std::max(absL, absR);
            absL = peak;
            absR = peak;
        }

        // env followers
        float coefL = (absL > fEnvelopeL) ? fAttackCoef : fReleaseCoef;
        fEnvelopeL = fEnvelopeL + coefL * (absL - fEnvelopeL);

        float coefR = (absR > fEnvelopeR) ? fAttackCoef : fReleaseCoef;
        fEnvelopeR = fEnvelopeR + coefR * (absR - fEnvelopeR);

        // denormal safety
        if (fEnvelopeL < 1e-6f) fEnvelopeL = 0.0f;
        if (fEnvelopeR < 1e-6f) fEnvelopeR = 0.0f;

        // threshold check - convert env to gain reduction val
        float sidechainL = 1.0f;
        float sidechainR = 1.0f;

        if (fEnvelopeL > fThreshold && fThreshold < 1.0f) {
            float amountOver = (fEnvelopeL - fThreshold) / (1.0f - fThreshold);
            if (amountOver > 1.0f) amountOver = 1.0f;
            sidechainL = 1.0f - amountOver; // Ducking depth equivalent
        }

        // safe division compression
        if (fEnvelopeR > fThreshold && fThreshold < 1.0f) {
            float amountOver = (fEnvelopeR - fThreshold) / (1.0f - fThreshold);
            if (amountOver > 1.0f) amountOver = 1.0f;
            sidechainR = 1.0f - amountOver;
        }

        // gain bounds
        sidechainL = std::max(0.0f, std::min(1.0f, sidechainL));
        sidechainR = std::max(0.0f, std::min(1.0f, sidechainR));

        // write to the mapped RAM
        if (fSharedControl != nullptr && fSharedControl != (float*)MAP_FAILED) {
            fSharedControl[0] = sidechainL;
            fSharedControl[1] = sidechainR;
        }

        // audio out is uneffected
        outL[i] = inL[i];
        outR[i] = inR[i];
        /*if (hasRightOutput) {
            outputs[1][i] = inR[i];
        }*/
    }
}

// Library entry point
Plugin* createPlugin() {
    return new SCSend();
}

// Close namespace
END_NAMESPACE_DISTRHO