#include "PanelModel.h"
#include "../Tempo.h"
#include "../sampler/TimeModify.h"
#include <cmath>
#include <cstdio>

// Switches over Control handle the relevant subset and default the rest on purpose.
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wswitch-enum")

namespace spsx
{
    namespace
    {
        constexpr double kSleepSeconds = 300.0;        // "approximately five minutes" (p. 40)
        constexpr double kTapResetSeconds = 2.0;

        int knobToIndex (double v, int count)
        {
            return juce::jlimit (0, count - 1, (int) std::lround (v * (count - 1)));
        }

        // ---- FUNC setting options (manual p. 25, 39, 40, 42, 47) -------------------
        const std::array<const char*, 5> kIllumination { "oFF", "r b", "b", "r", "SLP" };
        const std::array<const char*, 3> kMidiSync { "AUt", "tMp", "oFF" };
        const std::array<int, 2> kInputGain { -10, 0 };
        const std::array<int, 6> kOutputGain { -3, 0, 3, 6, 9, 12 };
    }

    PanelModel::PanelModel (Engine& e, SamplerController& c)
        : engine (e), controller (c)
    {
    }

    const std::array<const char*, 10>& PanelModel::quantizeNames()
    {
        static const std::array<const char*, 10> names { "S.8", "S.16", "4", "4.3", "8", "8.3", "16", "16.3", "32", "oFF" };
        return names;
    }

    std::vector<int> PanelModel::patternLengths()
    {
        // 1..20 in single bars, then 4-bar steps; 99 reachable [SPEC U12, assumed].
        std::vector<int> v;
        for (int i = 1; i <= 20; ++i) v.push_back (i);
        for (int i = 24; i <= 96; i += 4) v.push_back (i);
        v.push_back (99);
        return v;
    }

    DisplayText PanelModel::remainDisplay (double seconds)
    {
        const auto s = (long long) std::max (0.0, seconds);
        char buf[48];
        if (s < 600)            std::snprintf (buf, sizeof buf, "%lld.%02lld", s / 60, s % 60);
        else if (s < 6000)      std::snprintf (buf, sizeof buf, "%lld.%lld", s / 60, (s % 60) / 10);
        else if (s < 60000)     std::snprintf (buf, sizeof buf, "%lld", s / 60);
        else                    std::snprintf (buf, sizeof buf, "%lldH", std::min (99LL, s / 3600));
        return DisplayText::make (buf);
    }

    // ---- helpers ----------------------------------------------------------------------

    bool PanelModel::hasSample (PadId p) const
    {
        return p.isValid() && p.bank < controller.project().numBanks && ! controller.project().slot (p).isEmpty();
    }

    bool PanelModel::bankProtected (int b) const
    {
        const auto& prot = patternMode ? controller.project().patternBankProtected : controller.project().sampleBankProtected;
        return b >= 0 && b < (int) prot.size() && prot[(size_t) b];
    }

    void PanelModel::flash (DisplayText d, double now, double seconds)
    {
        tempDisplay = d;
        tempUntil = now + seconds;
    }

    void PanelModel::sendEngine (Command::Type t, PadId p, double v)
    {
        Command c;
        c.type = t;
        c.pad = p;
        c.value = v;
        engine.post (c);
    }

    void PanelModel::reportStatus (SamplerController::Status s, double now)
    {
        using S = SamplerController::Status;
        switch (s)
        {
            case S::protectedBank: flash ("Prt", now); break;
            case S::full:          flash ("FUL", now); break;
            case S::unsupported:   flash ("UnS", now); break;
            case S::emptyPad:      flash ("ENP", now); break;
            default: break;
        }
    }

    DisplayText PanelModel::bpmDisplay (double bpm)
    {
        if (bpm <= 0.0)
            return DisplayText::make ("---");
        if (bpm < 100.0)
        {
            char buf[8];
            std::snprintf (buf, sizeof buf, "%.1f", std::round (bpm * 10.0) / 10.0);
            return DisplayText::make (buf);
        }
        return DisplayText::number ((int) std::lround (bpm));
    }

    DisplayText PanelModel::sampleBpmDisplay() const
    {
        const auto* s = controller.settings (current);
        return hasSample (current) && s != nullptr ? bpmDisplay (s->bpm) : DisplayText::make ("---");
    }

    // ---- input ------------------------------------------------------------------------

