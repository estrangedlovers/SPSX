#include "PanelComponent.h"
#include <cmath>

namespace spsx::ui
{
    namespace
    {
        using Shape = PanelLayout::Shape;

        // Palette: charcoal body, light rubber pads, red backlight, red LED display.
        const juce::Colour kBodyTop     { 0xff2b2c30 };
        const juce::Colour kBodyBottom  { 0xff1b1c1f };
        const juce::Colour kLegend      { 0xffd9d9d4 };
        const juce::Colour kLegendDim   { 0xff8d8e8a };
        const juce::Colour kButton      { 0xff3d3e43 };
        const juce::Colour kButtonEdge  { 0xff121214 };
        const juce::Colour kLit         { 0xffff3d24 };
        const juce::Colour kPadRubber   { 0xffc8c8c2 };
        const juce::Colour kSegmentOn   { 0xffff2e18 };
        const juce::Colour kSegmentOff  { 0xff1c0805 };
        const juce::Colour kRingBlue    { 0xff2f7bff };

        juce::Font font (float size, bool bold = true)
        {
            return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
        }

        void drawMultiline (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float size)
        {
            g.setFont (font (size));
            const auto lines = juce::StringArray::fromLines (text);
            const float lh = size * 1.05f;
            float y = r.getCentreY() - lh * (float) lines.size() * 0.5f;
            for (auto& l : lines)
            {
                g.drawText (l, juce::Rectangle<float> (r.getX(), y, r.getWidth(), lh), juce::Justification::centred, false);
                y += lh;
            }
        }

        /** One 7-segment digit as paths, slanted like the hardware's LED. */
        juce::Path segmentPath (int segment, juce::Rectangle<float> box, float t)
        {
            const float x0 = box.getX(), x1 = box.getRight(), y0 = box.getY(), y1 = box.getBottom();
            const float ym = box.getCentreY();
            const float g = t * 0.18f;   // gap between segments
            juce::Path p;

            auto horizontal = [&] (float y)
            {
                const float a = x0 + g, b = x1 - g;
                p.startNewSubPath (a, y);
                p.lineTo (a + t * 0.5f, y - t * 0.5f);
                p.lineTo (b - t * 0.5f, y - t * 0.5f);
                p.lineTo (b, y);
                p.lineTo (b - t * 0.5f, y + t * 0.5f);
                p.lineTo (a + t * 0.5f, y + t * 0.5f);
                p.closeSubPath();
            };
            auto vertical = [&] (float x, float ya, float yb)
            {
                const float a = ya + g, b = yb - g;
                p.startNewSubPath (x, a);
                p.lineTo (x + t * 0.5f, a + t * 0.5f);
                p.lineTo (x + t * 0.5f, b - t * 0.5f);
                p.lineTo (x, b);
                p.lineTo (x - t * 0.5f, b - t * 0.5f);
                p.lineTo (x - t * 0.5f, a + t * 0.5f);
                p.closeSubPath();
            };

            switch (segment)
            {
                case 0: horizontal (y0); break;                 // a
                case 1: vertical (x1, y0, ym); break;           // b
                case 2: vertical (x1, ym, y1); break;           // c
                case 3: horizontal (y1); break;                 // d
                case 4: vertical (x0, ym, y1); break;           // e
                case 5: vertical (x0, y0, ym); break;           // f
                case 6: horizontal (ym); break;                 // g
                default: break;
            }

            // Forward slant around the digit's centre, like the hardware's italic LED.
            p.applyTransform (juce::AffineTransform::shear (-0.09f, 0.0f).translated (ym * 0.09f, 0.0f));
            return p;
        }
    }

    PanelComponent::PanelComponent (PanelModel& m) : model (m)
    {
        setOpaque (true);
        setWantsKeyboardFocus (false);
    }

    double PanelComponent::nowSeconds() const noexcept
    {
        return juce::Time::getMillisecondCounterHiRes() * 0.001;
    }

    void PanelComponent::setKeyLabels (std::map<Control, juce::String> c, std::map<Knob, juce::String> k)
    {
        keyLabels = std::move (c);
        knobKeyLabels = std::move (k);
        repaint();
    }

