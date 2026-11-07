#include <catch2/catch_test_macros.hpp>
#include "TestUtil.h"
#include "core/ui_model/PanelModel.h"

using namespace spsx;
using namespace spsx::test;

namespace
{
    struct Panel
    {
        Engine engine;
        SamplerController controller { engine, Limits::authentic() };
        PanelModel model { engine, controller };
        double now = 1000.0;

        Panel()
        {
            engine.prepare (kEngineRate, 512, Limits::authentic());
            model.setCallbacks ({ {}, [] { return (juce::int64) 44100 * 4 * 11; }, {} });
        }

        void press (Control c)   { model.press (c, now); run (1); }
        void release (Control c) { model.release (c, now); run (1); }
        void tap (Control c)     { press (c); release (c); }
        void turn (Knob k, double v) { model.turn (k, v, now); run (1); }
        void wait (double s)     { now += s; model.tick (now); }

        /** Runs the engine so posted commands apply and play positions update. */
        void run (double ms)
        {
            std::vector<float> l (512), r (512);
            for (int n = (int) (ms * 44.1 / 512.0) + 1; n > 0; --n)
            {
                float* o[2] = { l.data(), r.data() };
                engine.process (nullptr, 0, o, 2, 512);
            }
            controller.tick();
            model.tick (now);
        }

        PanelView view() { return model.view (now); }
        std::string display() { return view().display.toString(); }
        Led led (Control c) { return view().leds[(size_t) c]; }

        void load (PadId p, int length = 44100 * 2, std::function<void (PadSettings&)> edit = {})
        {
            controller.setSample (p, makeConstant (length, 0.1f), "test");
            if (edit) controller.editSettings (p, edit);
            run (1);
        }
    };
}

// ---- display ---------------------------------------------------------------------------

TEST_CASE ("Display text: numbers right-aligned, dots attach to the previous cell", "[panel][display]")
{
    REQUIRE (DisplayText::make ("5").toString() == "  5");
    REQUIRE (DisplayText::make ("120").toString() == "120");
    REQUIRE (DisplayText::make ("S.16").cells == std::array<char, 3> { 'S', '1', '6' });
    REQUIRE (DisplayText::make ("S.16").dots == std::array<bool, 3> { true, false, false });
    REQUIRE (DisplayText::make ("98.5").dots[1]);
    REQUIRE (DisplayText::make ("Ptn").toString() == "Ptn");
    REQUIRE (DisplayText::make ("-3").toString() == " -3");
}

TEST_CASE ("Every display code in the spec can be drawn on the 7-segment font", "[panel][display]")
{
    const char* codes[] = {
        "CoF", "rES", "drU", "FrM", "EFF", "dir", "t32", "t16", "t8t", "t1", "Fdb", "bAL", "Lo", "Mid", "Hi", "LEn",
        "-1.0", "1.0", "oFF", "on", "tiM", "ton", "rAt", "int", "dPt", "MAn", "trM", "PAn", "LEU", "PEt", "oC2", "oC1",
        "SUS", "Att", "rEL", "bit", "FLt", "UHt", "diS", "HUM", "rnG", "noS", "FLU", "tUn", "P01", "P16", "Frq", "SEn",
        "-C", "CC", "Pit", "L-r", "tHr", "Ptn", "dEL", "dAL", "Prt", "CHG", "CPY", "iNP", "ENP", "UnS", "SAU", "Lod",
        "bdL", "FNt", "P.on", "P.oF", "ini", "L.on", "L.oF", "AUt", "tMp", "r b", "b", "r", "SLP", "UAU", "AiF", "AL",
        "ni", "-0-", "rdY", "-4-", "rEC", "rEH", "ErS", "trC", "S.8", "S.16", "4.3", "16.3", "EJC", "LoC", "Err", "FUL",
        "0.11", "10.3", "24H", "---"
    };
    for (auto* c : codes)
    {
        const auto d = DisplayText::make (c);
        for (char ch : d.cells)
        {
            INFO ("code " << c << " char '" << ch << "'");
            REQUIRE (seg::forChar (ch) != seg::kUnknown);
        }
    }
}