    void PanelModel::press (Control c, double now)
    {
        lastActivity = now;

        if (sleeping)
        {
            // "To exit Sleep mode, press any button" (p. 40): the press only wakes the unit.
            sleeping = false;
            sendEngine (Command::Type::resume);
            return;
        }

        if (c == Control::func)   { funcHeld = true; return; }

        if (paused)
        {
            // Only FUNC + TAP TEMPO (resume) works while paused (p. 12).
            if (c == Control::tapTempo && funcHeld)
            {
                paused = false;
                sendEngine (Command::Type::resume);
            }
            return;
        }

        if (isPad (c))          { pressPad (padIndex (c), now); return; }
        if (isEffectButton (c)) { pressEffect (effectButtonIndex (c), now); return; }
        if (isBank (c))         { pressBank (bankButtonIndex (c), now); return; }

        switch (c)
        {
            case Control::del:        pressDel (now); break;
            case Control::rec:        pressRec (now); break;
            case Control::mark:       pressMark (now); break;
            case Control::cancel:     pressCancel (now); break;
            case Control::remain:     pressRemain (now); break;
            case Control::tapTempo:   pressTap (now); break;

            case Control::gate: case Control::loop: case Control::reverse: case Control::lofi:
                toggleMode (c, now);
                break;

            case Control::stereo:     stereo = ! stereo; break;
            case Control::mic:        mic = ! mic; break;

            case Control::startEndLevel: toggleEdit (Edit::startEndLevel, now); break;
            case Control::timeBpm:       toggleEdit (Edit::timeBpm, now); break;
            case Control::length:        if (patternMode) toggleEdit (Edit::length, now); break;
            case Control::quantize:      if (patternMode) toggleEdit (Edit::quantize, now); break;

            case Control::patternSelect:
                patternMode = ! patternMode;
                tempDisplay.reset();
                endOp();
                if (edit == Edit::length || edit == Edit::quantize)
                    edit = Edit::none;
                break;

            case Control::hold:
                holdHeld = true;
                if (op.op == Op::none)
                    sendEngine (Command::Type::holdDown);
                break;

            case Control::subPad:
                subHeld = true;
                if (op.op == Op::none && ! patternMode)
                    sendEngine (Command::Type::subDown);
                break;

            case Control::resample:
            case Control::extSource:
                flash ("---", now, 1.0);   // Phase 4
                break;

            default: break;
        }
    }

    void PanelModel::release (Control c, double now)
    {
        juce::ignoreUnused (now);
        switch (c)
        {
            case Control::func:    funcHeld = false; return;
            case Control::remain:  remainHeld = false; remainShowing = false; return;
            case Control::cancel:  cancelHeld = false; return;
            case Control::mark:    markHeld = false; return;
            case Control::tapTempo: tapHeld = false; return;
            case Control::hold:    holdHeld = false; return;
            case Control::subPad:
                if (subHeld && ! patternMode)
                    sendEngine (Command::Type::subUp);
                subHeld = false;
                return;
            default: break;
        }

        if (isPad (c))          releasePad (padIndex (c));
        if (isEffectButton (c)) releaseEffect (effectButtonIndex (c));
    }

    void PanelModel::selectBank (int b, double now)
    {
        lastActivity = now;
        if (sleeping || paused)
            return;
        bankIndex = juce::jlimit (0, controller.project().numBanks - 1, b);
        if (op.op == Op::delAll || op.op == Op::format)
            op.stage = 1;
    }

    // ---- pads -------------------------------------------------------------------------

    void PanelModel::pressPad (int index, double now)
    {
        const PadId pad = padAt (index);

        if (funcHeld && op.op == Op::none)
        {
            openFunc (index, now);
            return;
        }
        if (op.op != Op::none)
        {
            opPad (pad, now);
            return;
        }
        if (mfxHeld)
        {
            // MFX + pad = MFX 1..12; MFX + DJFX LOOPER + pad = MFX 13..24 (p. 13).
            fx.mfxType = index + 1 + (djfxHeldForMfx ? 12 : 0);
            mfxUsedAsModifier = true;
            flash (DisplayText::number (fx.mfxType), now);
            return;
        }
        if (remainHeld)
        {
            // REMAIN + pad: current pad without playing it, and toggle it as an effect target.
            current = pad;
            fx.targets.flip ((size_t) pad.index());
            fx.targetAll = false;
            return;
        }
        if (patternMode)
            return;   // pattern playback arrives in Phase 5

        if (markHeld && ! markSession && hasSample (pad))
        {
            // Hold MARK + pad: adjust only the end point (p. 28).
            markSession.emplace (edit::MarkSession::Mode::endOnly, *controller.settings (pad),
                                 controller.project().slot (pad).sample->length(), kEngineRate);
            markPad = pad;
        }

        current = pad;
        if (edit == Edit::startEndLevel)
            beginFineAdjust();
        if (edit == Edit::timeBpm && hasSample (pad))
            bpmAtEntry = controller.settings (pad)->bpm;

        sendEngine (Command::Type::padDown, pad);
        padsDownInEngine.set ((size_t) index);
    }

    void PanelModel::releasePad (int index)
    {
        if (padsDownInEngine.test ((size_t) index))
        {
            sendEngine (Command::Type::padUp, padAt (index));
            padsDownInEngine.reset ((size_t) index);
        }
    }

    // ---- effects (panel state only until Phase 3) -------------------------------------