    void PanelComponent::setShowKeyLabels (bool show)
    {
        showKeys = show;
        repaint();
    }

    bool PanelComponent::ledIsLit (Led l, double now) const noexcept
    {
        switch (l)
        {
            case Led::off:       return false;
            case Led::on:        return true;
            case Led::blink:     return std::fmod (now, 0.4) < 0.2;       // 2.5 Hz
            case Led::blinkFast: return std::fmod (now, 0.16) < 0.08;     // 6 Hz
        }
        return false;
    }

    juce::Colour PanelComponent::ringColour (const PanelView& v, double now) const noexcept
    {
        juce::Colour ring = juce::Colours::transparentBlack;
        if (v.sleeping)
            return ring;
        const bool tempoOn = v.illuminationTempo && std::fmod (now * v.tempo / 60.0, 1.0) < 0.5;
        switch (v.illumination)
        {
            case PanelView::Illumination::off:     break;
            case PanelView::Illumination::red:     ring = kLit; break;
            case PanelView::Illumination::blue:    ring = kRingBlue; break;
            case PanelView::Illumination::redBlue: ring = (v.illuminationTempo && tempoOn) ? kLit : kRingBlue; break;
        }
        if (v.illuminationTempo && v.illumination != PanelView::Illumination::redBlue && ! tempoOn)
            ring = ring.withMultipliedAlpha (0.25f);
        if (v.illuminationOverload)
            ring = kLit;
        return ring;
    }

    juce::Rectangle<int> PanelComponent::screenBounds (juce::Rectangle<float> logical, float margin) const noexcept
    {
        return logical.expanded (margin).transformedBy (transform).getSmallestIntegerContainer().expanded (1);
    }

    void PanelComponent::refresh (double now)
    {
        // Repaint only what visibly changed: blinking pads during playback no longer
        // redraw the whole panel every phase.
        const auto next = model.view (now);
        const auto& L = PanelLayout::get();
        juce::RectangleList<int> dirty;

        std::array<bool, kNumControls> lit {};
        for (int i = 0; i < kNumControls; ++i)
            lit[(size_t) i] = ! next.sleeping && ledIsLit (next.leds[(size_t) i], now);
        for (auto& c : L.controls)
            if (firstRefresh || lit[(size_t) c.control] != shownLit[(size_t) c.control])
                dirty.add (screenBounds (c.bounds, 10.0f));

        for (auto& k : L.knobs)
            if (firstRefresh || ! juce::exactlyEqual (next.knobs[(size_t) k.knob], view.knobs[(size_t) k.knob]))
                dirty.add (screenBounds ({ k.centre.x - k.radius, k.centre.y - k.radius, k.radius * 2, k.radius * 2 }, 6.0f));

        if (firstRefresh || next.peak != view.peak)
            dirty.add (screenBounds ({ L.peakLed.x - 10, L.peakLed.y - 10, 20, 20 }, 2.0f));

        // Display + illumination ring: a key over everything that changes their pixels.
        std::uint32_t key = 0;
        for (size_t d = 0; d < 3; ++d)
            key = key * 131u + (std::uint8_t) next.display.cells[d] * 2u + (next.display.dots[d] ? 1u : 0u);
        key = key * 7u + (next.busyDot && std::fmod (now, 0.3) < 0.15 ? 1u : 0u);
        key = key * 7u + (next.sleeping ? 1u + (std::uint32_t) ((int) std::floor (now / 0.3) % 3) : 0u);
        const auto ring = ringColour (next, now);
        if (firstRefresh || key != shownDisplayKey || ring != shownRing)
            dirty.add (screenBounds ({ L.displayCentre.x - L.displayRadius - 12, L.displayCentre.y - L.displayRadius - 12,
                                       (L.displayRadius + 12) * 2, (L.displayRadius + 12) * 2 }, 2.0f));

        view = next;
        viewTime = now;
        shownLit = lit;
        shownDisplayKey = key;
        shownRing = ring;
        firstRefresh = false;
        for (const auto& r : dirty)
            repaint (r);
    }

