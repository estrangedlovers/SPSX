#include "Effects.h"

namespace spsx::fx
{
    std::unique_ptr<Effect> create (int index)
    {
        switch (index)
        {
            case 0:  return createFilterDrive();
            case 1:  return createVoiceTrans();
            case 2:  return createDelay();
            case 3:  return createIsolator();
            case 4:  return createDjfxLooper();
            case 5:  return createReverb();
            case 6:  return createTapeEcho();
            case 7:  return createChorus();
            case 8:  return createFlanger();
            case 9:  return createPhaser();
            case 10: return createTremoloPan();
            case 11: return createDistortion();
            case 12: return createOverdrive();
            case 13: return createFuzz();
            case 14: return createWah();
            case 15: return createOctave();
            case 16: return createComp();
            case 17: return createEqualizer();
            case 18: return createBitCrash();
            case 19: return createNoiseGen();
            case 20: return createVinylSim();
            case 21: return createRadioTuning();
            case 22: return createSlicerFlanger();
            case 23: return createRingMod();
            case 24: return createChromaticPS();
            case 25: return createPitch();
            case 26: return createCCanceler();
            case 27: return createSubsonic();
            case 28: return createBpmLooper();
            default: return nullptr;
        }
    }
}