    void PanelModel::pressEffect (int index, double now)
    {
        if (op.op != Op::none)
            return;

        if (index == 5)   // MFX doubles as a modifier: toggle on release unless used as one
        {
            mfxHeld = true;
            mfxUsedAsModifier = false;
            return;
        }
        if (index == 4 && mfxHeld)
        {
            djfxHeldForMfx = true;
            mfxUsedAsModifier = true;
            return;
        }
        if (remainHeld)
        {
            // REMAIN + effect button: apply to all samples (p. 13).
            fx.targetAll = true;
            fx.active = index;
            if (fx.preset) { for (int k = 0; k < 3; ++k) fx.params[(size_t) index][(size_t) k] = (*fx.preset)[(size_t) k]; fx.preset.reset(); }
            return;
        }
        if (tapHeld)
        {
            // EFFECT GRAB: on only while the effect button is held (p. 13).
            fx.grabbing = true;
            fx.grabRestore = fx.active;
            fx.active = index;
            return;
        }

        if (fx.active == index)
        {
            fx.lastActive = index;
            fx.active = -1;
        }
        else
        {
            fx.active = index;
            if (fx.preset)
            {
                for (int k = 0; k < 3; ++k)
                    fx.params[(size_t) index][(size_t) k] = (*fx.preset)[(size_t) k];
                fx.preset.reset();
            }
        }
        juce::ignoreUnused (now);
    }

    void PanelModel::releaseEffect (int index)
    {
        if (index == 5)
        {
            if (mfxHeld && ! mfxUsedAsModifier && op.op == Op::none && ! paused)
            {
                const int mfxSlot = 5;
                if (fx.grabbing) { fx.active = mfxSlot; }
                else if (fx.active == mfxSlot) { fx.lastActive = mfxSlot; fx.active = -1; }
                else fx.active = mfxSlot;
            }
            mfxHeld = false;
            djfxHeldForMfx = false;
        }
        if (index == 4)
            djfxHeldForMfx = false;

        if (fx.grabbing && fx.active == index)
        {
            fx.lastActive = index;
            fx.active = fx.grabRestore;
            fx.grabbing = false;
        }
    }

    // ---- banks ------------------------------------------------------------------------

    void PanelModel::pressBank (int i, double now)
    {
        // Press once: A..E (lit). Press again: F..J (blinking). (p. 11)
        const int banks = controller.project().numBanks;
        int next = i;
        if (bankIndex == i && i + 5 < banks) next = i + 5;
        else if (bankIndex == i + 5)         next = i;
        selectBank (next, now);
    }

    // ---- mode buttons -----------------------------------------------------------------

    void PanelModel::toggleMode (Control c, double now)
    {
        if (op.op != Op::none || ! hasSample (current))
            return;

        const auto status = controller.editSettings (current, [c] (PadSettings& s)
        {
            switch (c)
            {
                case Control::gate:    s.gate = ! s.gate; break;
                case Control::loop:    s.loop = ! s.loop; break;
                case Control::reverse: s.reverse = ! s.reverse; break;
                case Control::lofi:    s.lofi = ! s.lofi; break;
                default: break;
            }
        });
        reportStatus (status, now);
    }

    void PanelModel::toggleEdit (Edit e, double now)
    {
        if (op.op != Op::none)
            return;
        edit = (edit == e) ? Edit::none : e;
        knobMovedInEdit = { false, false, false };
        tempDisplay.reset();   // a mode change shows the new mode's value at once

        if (edit == Edit::startEndLevel)
            beginFineAdjust();
        else
            fineAdjust.reset();

        if (edit == Edit::timeBpm)
            bpmAtEntry = hasSample (current) ? controller.settings (current)->bpm : 0.0;

        juce::ignoreUnused (now);
    }

    void PanelModel::beginFineAdjust()
    {
        fineAdjust.reset();
        knobMovedInEdit = { false, false, false };
        if (hasSample (current))
            fineAdjust.emplace (*controller.settings (current), controller.project().slot (current).sample->length(), kEngineRate);
    }

    // ---- DEL / REC / MARK / CANCEL / REMAIN / TAP -------------------------------------

    void PanelModel::pressDel (double now)
    {
        if (cancelHeld && op.op == Op::none)
        {
            // CANCEL + DEL: delete all samples (or patterns) of a bank (p. 30, 34).
            if (patternMode) { flash ("ENP", now); return; }   // patterns: Phase 5
            op = {};
            op.op = Op::delAll;
            return;
        }

        switch (op.op)
        {
            case Op::none:
                if (patternMode) { flash ("ENP", now); return; }
                op = {};
                op.op = Op::del;
                return;

            case Op::del:
            {
                bool any = false;
                for (int i = 0; i < kMaxPads; ++i)
                {
                    if (! op.selected.test ((size_t) i))
                        continue;
                    any = true;
                    reportStatus (controller.deleteSample (PadId::fromIndex (i)), now);
                }
                if (any)
                    busy (now);
                endOp();
                return;
            }

            case Op::truncate:
                reportStatus (controller.truncate (current), now);
                busy (now);
                endOp();
                return;

            case Op::delAll:
            case Op::format:
            {
                if (op.stage == 0)
                    return;   // choose a bank first
                const int first = op.op == Op::format ? 0 : bankIndex;
                const int last  = op.op == Op::format ? controller.project().numBanks - 1 : bankIndex;
                bool blocked = false;
                for (int b = first; b <= last; ++b)
                {
                    if (bankProtected (b)) { blocked = true; continue; }
                    for (int p = 0; p < kPadsPerBank; ++p)
                        if (hasSample ({ b, p }))
                            controller.deleteSample ({ b, p });
                }
                if (blocked && op.op == Op::delAll)
                    flash ("Prt", now);
                busy (now);
                endOp();
                return;
            }

            case Op::backupLoad:
                flash ("ENP", now);   // backups arrive with projects (Phase 7)
                endOp();
                return;

            default:
                return;
        }
    }