    void PanelComponent::resized()
    {
        firstRefresh = true;
        const auto w = (float) getWidth(), h = (float) getHeight();
        scale = std::min (w / PanelLayout::kWidth, h / PanelLayout::kHeight);
        const float ox = (w - PanelLayout::kWidth * scale) * 0.5f;
        const float oy = (h - PanelLayout::kHeight * scale) * 0.5f;
        transform = juce::AffineTransform::scale (scale).translated (ox, oy);

        // Cache static artwork at physical resolution so it stays sharp on Retina.
        const float pixelScale = juce::Component::getApproximateScaleFactorForComponent (this);
        const int iw = juce::jmax (1, juce::roundToInt (w * pixelScale));
        const int ih = juce::jmax (1, juce::roundToInt (h * pixelScale));
        staticLayer = juce::Image (juce::Image::ARGB, iw, ih, true);
        juce::Graphics g (staticLayer);
        g.addTransform (juce::AffineTransform::scale (pixelScale));
        g.addTransform (transform);
        renderStatic (g);
    }

    // ---- static artwork ---------------------------------------------------------------

    void PanelComponent::renderStatic (juce::Graphics& g) const
    {
        const auto& L = PanelLayout::get();
        const juce::Rectangle<float> body (0, 0, PanelLayout::kWidth, PanelLayout::kHeight);

        g.setGradientFill (juce::ColourGradient (kBodyTop, 0, 0, kBodyBottom, 0, PanelLayout::kHeight, false));
        g.fillRoundedRectangle (body, 38.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (body.reduced (3.0f), 36.0f, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (body.reduced (1.0f), 38.0f, 2.0f);

        // Wordmark (our own name only) and rear-panel hints.
        g.setColour (kLegend);
        g.setFont (font (30.0f));
        g.drawText ("SPSX", juce::Rectangle<float> (40, 26, 200, 40), juce::Justification::centredLeft);
        g.setColour (kLegendDim);
        g.setFont (font (13.0f, false));
        g.drawText ("PHRASE SAMPLER", juce::Rectangle<float> (40, 60, 220, 18), juce::Justification::centredLeft);
        g.drawText ("LINE OUT    LINE IN    MIDI IN    PHONES", juce::Rectangle<float> (500, 24, 460, 18), juce::Justification::centredRight);

        // Knob legends and scales.
        for (auto& k : L.knobs)
        {
            g.setColour (kLegend);
            g.setFont (font (k.radius > 40 ? 17.0f : 12.0f));
            g.drawText (k.label, juce::Rectangle<float> (k.centre.x - 110, k.centre.y - k.radius - 44, 220, 22), juce::Justification::centred);
            g.setColour (kLegendDim);
            g.setFont (font (12.5f));
            g.drawText (k.legendBottom, juce::Rectangle<float> (k.centre.x - 120, k.centre.y + k.radius + 14, 240, 18), juce::Justification::centred);

            // tick marks over 300 degrees
            for (int i = 0; i <= 10; ++i)
            {
                const float a = juce::degreesToRadians (-150.0f + 30.0f * (float) i);
                const float r0 = k.radius + 6.0f, r1 = k.radius + (i % 5 == 0 ? 16.0f : 11.0f);
                g.drawLine (k.centre.x + r0 * std::sin (a), k.centre.y - r0 * std::cos (a),
                            k.centre.x + r1 * std::sin (a), k.centre.y - r1 * std::cos (a), 2.0f);
            }
        }

        g.setColour (kLegendDim);
        g.setFont (font (12.0f));
        g.drawText ("PEAK", juce::Rectangle<float> (L.peakLed.x - 40, L.peakLed.y + 12, 80, 16), juce::Justification::centred);

        // Display housing: dark plate behind the effect ring, round display face.
        const auto c = L.displayCentre;
        g.setColour (juce::Colour (0xff111113));
        g.fillEllipse (c.x - L.ringOuter - 10, c.y - L.ringOuter - 10, (L.ringOuter + 10) * 2, (L.ringOuter + 10) * 2);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff222327), c.x, c.y - L.displayRadius,
                                                 juce::Colour (0xff0d0d0f), c.x, c.y + L.displayRadius, false));
        g.fillEllipse (c.x - L.displayRadius, c.y - L.displayRadius, L.displayRadius * 2, L.displayRadius * 2);
        g.setColour (kLegendDim);
        g.setFont (font (14.0f));
        g.drawText ("BPM / VALUE", juce::Rectangle<float> (c.x - 90, L.displayWindow.getBottom() + 12, 180, 20), juce::Justification::centred);

