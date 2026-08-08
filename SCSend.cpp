#include "SCSend.hpp"
#include <cmath>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

// Open namespace
START_NAMESPACE_DISTRHO

SCSend::SCSend()
    : Plugin(DISTRHO_PLUGIN_NUM_PARAMS, 0, 0),
    fBusParam(0.0f)
{
    const char* busNames[4] = {"/sc_bus_a", "/sc_bus_b", "/sc_bus_c", "/sc_bus_d"};

    // ask linux kernel to create/open a shared block of RAM
    for (int i = 0; i < 4; ++i) {
        fSharedFds[i] = shm_open(busNames[i], O_CREAT | O_RDWR, 0666);

        if (fSharedFds[i] >= 0) {
            // set the size to exactly 1 float (4 bytes)
            ftruncate(fSharedFds[i], sizeof(StereoSidechain));

            // map that memory directly to pointer
            fBuses[i] = (StereoSidechain*)mmap(0, sizeof(StereoSidechain), PROT_READ | PROT_WRITE, MAP_SHARED, fSharedFds[i], 0);
            // set output to zero as default state
            if (fBuses[i] != (StereoSidechain*)MAP_FAILED) {
                fBuses[i]->left = 0.0f;
                fBuses[i]->right = 0.0f;
            }
        } else {
            fBuses[i] = (StereoSidechain*)MAP_FAILED;
        }
    }
}

SCSend::~SCSend() {
    // unlink and close memory safely when deleted
    for (int i = 0; i < 4; ++i) {
        if (fBuses[i] != (StereoSidechain*)MAP_FAILED) {
            munmap(fBuses[i], sizeof(StereoSidechain));
        }
        if (fSharedFds[i] >= 0) {
            close(fSharedFds[i]);
        }
    }
}

void SCSend::initParameter(uint32_t index, Parameter& parameter) {

    switch (index) {
        case 0:
        parameter.name = "Send Bus";
        parameter.symbol = "sendBus";
        parameter.hints = kParameterIsAutomatable | kParameterIsInteger;
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 3.0f;
        parameter.ranges.def = 0.0f;

        parameter.enumValues.count = 4;
        parameter.enumValues.restrictedMode = true;
        parameter.enumValues.values = new ParameterEnumerationValue[4];

        parameter.enumValues.values[0].value = 0;
        parameter.enumValues.values[0].label = "A";

        parameter.enumValues.values[1].value = 1;
        parameter.enumValues.values[1].label = "B";

        parameter.enumValues.values[2].value = 2;
        parameter.enumValues.values[2].label = "C";

        parameter.enumValues.values[3].value = 3;
        parameter.enumValues.values[3].label = "D";
        break;
    }
}

float SCSend::getParameterValue(uint32_t index) const {
    switch (index) {
        case 0: return fBusParam;
        default: return 0.0f;
    }
}

void SCSend::setParameterValue(uint32_t index, float value) {
    switch (index) {
        case 0:
        int oldBus = (int)fBusParam;
        fBusParam = value;
        int newBus = (int)fBusParam;

        // reset old bus to silence
        if (oldBus != newBus) {
            if (fBuses[oldBus] != (StereoSidechain*)MAP_FAILED) {
                fBuses[oldBus]->left = 0.0f;
                fBuses[oldBus]->right = 0.0f;
            }
        }
        break;
    }
}

void SCSend::run(const float** inputs, float** outputs, uint32_t frames) {
    const float* inL = inputs[0]; // left audio in
    const float* inR = (inputs[1] != nullptr) ? inputs[1] : inputs[0]; // right audio in
    float* outL = outputs[0]; // left audio out
    bool hasRightOutput = (outputs[1] != nullptr); // right audio out

    int currentBus = (int)fBusParam;

    for (uint32_t i = 0; i < frames; ++i) {
        // write absolute raw audio values directly to RAM
        if (fBuses[currentBus] != (StereoSidechain*)MAP_FAILED) {
            fBuses[currentBus]->left = std::abs(inL[i]);
            fBuses[currentBus]->right = std::abs(inR[i]);
        }

        // audio out is uneffected
        outL[i] = inL[i];
        if (hasRightOutput) {
            outputs[1][i] = inR[i];
        }
    }
}

// Library entry point
Plugin* createPlugin() {
    return new SCSend();
}

// Close namespace
END_NAMESPACE_DISTRHO