    void PanelModel::pressRec (double now)
    {
        switch (op.op)
        {
            case Op::copy:
            case Op::exchange:
                if (op.stage == 2)
                {
                    const auto s = op.op == Op::copy ? controller.copySample (op.a, op.b)
                                                     : controller.exchangeSamples (op.a, op.b);
                    reportStatus (s, now);
                    busy (now);
                    endOp();
                }
                return;

            case Op::import:
                if (op.stage == 1)
                {
                    if (callbacks.requestImport)
                        callbacks.requestImport (op.a);
                    endOp();
                }
                return;

            case Op::funcSetting:
                applyFuncSetting();
                endOp();
                return;

            case Op::protect:
            {
                auto& prot = patternMode ? controller.project().patternBankProtected : controller.project().sampleBankProtected;
                if (bankIndex < (int) prot.size())
                    prot[(size_t) bankIndex] = op.protectOn;
                endOp();
                return;
            }

            case Op::none:
                flash ("---", now, 1.0);   // sampling (Phase 4) and pattern recording (Phase 5)
                return;

            default:
                return;
        }
    }

    void PanelModel::pressMark (double now)
    {
        markHeld = true;

        // DEL then MARK on a marked pad: truncate (p. 29).
        if (op.op == Op::del && op.selected.none())
        {
            if (hasSample (current) && controller.settings (current)->isMarked (controller.project().slot (current).sample->length()))
            {
                op.op = Op::truncate;
                return;
            }
            flash ("---", now, 1.0);
            return;
        }
        if (op.op != Op::none || patternMode)
            return;

        if (markSession)
        {
            if (auto result = markSession->markPressed (engine.playPosition (markPad)))
                finishMark (*result, now);
            return;
        }

        if (! hasSample (current) || ! isPlaying (current))
            return;

        const auto& slot = controller.project().slot (current);
        if (slot.settings.isMarked (slot.sample->length()))
        {
            // Pressing the lit MARK during playback clears the region (p. 27).
            controller.editSettings (current, [len = slot.sample->length()] (PadSettings& s) { edit::setRegion (s, 0, len, len); });
            return;
        }

        markSession.emplace (edit::MarkSession::Mode::startAndEnd, slot.settings, slot.sample->length(), kEngineRate);
        markPad = current;
        markSession->markPressed (engine.playPosition (current));
    }

    void PanelModel::finishMark (const PadSettings& s, double now)
    {
        const auto pad = markPad;
        reportStatus (controller.editSettings (pad, [&s] (PadSettings& target)
        {
            target.start = s.start;
            target.end = s.end;
            target.bpm = s.bpm;
        }), now);
        markSession.reset();
        markPad = kNoPad;
    }

    void PanelModel::pressCancel (double now)
    {
        cancelHeld = true;

        if (funcHeld && op.op == Op::none)
        {
            op = {};
            op.op = Op::protect;
            op.protectOn = false;
            return;
        }

        if (cancelDetector.press (now))
        {
            sendEngine (Command::Type::stopAll);
            flash ("---", now, 0.5);
        }

        if (op.op != Op::none)
            endOp();
        if (markSession)
        {
            markSession.reset();
            markPad = kNoPad;
        }
    }

    void PanelModel::pressRemain (double now)
    {
        if (funcHeld && op.op == Op::none)
        {
            op = {};
            op.op = Op::protect;
            op.protectOn = true;
            return;
        }
        if (cancelHeld && op.op == Op::none)
        {
            op = {};
            op.op = Op::format;
            return;
        }

        remainHeld = true;
        remainShowing = true;
        juce::ignoreUnused (now);
    }

    void PanelModel::pressTap (double now)
    {
        tapHeld = true;

        if (funcHeld)
        {
            paused = true;
            sendEngine (Command::Type::pause);
            return;
        }

        // With every effect off, TAP reminds you of the last effect (p. 13).
        if (fx.active < 0 && fx.lastActive >= 0)
            lastEffectBlinkUntil = now + 2.0;

        if (numTaps > 0 && now - taps[(size_t) ((numTaps - 1) % 4)] > kTapResetSeconds)
            numTaps = 0;
        taps[(size_t) (numTaps % 4)] = now;
        ++numTaps;
        if (numTaps < 2)
            return;

        const int n = std::min (numTaps, 4);
        const double first = taps[(size_t) ((numTaps - n) % 4)];
        const double bpmRaw = 60.0 * (n - 1) / (now - first);
        const double bpm = controller.limits().steppedTempo ? (double) tempo::snap (bpmRaw)
                                                            : std::clamp (std::round (bpmRaw * 10.0) / 10.0, 40.0, 200.0);

        if (markSession)
            markSession->setSnapBpm (bpm);
        else if (edit == Edit::timeBpm && ! patternMode && hasSample (current))
            controller.editSettings (current, [bpm] (PadSettings& s) { s.bpm = bpm; });
        else
            controller.setPatternTempo (bpm);

        flash (bpmDisplay (bpm), now);
    }

