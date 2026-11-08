#pragma once

#include "Effect.h"
#include <memory>

namespace spsx::fx
{
    /** Creates effect `index` (0..28, see EffectInfo). */
    std::unique_ptr<Effect> create (int index);

    // Families, defined in EffectsFilterDrive.cpp, EffectsTime.cpp, EffectsLofi.cpp,
    // EffectsPitch.cpp, EffectsLoop.cpp.
    std::unique_ptr<Effect> createFilterDrive();
    std::unique_ptr<Effect> createIsolator();
    std::unique_ptr<Effect> createEqualizer();
    std::unique_ptr<Effect> createWah();
    std::unique_ptr<Effect> createDistortion();
    std::unique_ptr<Effect> createOverdrive();
    std::unique_ptr<Effect> createFuzz();
    std::unique_ptr<Effect> createComp();
    std::unique_ptr<Effect> createBitCrash();

    std::unique_ptr<Effect> createDelay();
    std::unique_ptr<Effect> createTapeEcho();
    std::unique_ptr<Effect> createChorus();
    std::unique_ptr<Effect> createFlanger();
    std::unique_ptr<Effect> createPhaser();
    std::unique_ptr<Effect> createTremoloPan();
    std::unique_ptr<Effect> createReverb();

    std::unique_ptr<Effect> createNoiseGen();
    std::unique_ptr<Effect> createVinylSim();
    std::unique_ptr<Effect> createRadioTuning();
    std::unique_ptr<Effect> createCCanceler();
    std::unique_ptr<Effect> createSubsonic();
    std::unique_ptr<Effect> createRingMod();

    std::unique_ptr<Effect> createVoiceTrans();
    std::unique_ptr<Effect> createOctave();
    std::unique_ptr<Effect> createChromaticPS();
    std::unique_ptr<Effect> createPitch();

    std::unique_ptr<Effect> createDjfxLooper();
    std::unique_ptr<Effect> createBpmLooper();
    std::unique_ptr<Effect> createSlicerFlanger();
}