TEST_CASE ("REMAIN display formats (SPEC U10)", "[panel][display]")
{
    REQUIRE (PanelModel::remainDisplay (11).toString() == "0.11");
    REQUIRE (PanelModel::remainDisplay (630).toString() == "10.3");
    REQUIRE (PanelModel::remainDisplay (173 * 60).toString() == "173");
    REQUIRE (PanelModel::remainDisplay (24 * 3600).toString() == "24H");
}

// ---- banks, pads, modes ----------------------------------------------------------------

TEST_CASE ("Bank buttons: press once for A..E, again for F..J (blinking)", "[panel][bank]")
{
    Panel p;
    REQUIRE (p.model.bank() == 0);
    REQUIRE (p.led (Control::bankAF) == Led::on);
    p.tap (Control::bankAF);
    REQUIRE (p.model.bank() == 5);
    REQUIRE (p.led (Control::bankAF) == Led::blink);
    p.tap (Control::bankAF);
    REQUIRE (p.model.bank() == 0);
    p.tap (Control::bankCH);
    REQUIRE (p.model.bank() == 2);
    p.model.selectBank (9, p.now);
    REQUIRE (p.led (Control::bankEJ) == Led::blink);
}

TEST_CASE ("Pads play, become current, and light: loaded = lit, playing = blinking", "[panel][pads]")
{
    Panel p;
    p.load ({ 0, 3 });
    REQUIRE (p.led (Control::pad4) == Led::on);
    REQUIRE (p.led (Control::pad5) == Led::off);

    p.press (Control::pad4);
    REQUIRE (p.model.currentPad() == PadId { 0, 3 });
    REQUIRE (p.engine.playPosition ({ 0, 3 }) >= 0);
    REQUIRE (p.led (Control::pad4) == Led::blink);
}

TEST_CASE ("SAMPLE MODE buttons edit the current pad and show its state", "[panel][modes]")
{
    Panel p;
    p.load ({ 0, 0 });
    p.tap (Control::pad1);
    p.tap (Control::gate);
    p.tap (Control::loop);
    REQUIRE (p.controller.settings ({ 0, 0 })->gate);
    REQUIRE (p.controller.settings ({ 0, 0 })->loop);
    REQUIRE (p.led (Control::gate) == Led::on);
    REQUIRE (p.led (Control::reverse) == Led::off);

    p.controller.project().sampleBankProtected[0] = true;
    p.tap (Control::reverse);
    REQUIRE (p.display() == "Prt");
    REQUIRE_FALSE (p.controller.settings ({ 0, 0 })->reverse);
}

// ---- FUNC menus -------------------------------------------------------------------------

TEST_CASE ("FUNC + 12: OUTPUT GAIN, CTRL 3 selects, REC confirms, CANCEL aborts", "[panel][func]")
{
    Panel p;
    p.press (Control::func);
    p.tap (Control::pad12);
    p.release (Control::func);
    REQUIRE (p.display() == "  0");
    REQUIRE (p.led (Control::rec) == Led::off);

    p.turn (Knob::ctrl3, 1.0);
    REQUIRE (p.display() == " 12");
    REQUIRE (p.led (Control::rec) == Led::blink);
    p.tap (Control::cancel);
    REQUIRE (p.controller.project().device.outputGainDb == 0);

    p.press (Control::func);
    p.tap (Control::pad12);
    p.release (Control::func);
    p.turn (Knob::ctrl3, 0.0);
    REQUIRE (p.display() == " -3");
    p.tap (Control::rec);
    REQUIRE (p.controller.project().device.outputGainDb == -3);
}

TEST_CASE ("FUNC + 8: Effect Limit L.on / L.oF", "[panel][func]")
{
    Panel p;
    p.press (Control::func);
    p.tap (Control::pad8);
    p.release (Control::func);
    REQUIRE (p.display() == "L.oF");
    p.turn (Knob::ctrl3, 1.0);
    REQUIRE (p.display() == "L.on");
    p.tap (Control::rec);
    REQUIRE (p.controller.project().device.effectLimit);
}