    // ---- FUNC menus -------------------------------------------------------------------

    int PanelModel::funcOptionCount (FuncSetting s) const
    {
        switch (s)
        {
            case FuncSetting::autoTrig:     return 10;
            case FuncSetting::illumination: return (int) kIllumination.size();
            case FuncSetting::effectLimit:  return 2;
            case FuncSetting::midiChannel:  return 16;
            case FuncSetting::midiSync:     return (int) kMidiSync.size();
            case FuncSetting::inputGain:    return (int) kInputGain.size();
            case FuncSetting::outputGain:   return (int) kOutputGain.size();
        }
        return 1;
    }

    void PanelModel::openFunc (int pad, double now)
    {
        const auto& dev = controller.project().device;
        op = {};

        switch (pad)
        {
            case 0: op.op = patternMode ? Op::none : Op::copy; break;
            case 1: op.op = patternMode ? Op::none : Op::exchange; break;
            case 2: op.op = patternMode ? Op::none : Op::import; break;
            case 3: op.op = Op::funcSetting; op.setting = FuncSetting::autoTrig;     op.value = dev.autoTrigLevel; break;
            case 4: op.op = Op::backupLoad; break;
            case 5: op.op = Op::backupSave; break;
            case 6: op.op = Op::funcSetting; op.setting = FuncSetting::illumination; op.value = dev.illumination; break;
            case 7: op.op = Op::funcSetting; op.setting = FuncSetting::effectLimit;  op.value = dev.effectLimit ? 1 : 0; break;
            case 8: op.op = Op::funcSetting; op.setting = FuncSetting::midiChannel;  op.value = dev.midiChannel - 1; break;
            case 9: op.op = Op::funcSetting; op.setting = FuncSetting::midiSync;     op.value = dev.midiSync; break;
            case 10:
                op.op = Op::funcSetting; op.setting = FuncSetting::inputGain;
                op.value = dev.inputGainDb <= -10 ? 0 : 1;
                break;
            case 11:
            {
                op.op = Op::funcSetting; op.setting = FuncSetting::outputGain;
                const auto it = std::find (kOutputGain.begin(), kOutputGain.end(), dev.outputGainDb);
                op.value = it == kOutputGain.end() ? 1 : (int) (it - kOutputGain.begin());
                break;
            }
            default: break;
        }

        if (op.op == Op::none)
            flash ("ENP", now);   // pattern copy/exchange arrive in Phase 5
        if (op.op == Op::import && bankProtected (bankIndex))
            flash ("Prt", now);
    }

    void PanelModel::opPad (PadId pad, double now)
    {
        switch (op.op)
        {
            case Op::copy:
            case Op::exchange:
                if (op.stage == 0)
                {
                    if (op.op == Op::copy && ! hasSample (pad))
                        return;   // only pads with a sample are copy sources
                    op.a = pad;
                    op.stage = 1;
                }
                else if (op.stage >= 1)
                {
                    if (pad == op.a)
                        return;
                    op.b = pad;
                    op.stage = 2;   // REC blinks
                }
                return;

            case Op::import:
                if (bankProtected (pad.bank)) { flash ("Prt", now); return; }
                op.a = pad;
                op.stage = 1;
                return;

            case Op::del:
                if (bankProtected (pad.bank)) { flash ("Prt", now); return; }
                if (hasSample (pad))
                    op.selected.flip ((size_t) pad.index());
                return;

            default:
                return;
        }
    }

    void PanelModel::applyFuncSetting()
    {
        auto& dev = controller.project().device;
        const int v = op.value;
        switch (op.setting)
        {
            case FuncSetting::autoTrig:     dev.autoTrigLevel = v; break;
            case FuncSetting::illumination: dev.illumination = v; break;
            case FuncSetting::effectLimit:  dev.effectLimit = v == 1; break;
            case FuncSetting::midiChannel:  dev.midiChannel = v + 1; break;
            case FuncSetting::midiSync:     dev.midiSync = v; break;
            case FuncSetting::inputGain:    dev.inputGainDb = kInputGain[(size_t) v]; break;
            case FuncSetting::outputGain:   controller.setOutputGainDb (kOutputGain[(size_t) v]); break;
        }
    }