        // MFX list printed beside the MFX button (p. 4 diagram).
        const char* mfx[] = { "REVERB", "TAPE ECHO", "CHORUS", "FLANGER", "PHASER", "TREMOLO/PAN", "DISTORTION", "OVERDRIVE",
                              "FUZZ", "WAH", "OCTAVE", "COMP", "EQUALIZER", "BIT CRASH", "NOISE GEN", "VINYL SIM",
                              "RADIO TUNING", "SLICER+FLG", "RING MOD", "CHROMATIC PS", "PITCH", "C.CANCELER", "SUBSONIC", "BPM LOOPER" };
        g.setFont (font (11.5f));
        for (int i = 0; i < 24; ++i)
        {
            g.setColour (i < 12 ? kLegend : kLegendDim);
            g.drawText (juce::String (i + 1) + "  " + mfx[i], juce::Rectangle<float> (800, 330.0f + (float) i * 17.5f, 190, 16),
                        juce::Justification::centredLeft);
        }

        // Section legends.
        g.setColour (kLegendDim);
        g.setFont (font (13.0f));
        g.drawText ("PATTERN SEQUENCER", juce::Rectangle<float> (60, 800, 420, 18), juce::Justification::centredLeft);
        g.drawText ("SAMPLE EDIT", juce::Rectangle<float> (545, 800, 255, 18), juce::Justification::centredLeft);
        g.drawText ("EFFECT GRAB", juce::Rectangle<float> (830, 780, 150, 16), juce::Justification::centred);
        g.drawText ("SAMPLE MODE", juce::Rectangle<float> (470, 914, 516, 18), juce::Justification::centredLeft);
        g.drawText ("BANK", juce::Rectangle<float> (470, 990, 400, 18), juce::Justification::centredLeft);
        g.drawText ("PAUSE", juce::Rectangle<float> (850, 908, 108, 16), juce::Justification::centred);

        // Pad function legends (FUNC + pad).
        for (auto& cg : L.controls)
        {
            if (cg.shape != Shape::pad)
                continue;
            g.setColour (kLegendDim);
            g.setFont (font (11.0f));
            g.drawText (cg.legend, juce::Rectangle<float> (cg.bounds.getX(), cg.bounds.getBottom() + 2, cg.bounds.getWidth(), 16),
                        juce::Justification::centred);
        }

        // Pad well and side-pad well.
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (juce::Rectangle<float> (46, 1086, 650, 346), 16.0f);
        g.fillRoundedRectangle (juce::Rectangle<float> (712, 1086, 176, 346), 16.0f);