TEST_CASE ("FUNC + 9 / 10 / 11: MIDI channel, MIDI sync, input gain", "[panel][func]")
{
    Panel p;
    p.press (Control::func); p.tap (Control::pad9); p.release (Control::func);
    p.turn (Knob::ctrl3, 1.0);
    REQUIRE (p.display() == " 16");
    p.tap (Control::rec);
    REQUIRE (p.controller.project().device.midiChannel == 16);

    p.press (Control::func); p.tap (Control::pad10); p.release (Control::func);
    REQUIRE (p.display() == "AUt");
    p.turn (Knob::ctrl3, 0.5);
    REQUIRE (p.display() == "tMp");

    p.tap (Control::cancel);
    p.press (Control::func); p.tap (Control::pad11); p.release (Control::func);
    p.turn (Knob::ctrl3, 0.0);
    REQUIRE (p.display() == "-10");
    p.tap (Control::rec);
    REQUIRE (p.controller.project().device.inputGainDb == -10);
}

TEST_CASE ("FUNC + 7 SLP: sleeps after five idle minutes, any button wakes", "[panel][func][sleep]")
{
    Panel p;
    p.press (Control::func); p.tap (Control::pad7); p.release (Control::func);
    p.turn (Knob::ctrl3, 1.0);
    REQUIRE (p.display() == "SLP");
    p.tap (Control::rec);

    p.wait (299.0);
    REQUIRE_FALSE (p.view().sleeping);
    p.wait (2.0);
    REQUIRE (p.view().sleeping);
    REQUIRE (p.engine.isPaused() == false);   // pause command posted; applied on next block
    p.run (1);
    REQUIRE (p.engine.isPaused());

    p.tap (Control::lofi);   // wakes only
    REQUIRE_FALSE (p.view().sleeping);
    REQUIRE_FALSE (p.engine.isPaused());
}

TEST_CASE ("FUNC + TAP TEMPO pauses; only FUNC + TAP resumes", "[panel][pause]")
{
    Panel p;
    p.load ({ 0, 0 }, 44100 * 4);
    p.tap (Control::pad1);
    p.press (Control::func);
    p.tap (Control::tapTempo);
    p.release (Control::func);
    REQUIRE (p.model.isPaused());
    REQUIRE (p.engine.isPaused());

    p.tap (Control::gate);
    REQUIRE_FALSE (p.controller.settings ({ 0, 0 })->gate);   // ignored while paused

    p.press (Control::func);
    p.tap (Control::tapTempo);
    p.release (Control::func);
    REQUIRE_FALSE (p.model.isPaused());
    REQUIRE_FALSE (p.engine.isPaused());
}

TEST_CASE ("FUNC + REMAIN protects the bank, FUNC + CANCEL unprotects", "[panel][protect]")
{
    Panel p;
    p.model.selectBank (3, p.now);
    p.press (Control::func); p.tap (Control::remain); p.release (Control::func);
    REQUIRE (p.display() == "P.on");
    REQUIRE (p.led (Control::rec) == Led::blink);
    p.tap (Control::rec);
    REQUIRE (p.controller.project().sampleBankProtected[3]);

    p.press (Control::func); p.tap (Control::cancel); p.release (Control::func);
    REQUIRE (p.display() == "P.oF");
    p.tap (Control::rec);
    REQUIRE_FALSE (p.controller.project().sampleBankProtected[3]);
}

TEST_CASE ("FUNC + 3 IMPORT asks for files starting at the chosen pad", "[panel][import]")
{
    Panel p;
    PadId requested = kNoPad;
    p.model.setCallbacks ({ [&requested] (PadId pad) { requested = pad; }, {}, {} });
    p.model.selectBank (1, p.now);
    p.press (Control::func); p.tap (Control::pad3); p.release (Control::func);
    REQUIRE (p.display() == "iNP");
    p.tap (Control::pad5);
    REQUIRE (p.led (Control::rec) == Led::blink);
    p.tap (Control::rec);
    REQUIRE (requested == PadId { 1, 4 });
}

// ---- copy / exchange / delete / truncate ----------------------------------------------