    DisplayText PanelModel::funcDisplay() const
    {
        const int v = op.value;
        switch (op.setting)
        {
            case FuncSetting::autoTrig:     return DisplayText::make ("-" + std::to_string (v) + "-");
            case FuncSetting::illumination: return DisplayText::make (kIllumination[(size_t) v]);
            case FuncSetting::effectLimit:  return DisplayText::make (v == 1 ? "L.on" : "L.oF");
            case FuncSetting::midiChannel:  return DisplayText::number (v + 1);
            case FuncSetting::midiSync:     return DisplayText::make (kMidiSync[(size_t) v]);
            case FuncSetting::inputGain:    return DisplayText::number (kInputGain[(size_t) v]);
            case FuncSetting::outputGain:   return DisplayText::number (kOutputGain[(size_t) v]);
        }
        return {};
    }

    void PanelModel::endOp()
    {
        op = {};
    }

    // ---- knobs ------------------------------------------------------------------------

    void PanelModel::turn (Knob k, double v, double now)
    {
        v = juce::jlimit (0.0, 1.0, v);
        knobs[(size_t) k] = v;
        lastActivity = now;

        if (k == Knob::volume)
        {
            if (callbacks.setVolume) callbacks.setVolume (v);
            else controller.setVolume (v);
            return;
        }
        if (k == Knob::micLevel || sleeping || paused)
            return;

        if (op.op == Op::funcSetting)
        {
            if (k == Knob::ctrl3)
            {
                op.value = knobToIndex (v, funcOptionCount (op.setting));
                op.changed = true;
            }
            return;
        }
        if (op.op != Op::none)
            return;

        if (edit != Edit::none)
        {
            turnInEdit (k, v, now);
            return;
        }

        const int knobIdx = (int) k;   // ctrl1..3
        const int value127 = (int) std::lround (v * 127.0);

        if (mfxHeld)
        {
            mfxUsedAsModifier = true;
            if (k == Knob::ctrl3)
            {
                fx.mfxType = 1 + knobToIndex (v, 24);
                flash (DisplayText::number (fx.mfxType), now);
            }
            else if (k == Knob::ctrl1 && (fx.mfxType == 4 || fx.mfxType == 5))
            {
                // FLANGER / PHASER hidden balance (p. 44).
                fx.params[(size_t) (5 + fx.mfxType - 1)][3] = (float) v;
                flash (DisplayText::number (value127), now);
            }
            return;
        }

        if (remainHeld)
        {
            if (! fx.preset) fx.preset = std::array<float, 3> { 0.5f, 0.5f, 0.5f };
            (*fx.preset)[(size_t) knobIdx] = (float) v;
            flash (DisplayText::number (value127), now);
            return;
        }

        if (fx.active >= 0)
        {
            const int table = fx.active == 5 ? 5 + fx.mfxType - 1 : fx.active;
            fx.params[(size_t) table][(size_t) knobIdx] = (float) v;
            flash (DisplayText::number (value127), now);
        }
    }

    void PanelModel::turnInEdit (Knob k, double v, double now)
    {
        const int idx = (int) k;
        if (idx < 3)
            knobMovedInEdit[(size_t) idx] = true;

        switch (edit)
        {
            case Edit::startEndLevel:
            {
                if (patternMode)
                {
                    if (k == Knob::ctrl3)
                    {
                        seq.metronomeLevel = (int) std::lround (v * 127.0);
                        flash (DisplayText::number (seq.metronomeLevel), now);
                    }
                    return;
                }
                if (! hasSample (current))
                    return;

                if (k == Knob::ctrl3)
                {
                    const int level = (int) std::lround (v * 127.0);
                    reportStatus (controller.editSettings (current, [level] (PadSettings& s) { s.level = level; }), now);
                    flash (DisplayText::number (level), now);
                    return;
                }

                if (! fineAdjust)
                    return;
                // Offsets are relative to the knob centre, and only once a knob has moved
                // in this mode (a knob left off-centre does not jump the point on entry).
                const double off1 = knobMovedInEdit[0] ? (knobs[0] - 0.5) * 2.0 : 0.0;
                const double off2 = knobMovedInEdit[1] ? (knobs[1] - 0.5) * 2.0 : 0.0;
                const auto adjusted = fineAdjust->apply (std::abs (off1) < 0.01 ? 0.0 : off1,
                                                         std::abs (off2) < 0.01 ? 0.0 : off2);
                reportStatus (controller.editSettings (current, [&adjusted] (PadSettings& s)
                {
                    s.start = adjusted.start;
                    s.end = adjusted.end;
                    s.bpm = adjusted.bpm;
                }), now);
                // Offset shown in tens of milliseconds, -13..13 [SPEC U30, assumed].
                const double off = k == Knob::ctrl1 ? off1 : off2;
                flash (DisplayText::number ((int) std::lround (off * 13.0)), now);
                return;
            }

            case Edit::timeBpm:
                if (k == Knob::ctrl2)
                {
                    if (patternMode || ! hasSample (current))
                    {
                        const double bpm = controller.limits().steppedTempo
                                               ? (double) tempo::fromIndex (knobToIndex (v, 127))
                                               : std::round ((40.0 + v * 160.0) * 10.0) / 10.0;
                        controller.setPatternTempo (bpm);
                        flash (bpmDisplay (bpm), now);
                    }
                    else
                    {
                        // Left halves, right doubles the BPM shown on entry (p. 30).
                        double bpm = bpmAtEntry;
                        if (v < 1.0 / 3.0)      bpm = edit::halveBpm (bpmAtEntry);
                        else if (v > 2.0 / 3.0) bpm = edit::doubleBpm (bpmAtEntry);
                        reportStatus (controller.editSettings (current, [bpm] (PadSettings& s) { s.bpm = bpm; }), now);
                        flash (bpmDisplay (bpm), now);
                    }
                }
                else if (k == Knob::ctrl1 && ! patternMode && hasSample (current))
                {
                    const int knobValue = (int) std::lround (v * 127.0);
                    PadSettings after;
                    reportStatus (controller.editSettings (current, [knobValue, &after] (PadSettings& s)
                    {
                        timemodify::applyKnob (s, knobValue);
                        after = s;
                    }), now);
                    if (after.timeModify == TimeModifyMode::off)          flash ("oFF", now);
                    else if (after.timeModify == TimeModifyMode::pattern) flash ("Ptn", now);
                    else                                                  flash (bpmDisplay (after.timeModifyBpm), now);
                }
                return;

            case Edit::length:
                if (k == Knob::ctrl3)
                {
                    const auto lengths = patternLengths();
                    seq.lengthBars = lengths[(size_t) knobToIndex (v, (int) lengths.size())];
                    flash (DisplayText::number (seq.lengthBars), now);
                }
                return;

            case Edit::quantize:
                if (k == Knob::ctrl3)
                {
                    seq.quantizeIndex = knobToIndex (v, 10);
                    flash (quantizeNames()[(size_t) seq.quantizeIndex], now);
                }
                else if (k == Knob::ctrl2 && seq.quantizeIndex <= 1)
                {
                    seq.shuffle = 50 + (int) std::lround (v * 25.0);   // range assumed (SPEC U13)
                    flash (DisplayText::number (seq.shuffle), now);
                }
                return;

            case Edit::none:
                return;
        }
    }

