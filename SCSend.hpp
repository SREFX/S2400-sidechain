#ifndef SC_SEND_HPP_INCLUDED
#define SC_SEND_HPP_INCLUDED

#include "DistrhoPlugin.hpp"

// Open namespace
START_NAMESPACE_DISTRHO

// struct to set RAM size
struct StereoSidechain {
    float left;
    float right;
};

class SCSend : public Plugin {

public:
    SCSend();
    ~SCSend() override; // destructor to clean up RAM when plug is removed

protected:
    // plugin information
    const char* getLabel() const override { return "SCSend"; }
    const char* getMaker() const override { return "Fxxxxx"; }
    const char* getLicense() const override { return "GPLv3"; }
    uint32_t getVersion() const override { return d_version(0, 0, 6); }
    int64_t getUniqueId() const override { return d_cconst('s', 'c', 'S', '6'); }

    // init
    void initParameter(uint32_t index, Parameter& parameter) override;

    // internal state
    float getParameterValue(uint32_t index) const override;
    void setParameterValue(uint32_t index, float value) override;

    // processing
    void activate() override;
    void run (const float** inputs, float** outputs, uint32_t frames) override;

private:
    // params
    float fThreshold, fGain, fAttackMs, fReleaseMs, fLink, fBusParam;

    // internal states
    float fSampleRate, fEnvelopeL, fEnvelopeR, fAttackCoef, fReleaseCoef;

    // POSIX shared memory variables
    StereoSidechain* fBuses[4];
    int fSharedFds[4];

    // helper
    

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SCSend)

};

// Close namespace
END_NAMESPACE_DISTRHO

#endif