TEST_CASE ("FUNC + 1 COPY: source, destination in another bank, REC", "[panel][copy]")
{
    Panel p;
    p.load ({ 0, 0 });
    p.press (Control::func); p.tap (Control::pad1); p.release (Control::func);
    REQUIRE (p.display() == "CPY");
    p.tap (Control::pad2);   // empty: not a source
    p.tap (Control::pad1);
    p.tap (Control::bankBG);
    p.tap (Control::pad3);
    REQUIRE (p.led (Control::rec) == Led::blink);
    p.tap (Control::rec);
    REQUIRE_FALSE (p.controller.project().slot ({ 1, 2 }).isEmpty());
    REQUIRE (p.controller.project().slot ({ 0, 1 }).isEmpty());
}

TEST_CASE ("FUNC + 2 EXCHANGE swaps two pads", "[panel][exchange]")
{
    Panel p;
    p.load ({ 0, 0 }, 1000);
    p.load ({ 0, 1 }, 2000);
    p.press (Control::func); p.tap (Control::pad2); p.release (Control::func);
    REQUIRE (p.display() == "CHG");
    p.tap (Control::pad1);
    p.tap (Control::pad2);
    p.tap (Control::rec);
    REQUIRE (p.controller.project().slot ({ 0, 0 }).sample->length() == 2000);
    REQUIRE (p.controller.project().slot ({ 0, 1 }).sample->length() == 1000);
}

TEST_CASE ("DEL: select pads, DEL deletes; protected banks show Prt", "[panel][delete]")
{
    Panel p;
    p.load ({ 0, 0 });
    p.load ({ 0, 1 });
    p.load ({ 0, 2 });
    p.tap (Control::del);
    REQUIRE (p.display() == "dEL");
    REQUIRE (p.led (Control::pad1) == Led::blink);
    p.tap (Control::pad1);
    p.tap (Control::pad3);
    REQUIRE (p.led (Control::pad1) == Led::on);
    p.tap (Control::del);
    REQUIRE (p.controller.project().slot ({ 0, 0 }).isEmpty());
    REQUIRE_FALSE (p.controller.project().slot ({ 0, 1 }).isEmpty());
    REQUIRE (p.controller.project().slot ({ 0, 2 }).isEmpty());

    p.controller.project().sampleBankProtected[0] = true;
    p.tap (Control::del);
    p.tap (Control::pad2);
    REQUIRE (p.display() == "Prt");
}

TEST_CASE ("CANCEL + DEL: delete every sample in a chosen bank", "[panel][delete]")
{
    Panel p;
    p.load ({ 2, 0 });
    p.load ({ 2, 11 });
    p.load ({ 3, 0 });
    p.press (Control::cancel);
    p.tap (Control::del);
    p.release (Control::cancel);
    REQUIRE (p.display() == "dAL");
    REQUIRE (p.led (Control::bankAF) == Led::blink);
    p.tap (Control::bankCH);
    p.tap (Control::del);
    REQUIRE (p.controller.project().slot ({ 2, 0 }).isEmpty());
    REQUIRE (p.controller.project().slot ({ 2, 11 }).isEmpty());
    REQUIRE_FALSE (p.controller.project().slot ({ 3, 0 }).isEmpty());
}

TEST_CASE ("Truncate: DEL, MARK (trC), DEL", "[panel][truncate]")
{
    Panel p;
    p.load ({ 0, 0 }, 44100, [] (PadSettings& s) { edit::setRegion (s, 10000, 30000, 44100); });
    p.tap (Control::pad1);
    REQUIRE (p.led (Control::mark) == Led::on);
    p.tap (Control::del);
    p.tap (Control::mark);
    REQUIRE (p.display() == "trC");
    p.tap (Control::del);
    REQUIRE (p.controller.project().slot ({ 0, 0 }).sample->length() == 20000);
    REQUIRE (p.led (Control::mark) == Led::off);
}

// ---- START/END/LEVEL, TIME/BPM, TAP ------------------------------------------------------