    // ---- time -------------------------------------------------------------------------

    void PanelModel::tick (double now)
    {
        if (tempDisplay && now >= tempUntil)
            tempDisplay.reset();

        // Start-only MARK completes when the pad stops (p. 28).
        if (markSession && markPad.isValid() && ! isPlaying (markPad))
        {
            if (auto result = markSession->padStopped())
                finishMark (*result, now);
            else
            {
                markSession.reset();
                markPad = kNoPad;
            }
        }

        // SLP: sleep after about five minutes without operation (p. 40).
        if (! sleeping && controller.project().device.illumination == 4 && now - lastActivity > kSleepSeconds)
        {
            sleeping = true;
            sendEngine (Command::Type::pause);   // "No sound will be produced while in Sleep mode"
        }
    }

    // ---- view -------------------------------------------------------------------------

    DisplayText PanelModel::idleDisplay() const
    {
        switch (op.op)
        {
            case Op::copy:        return DisplayText::make ("CPY");
            case Op::exchange:    return DisplayText::make ("CHG");
            case Op::import:      return DisplayText::make ("iNP");
            case Op::funcSetting: return funcDisplay();
            case Op::del:         return DisplayText::make ("dEL");
            case Op::delAll:      return DisplayText::make ("dAL");
            case Op::truncate:    return DisplayText::make ("trC");
            case Op::protect:     return DisplayText::make (op.protectOn ? "P.on" : "P.oF");
            case Op::format:      return DisplayText::make ("FNt");
            case Op::backupLoad:  return DisplayText::make ("Lod");
            case Op::backupSave:  return DisplayText::make ("SAU");
            case Op::none:        break;
        }

        switch (edit)
        {
            case Edit::startEndLevel:
                if (patternMode) return DisplayText::number (seq.metronomeLevel);
                return hasSample (current) ? DisplayText::number (controller.settings (current)->level) : DisplayText::make ("---");
            case Edit::timeBpm:
                return (patternMode || ! hasSample (current)) ? bpmDisplay (controller.project().patternTempo) : sampleBpmDisplay();
            case Edit::length:   return DisplayText::number (seq.lengthBars);
            case Edit::quantize: return DisplayText::make (quantizeNames()[(size_t) seq.quantizeIndex]);
            case Edit::none:     break;
        }

        if (patternMode)
            return DisplayText::make ("Ptn");
        // Idle: the current sample's BPM [SPEC U28, assumed].
        return sampleBpmDisplay();
    }