        // Internal mic grille.
        g.setColour (juce::Colour (0xff0c0c0d));
        for (int r = 0; r < 4; ++r)
            for (int col = 0; col < 3; ++col)
                g.fillEllipse (925.0f + (float) col * 14.0f, 1330.0f + (float) r * 14.0f, 7.0f, 7.0f);
        g.setColour (kLegendDim);
        g.setFont (font (11.0f));
        g.drawText ("MIC", juce::Rectangle<float> (905, 1390, 80, 14), juce::Justification::centred);
    }

    // ---- live elements ----------------------------------------------------------------

    void PanelComponent::drawButton (juce::Graphics& g, const PanelLayout::ControlGeom& c, bool lit, bool isLatched) const
    {
        juce::Path shape;
        if (c.shape == Shape::ringSegment)        shape = c.path;
        else if (c.shape == Shape::roundButton)   shape.addEllipse (c.bounds);
        else                                      shape.addRoundedRectangle (c.bounds, 9.0f);

        const bool down = mouseControl && *mouseControl == c.control;
        const auto base = lit ? kLit : kButton;

        g.setColour (kButtonEdge);
        g.fillPath (shape, juce::AffineTransform::translation (0, 3));
        g.setGradientFill (juce::ColourGradient (base.brighter (down ? 0.0f : 0.18f), 0, c.bounds.getY(),
                                                 base.darker (lit ? 0.25f : 0.35f), 0, c.bounds.getBottom(), false));
        g.fillPath (shape, juce::AffineTransform::translation (0, down ? 2.0f : 0.0f));

        if (lit)
        {
            g.setColour (kLit.withAlpha (0.35f));
            g.strokePath (shape, juce::PathStrokeType (6.0f));
        }
        if (isLatched)
        {
            g.setColour (juce::Colours::yellow.withAlpha (0.8f));
            g.strokePath (shape, juce::PathStrokeType (2.5f));
        }

        g.setColour (lit ? juce::Colours::white : kLegend);
        const float size = c.shape == Shape::ringSegment ? 15.0f : (c.label.length() > 8 ? 13.5f : 15.0f);
        auto r = c.bounds;
        if (c.shape == Shape::ringSegment)
            r = juce::Rectangle<float> (0, 0, 120, 60).withCentre (c.path.getBounds().getCentre());
        drawMultiline (g, c.label, r.translated (0, down ? 2.0f : 0.0f), size);
    }

    void PanelComponent::drawPad (juce::Graphics& g, const PanelLayout::ControlGeom& c, bool lit) const
    {
        const bool down = mouseControl && *mouseControl == c.control;
        auto r = c.bounds.translated (0, down ? 2.5f : 0.0f);

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (c.bounds.translated (0, 4), 10.0f);

        // Rubber: light grey; backlight bleeds red through it when lit.
        auto top = lit ? kPadRubber.interpolatedWith (kLit, 0.72f) : kPadRubber;
        auto bottom = lit ? kLit.darker (0.25f) : kPadRubber.darker (0.28f);
        g.setGradientFill (juce::ColourGradient (top, r.getCentreX(), r.getY(), bottom, r.getCentreX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 10.0f);

        if (lit)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.35f), r.getCentreX(), r.getCentreY(),
                                                     juce::Colours::transparentWhite, r.getX(), r.getY(), true));
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (kLit.withAlpha (0.4f));
            g.drawRoundedRectangle (r.expanded (2.0f), 12.0f, 4.0f);
        }

        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (r.reduced (1.5f), 9.0f, 1.5f);

        g.setColour (lit ? juce::Colours::white : juce::Colour (0xff3a3a38));
        if (c.shape == Shape::pad)
        {
            g.setFont (font (34.0f));
            g.drawText (c.label, r, juce::Justification::centred);
        }
        else
            drawMultiline (g, c.label, r, 18.0f);
    }

    void PanelComponent::drawKnob (juce::Graphics& g, const PanelLayout::KnobGeom& k, float value, bool selected) const
    {
        const auto c = k.centre;
        const float r = k.radius;

        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillEllipse (c.x - r - 2, c.y - r + 3, (r + 2) * 2, (r + 2) * 2);

        // Grippy skirt
        g.setColour (juce::Colour (0xff141415));
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        g.setColour (juce::Colour (0xff2c2c2f));
        for (int i = 0; i < 36; ++i)
        {
            const float a = (float) i * juce::MathConstants<float>::twoPi / 36.0f;
            g.drawLine (c.x + (r - 5) * std::sin (a), c.y - (r - 5) * std::cos (a),
                        c.x + (r - 1) * std::sin (a), c.y - (r - 1) * std::cos (a), 2.2f);
        }

        // Cap
        const float rc = r * 0.78f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4b50), c.x, c.y - rc, juce::Colour (0xff1e1f22), c.x, c.y + rc, false));
        g.fillEllipse (c.x - rc, c.y - rc, rc * 2, rc * 2);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawEllipse (c.x - rc, c.y - rc, rc * 2, rc * 2, 1.5f);

        // Pointer over 300 degrees
        const float a = juce::degreesToRadians (-150.0f + 300.0f * value);
        g.setColour (juce::Colours::white);
        g.drawLine (c.x + rc * 0.25f * std::sin (a), c.y - rc * 0.25f * std::cos (a),
                    c.x + rc * 0.92f * std::sin (a), c.y - rc * 0.92f * std::cos (a), juce::jmax (2.5f, r * 0.07f));

        if (selected)
        {
            g.setColour (juce::Colours::yellow.withAlpha (0.7f));
            g.drawEllipse (c.x - r - 22, c.y - r - 22, (r + 22) * 2, (r + 22) * 2, 2.0f);
        }
    }

    void PanelComponent::drawDisplay (juce::Graphics& g, double now) const
    {
        const auto& L = PanelLayout::get();
        const auto c = L.displayCentre;

        // Illumination ring around the display face.
        const auto ring = ringColour (view, now);
        if (! ring.isTransparent())
        {
            for (int i = 0; i < 4; ++i)
            {
                g.setColour (ring.withMultipliedAlpha (0.12f + 0.2f * (float) i));
                const float rr = L.displayRadius + 6.0f - (float) i * 1.5f;
                g.drawEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2, 10.0f - (float) i * 2.5f);
            }
        }

        // Window
        const auto w = L.displayWindow;
        g.setColour (juce::Colour (0xff050505));
        g.fillRoundedRectangle (w, 8.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), w.getX(), w.getY(),
                                                 juce::Colours::transparentWhite, w.getX(), w.getCentreY(), false));
        g.fillRoundedRectangle (w.withHeight (w.getHeight() * 0.5f), 8.0f);

        const float digitW = w.getWidth() * 0.17f, digitH = w.getHeight() * 0.62f;
        const float pitch = w.getWidth() * 0.29f;
        const float t = digitW * 0.21f;
        const float startX = w.getCentreX() - pitch - digitW * 0.5f;

        for (int d = 0; d < 3; ++d)
        {
            const juce::Rectangle<float> box (startX + (float) d * pitch, w.getCentreY() - digitH * 0.5f, digitW, digitH);
            std::uint8_t bits = 0;
            bool dot = false;
            if (view.sleeping)
                dot = (int) std::floor (now / 0.3) % 3 == d;   // dots chase in Sleep
            else
            {
                bits = seg::forChar (view.display.cells[(size_t) d]);
                if (bits == seg::kUnknown) bits = seg::g;
                dot = view.display.dots[(size_t) d] || (d == 2 && view.busyDot && std::fmod (now, 0.3) < 0.15);
            }

            for (int s = 0; s < 7; ++s)
            {
                const bool on = (bits >> s) & 1;
                auto p = segmentPath (s, box, t);
                if (on)
                {
                    g.setColour (kSegmentOn.withAlpha (0.25f));
                    g.strokePath (p, juce::PathStrokeType (t * 0.7f));
                }
                g.setColour (on ? kSegmentOn : kSegmentOff);
                g.fillPath (p);
            }

            const float dr = t * 0.6f;
            g.setColour (dot ? kSegmentOn : kSegmentOff);
            g.fillEllipse (box.getRight() + t * 0.55f, box.getBottom() - dr, dr * 1.4f, dr * 1.4f);
        }
    }

    void PanelComponent::drawKeyLabels (juce::Graphics& g) const
    {
        const auto& L = PanelLayout::get();
        auto tag = [&g] (juce::Point<float> at, const juce::String& text)
        {
            g.setFont (font (14.0f));
            const float w = juce::jmax (24.0f, (float) text.length() * 9.0f + 10.0f);
            juce::Rectangle<float> r (at.x, at.y, w, 20);
            g.setColour (juce::Colour (0xfff2d03b));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (juce::Colours::black);
            g.drawText (text, r, juce::Justification::centred);
        };

        for (auto& c : L.controls)
            if (auto it = keyLabels.find (c.control); it != keyLabels.end())
                tag (c.bounds.getTopLeft().translated (-4, -10), it->second);
        for (auto& k : L.knobs)
            if (auto it = knobKeyLabels.find (k.knob); it != knobKeyLabels.end())
                tag (k.centre.translated (k.radius * 0.5f, -k.radius - 10), it->second);

        g.setColour (juce::Colours::black.withAlpha (0.75f));
        const juce::Rectangle<float> help (40, 1418, 920, 26);
        g.fillRoundedRectangle (help, 6.0f);
        g.setColour (juce::Colours::white);
        g.setFont (font (13.0f, false));
        g.drawText ("Option = FUNC   Shift+Z..B = banks F..J   Left/Right select knob, Up/Down turn (Shift fine)   "
                    "- = volume   Shift-click a button to hold it   ? closes",
                    help, juce::Justification::centred);
    }

    void PanelComponent::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (0xff101012));
        g.drawImage (staticLayer, getLocalBounds().toFloat());

        const double now = viewTime;
        g.addTransform (transform);
        const auto& L = PanelLayout::get();

        for (auto& c : L.controls)
        {
            const Led led = view.sleeping ? Led::off : view.leds[(size_t) c.control];
            const bool lit = ledIsLit (led, now);
            if (c.shape == Shape::pad || c.shape == Shape::sidePad)
                drawPad (g, c, lit);
            else
                drawButton (g, c, lit, latched.count (c.control) > 0);
        }

        for (auto& k : L.knobs)
            drawKnob (g, k, view.knobs[(size_t) k.knob], selectedKnob && *selectedKnob == k.knob);

        g.setColour (ledIsLit (view.peak, now) ? kLit : juce::Colour (0xff3a0d08));
        g.fillEllipse (L.peakLed.x - 8, L.peakLed.y - 8, 16, 16);

        drawDisplay (g, now);

        if (showKeys)
            drawKeyLabels (g);
    }

    // ---- mouse ------------------------------------------------------------------------

    void PanelComponent::mouseDown (const juce::MouseEvent& e)
    {
        const auto p = toLogical (e.position);
        const auto& L = PanelLayout::get();
        const double now = nowSeconds();

        // MIDI learn: a click picks the knob or button to bind instead of operating it.
        if (learnMode)
        {
            if (auto* k = L.knobAt (p))
                model.armMidiLearn (PanelModel::LearnTarget { true, (int) k->knob });
            else if (auto* c = L.controlAt (p))
                model.armMidiLearn (PanelModel::LearnTarget { false, (int) c->control });
            if (onLearnTargetChanged)
                onLearnTargetChanged();
            return;
        }

        if (auto* k = L.knobAt (p))
        {
            dragKnob = k->knob;
            dragStartValue = model.knob (k->knob);
            dragStartY = e.position.y;
            return;
        }

        if (auto* c = L.controlAt (p))
        {
            // Shift-click latches a button down, so mouse users can do FUNC + pad,
            // REMAIN + pad, MARK + pad and other two-handed combinations.
            if (e.mods.isShiftDown())
            {
                if (latched.erase (c->control) > 0)
                    model.release (c->control, now);
                else
                {
                    latched.insert (c->control);
                    model.press (c->control, now);
                }
                refresh (now);
                return;
            }
            mouseControl = c->control;
            model.press (c->control, now);
            refresh (now);
            repaint();
        }
    }

    void PanelComponent::mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragKnob)
            return;
        // One full sweep per ~250 logical units of vertical travel; Shift for fine.
        const float travel = 250.0f * scale * (e.mods.isShiftDown() ? 8.0f : 1.0f);
        const double v = juce::jlimit (0.0, 1.0, dragStartValue + (dragStartY - e.position.y) / travel);
        if (e.mods.isShiftDown())
        {
            // Re-anchor so switching to fine mid-drag does not jump.
            dragStartValue = v;
            dragStartY = e.position.y;
        }
        model.turn (*dragKnob, v, nowSeconds());
        refresh (nowSeconds());
    }

    void PanelComponent::mouseUp (const juce::MouseEvent&)
    {
        const double now = nowSeconds();
        if (mouseControl)
        {
            model.release (*mouseControl, now);
            mouseControl.reset();
            repaint();
        }
        dragKnob.reset();
        refresh (now);
    }

    void PanelComponent::mouseDoubleClick (const juce::MouseEvent& e)
    {
        // Double-click a knob: back to centre (cancels START/END fine adjust, p. 29).
        if (auto* k = PanelLayout::get().knobAt (toLogical (e.position)))
            model.turn (k->knob, 0.5, nowSeconds());
    }

    void PanelComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        if (auto* k = PanelLayout::get().knobAt (toLogical (e.position)))
        {
            const double step = (w.deltaY + w.deltaX) * (e.mods.isShiftDown() ? 0.03 : 0.15);
            model.turn (k->knob, model.knob (k->knob) + step, nowSeconds());
            refresh (nowSeconds());
        }
    }
}