TEST_CASE ("START/END/LEVEL: CTRL 3 sets LEVEL, CTRL 1/2 move points from centre", "[panel][edit]")
{
    Panel p;
    p.load ({ 0, 0 }, 44100 * 2);
    p.tap (Control::pad1);
    p.tap (Control::startEndLevel);
    REQUIRE (p.led (Control::startEndLevel) == Led::on);

    p.turn (Knob::ctrl3, 64.0 / 127.0);
    REQUIRE (p.controller.settings ({ 0, 0 })->level == 64);

    p.turn (Knob::ctrl1, 1.0);
    REQUIRE (p.controller.settings ({ 0, 0 })->start == 5733);
    REQUIRE (p.led (Control::mark) == Led::on);
    p.turn (Knob::ctrl1, 0.5);
    REQUIRE (p.controller.settings ({ 0, 0 })->start == 0);   // centre cancels
    p.tap (Control::startEndLevel);
    REQUIRE (p.led (Control::startEndLevel) == Led::off);
}

TEST_CASE ("TIME/BPM: pattern tempo follows the stepped table; sample BPM halves/doubles", "[panel][tempo]")
{
    Panel p;
    p.tap (Control::patternSelect);
    REQUIRE (p.display() == "Ptn");
    p.tap (Control::timeBpm);
    p.turn (Knob::ctrl2, 1.0);
    REQUIRE (p.controller.project().patternTempo == 200.0);
    p.turn (Knob::ctrl2, 0.0);
    REQUIRE (p.controller.project().patternTempo == 40.0);
    p.tap (Control::timeBpm);
    p.tap (Control::patternSelect);

    p.load ({ 0, 0 }, 88200);   // 120 BPM
    p.tap (Control::pad1);
    p.tap (Control::timeBpm);
    REQUIRE (p.display() == "120");
    p.turn (Knob::ctrl2, 0.0);
    REQUIRE (p.controller.settings ({ 0, 0 })->bpm == 60.0);
    p.turn (Knob::ctrl2, 0.5);
    REQUIRE (p.controller.settings ({ 0, 0 })->bpm == 120.0);

    p.turn (Knob::ctrl1, 0.0);
    REQUIRE (p.display() == "oFF");
    p.turn (Knob::ctrl1, 1.0);
    REQUIRE (p.display() == "Ptn");
}

TEST_CASE ("TAP TEMPO at quarter notes sets the pattern tempo", "[panel][tempo]")
{
    Panel p;
    for (int i = 0; i < 4; ++i)
    {
        p.tap (Control::tapTempo);
        p.now += 0.5;
    }
    REQUIRE (p.controller.project().patternTempo == 120.0);
}

TEST_CASE ("CANCEL four times quickly stops all sound", "[panel][cancel]")
{
    Panel p;
    p.load ({ 0, 0 }, 44100 * 4, [] (PadSettings& s) { s.loop = true; });
    p.tap (Control::pad1);
    p.run (20);
    REQUIRE (p.engine.playPosition ({ 0, 0 }) >= 0);
    for (int i = 0; i < 4; ++i)
    {
        p.tap (Control::cancel);
        p.now += 0.1;
    }
    p.run (20);
    REQUIRE (p.engine.playPosition ({ 0, 0 }) == -1);
}

// ---- MARK -------------------------------------------------------------------------------

TEST_CASE ("MARK during playback: first press = start, second press = end", "[panel][mark]")
{
    Panel p;
    p.load ({ 0, 0 }, 44100 * 4);
    p.tap (Control::pad1);
    p.run (200);
    const auto startPos = p.engine.playPosition ({ 0, 0 });
    p.tap (Control::mark);
    REQUIRE (p.led (Control::mark) == Led::blink);
    p.run (1000);
    const auto endPos = p.engine.playPosition ({ 0, 0 });
    p.tap (Control::mark);

    const auto* s = p.controller.settings ({ 0, 0 });
    REQUIRE (std::abs (s->start - startPos) < 1100);
    REQUIRE (std::abs (s->end - endPos) < 1100);
    REQUIRE (p.led (Control::mark) == Led::on);

    // Lit MARK pressed during playback clears the region. (Setting the end at the play
    // head ended the one-shot, so the next press starts it again.)
    p.run (20);
    REQUIRE (p.engine.playPosition ({ 0, 0 }) == -1);
    p.tap (Control::pad1);
    p.run (50);
    p.tap (Control::mark);
    REQUIRE_FALSE (p.controller.settings ({ 0, 0 })->isMarked (44100 * 4));
}