    PanelView PanelModel::view (double now) const
    {
        PanelView v;
        v.knobs = { (float) knobs[0], (float) knobs[1], (float) knobs[2], (float) knobs[3], (float) knobs[4] };
        v.tempo = controller.project().patternTempo;
        v.illumination = (PanelView::Illumination) juce::jlimit (0, 3, controller.project().device.illumination == 4
                                                                      ? 0 : controller.project().device.illumination);

        if (sleeping)
        {
            v.sleeping = true;
            return v;
        }

        auto led = [&v] (Control c, Led l) { v.leds[(size_t) c] = l; };

        // Display
        if (remainShowing && op.op == Op::none)
        {
            const double bytes = callbacks.freeBytes ? (double) callbacks.freeBytes() : 0.0;
            v.display = remainDisplay (bytes / (kEngineRate * 2.0 * 2.0));   // shown for stereo (p. 25)
        }
        else if (tempDisplay && now < tempUntil)
            v.display = *tempDisplay;
        else
            v.display = idleDisplay();
        v.busyDot = now < busyUntil;

        // Effects
        for (int i = 0; i < 6; ++i)
            if (fx.active == i)
                led ((Control) i, Led::on);
        if (fx.active < 0 && fx.lastActive >= 0 && now < lastEffectBlinkUntil)
            led ((Control) fx.lastActive, Led::blink);

        // Pattern and edit buttons
        led (Control::patternSelect, patternMode ? Led::on : Led::off);
        led (Control::length, edit == Edit::length ? Led::on : Led::off);
        led (Control::quantize, edit == Edit::quantize ? Led::on : Led::off);
        led (Control::startEndLevel, edit == Edit::startEndLevel ? Led::on : Led::off);
        led (Control::timeBpm, edit == Edit::timeBpm ? Led::on : Led::off);
        led (Control::stereo, stereo ? Led::on : Led::off);
        led (Control::mic, mic ? Led::on : Led::off);
        led (Control::func, funcHeld ? Led::on : Led::off);

        // Current pad's SAMPLE MODE and MARK
        if (hasSample (current) && ! patternMode)
        {
            const auto& slot = controller.project().slot (current);
            led (Control::gate, slot.settings.gate ? Led::on : Led::off);
            led (Control::loop, slot.settings.loop ? Led::on : Led::off);
            led (Control::reverse, slot.settings.reverse ? Led::on : Led::off);
            led (Control::lofi, slot.settings.lofi ? Led::on : Led::off);
            led (Control::mark, slot.settings.isMarked (slot.sample->length()) ? Led::on : Led::off);
        }
        if (markSession)
            led (Control::mark, Led::blink);

        // Banks: lit = A..E, blinking = F..J; blinking all while a bank must be chosen.
        const bool chooseBank = (op.op == Op::delAll || op.op == Op::format) && op.stage == 0;
        for (int i = 0; i < 5; ++i)
        {
            Led l = Led::off;
            if (chooseBank)              l = Led::blink;
            else if (bankIndex == i)     l = Led::on;
            else if (bankIndex == i + 5) l = Led::blink;
            led ((Control) ((int) Control::bankAF + i), l);
        }

        // DEL / REC per operation
        switch (op.op)
        {
            case Op::copy:
            case Op::exchange:
                led (Control::rec, op.stage == 2 ? Led::blink : (op.op == Op::exchange ? Led::on : Led::off));
                if (op.op == Op::exchange) led (Control::del, Led::on);
                break;
            case Op::import:      led (Control::rec, op.stage == 1 ? Led::blink : Led::on); break;
            case Op::funcSetting: led (Control::rec, op.changed ? Led::blink : Led::off); break;
            case Op::protect:     led (Control::rec, Led::blink); break;
            case Op::del:         led (Control::del, op.selected.any() ? Led::blink : Led::on); break;
            case Op::truncate:    led (Control::del, Led::blink); break;
            case Op::delAll:      led (Control::del, op.stage == 1 ? Led::blink : Led::on); break;
            case Op::format:      led (Control::del, op.stage == 1 ? Led::blink : Led::off); break;
            case Op::backupLoad:
            case Op::backupSave:  led (Control::rec, Led::off); break;
            case Op::none:        break;
        }

        // Pads
        for (int i = 0; i < kPadsPerBank; ++i)
        {
            const PadId p = padAt (i);
            Led l = Led::off;

            if (remainHeld && op.op == Op::none)
                l = (fx.targetAll || fx.targets.test ((size_t) p.index())) ? Led::on : Led::off;
            else if (op.op == Op::copy || op.op == Op::exchange)
            {
                if (p == op.a || p == op.b)            l = Led::on;
                else if (op.stage == 0 && (op.op == Op::exchange || hasSample (p))) l = Led::blink;
                else if (op.stage == 1)                l = Led::blink;
            }
            else if (op.op == Op::import)
                l = (op.stage == 1 && p == op.a) ? Led::on : (op.stage == 0 ? Led::blink : Led::off);
            else if (op.op == Op::del)
                l = op.selected.test ((size_t) p.index()) ? Led::on : (hasSample (p) ? Led::blink : Led::off);
            else if (op.op != Op::none)
                l = Led::off;
            else if (patternMode)
                l = Led::off;   // pads with patterns blink once patterns exist (Phase 5)
            else if (isPlaying (p))
                l = Led::blink;   // SPEC U1
            else if (hasSample (p))
                l = Led::on;

            led (padControl (i), l);
        }

        led (Control::hold, holdHeld ? Led::on : Led::off);
        led (Control::subPad, subHeld ? Led::on : Led::off);
        return v;
    }
}

JUCE_END_IGNORE_WARNINGS_GCC_LIKE