TEST_CASE ("MARK once then stopping the pad sets only the start point", "[panel][mark]")
{
    Panel p;
    p.load ({ 0, 0 }, 44100 * 4);
    p.tap (Control::pad1);
    p.run (300);
    p.tap (Control::mark);
    p.tap (Control::pad1);   // trigger mode: second press stops
    p.run (20);
    p.wait (0.1);
    const auto* s = p.controller.settings ({ 0, 0 });
    REQUIRE (s->start > 10000);
    REQUIRE (s->end == 44100 * 4);
}

TEST_CASE ("Hold MARK + pad, then MARK sets only the end point", "[panel][mark]")
{
    Panel p;
    p.load ({ 0, 0 }, 44100 * 4);
    p.press (Control::mark);
    p.tap (Control::pad1);
    p.release (Control::mark);
    p.run (500);
    p.tap (Control::mark);
    const auto* s = p.controller.settings ({ 0, 0 });
    REQUIRE (s->start == 0);
    REQUIRE (std::abs (s->end - 22050) < 3000);   // + the blocks the taps themselves render
}

// ---- effects panel ------------------------------------------------------------------------

TEST_CASE ("Effect buttons: one at a time, press again to turn off", "[panel][effects]")
{
    Panel p;
    p.tap (Control::delay);
    REQUIRE (p.model.effects().active == 2);
    REQUIRE (p.led (Control::delay) == Led::on);
    p.tap (Control::isolator);
    REQUIRE (p.model.effects().active == 3);
    REQUIRE (p.led (Control::delay) == Led::off);
    p.tap (Control::isolator);
    REQUIRE (p.model.effects().active == -1);

    // With effects off, TAP TEMPO blinks the last one used (p. 13).
    p.tap (Control::tapTempo);
    REQUIRE (p.led (Control::isolator) == Led::blink);
}

TEST_CASE ("MFX type: MFX + pad (1..12), MFX + DJFX + pad (13..24), MFX + CTRL 3", "[panel][effects]")
{
    Panel p;
    p.press (Control::mfx);
    p.tap (Control::pad3);
    p.release (Control::mfx);
    REQUIRE (p.model.effects().mfxType == 3);
    REQUIRE (p.model.effects().active == -1);   // used as a modifier: no toggle

    p.press (Control::mfx);
    p.press (Control::djfxLooper);
    p.tap (Control::pad1);
    p.release (Control::djfxLooper);
    p.release (Control::mfx);
    REQUIRE (p.model.effects().mfxType == 13);

    p.press (Control::mfx);
    p.turn (Knob::ctrl3, 1.0);
    p.release (Control::mfx);
    REQUIRE (p.model.effects().mfxType == 24);

    p.tap (Control::mfx);
    REQUIRE (p.model.effects().active == 5);
}

TEST_CASE ("EFFECT GRAB: hold TAP TEMPO, the effect is on only while its button is held", "[panel][effects]")
{
    Panel p;
    p.press (Control::tapTempo);
    p.press (Control::filterDrive);
    REQUIRE (p.model.effects().active == 0);
    p.release (Control::filterDrive);
    p.release (Control::tapTempo);
    REQUIRE (p.model.effects().active == -1);
}

TEST_CASE ("REMAIN + pads choose effect targets without playing", "[panel][effects]")
{
    Panel p;
    p.load ({ 0, 4 });
    p.press (Control::remain);
    p.tap (Control::pad5);
    p.tap (Control::pad6);
    REQUIRE (p.led (Control::pad5) == Led::on);
    p.release (Control::remain);
    REQUIRE (p.model.currentPad() == PadId { 0, 5 });
    REQUIRE (p.model.effects().targets.test ((size_t) PadId { 0, 4 }.index()));
    REQUIRE (p.engine.playPosition ({ 0, 4 }) == -1);
}

TEST_CASE ("REMAIN shows remaining sampling time while held", "[panel][remain]")
{
    Panel p;
    p.press (Control::remain);
    REQUIRE (p.display() == "0.11");   // callback reports 11 s of stereo 16-bit audio
    p.release (Control::remain);
    REQUIRE (p.display() == "---");    // idle with an empty current pad
}
