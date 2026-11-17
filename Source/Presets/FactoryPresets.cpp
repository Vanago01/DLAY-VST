#include "FactoryPresets.h"

namespace dlay
{

namespace
{
    /** Fluent builder: keeps the preset table compact and readable. */
    struct B
    {
        FactoryPreset p;

        B (const char* name, Cat category, const char* description)
        {
            p.name = name;
            p.category = category;
            p.description = description;
        }

        B& set (const juce::String& paramId, float v)      { p.values.emplace_back (paramId, v); return *this; }

        // engine
        B& mode (Mode m)                                   { return set (id::mode, (float) m); }
        B& ms (float t)                                    { set (id::sync, 0.0f); return set (id::time, t); }
        B& div (int d)                                     { set (id::sync, 1.0f); return set (id::division, (float) d); }
        B& fb (float v)                                    { return set (id::feedback, v); }
        B& mix (float v)                                   { return set (id::mix, v); }
        B& a (float v)                                     { return set (id::algoA, v); }
        B& b (float v)                                     { return set (id::algoB, v); }

        // stereo
        B& stereo (StereoMode s)                           { return set (id::stereoMode, (float) s); }
        B& pp (float v)                                    { return set (id::pingPong, v); }
        B& width (float v)                                 { return set (id::width, v); }
        B& offset (float v)                                { return set (id::offset, v); }
        B& spread (float v)                                { return set (id::spread, v); }

        // character
        B& sat (float amount, SatType t = SatType::tape)   { set (id::satType, (float) t); return set (id::saturation, amount); }
        B& drive (float db)                                { return set (id::drive, db); }
        B& cut (float lowHz, float highHz, float res = 0)  { set (id::lowCut, lowHz); set (id::highCut, highHz); return set (id::resonance, res); }
        B& diff (float v)                                  { return set (id::diffusion, v); }
        B& mod (float depth, float rateHz)                 { set (id::modDepth, depth); return set (id::modRate, rateHz); }
        B& pitch (float st)                                { return set (id::pitch, st); }
        B& rev (float v)                                   { return set (id::reverse, v); }
        B& duck (float v)                                  { return set (id::ducking, v); }
        B& freeze()                                        { return set (id::freeze, 1.0f); }
        B& infinite()                                      { return set (id::infinite, 1.0f); }

        // signature
        B& warp (float v)                                  { return set (id::warp, v); }
        B& inertia (float v)                               { return set (id::inertia, v); }
        B& ghost (float v)                                 { return set (id::ghost, v); }
        B& collapse (float v)                              { return set (id::collapse, v); }
        B& rnd (float v)                                   { return set (id::random, v); }
        B& tapeMem (float v)                               { return set (id::tapeMemory, v); }
        B& out (float db)                                  { return set (id::outGain, db); }

        B& orbit (float amount, float rateHz, OrbitShape shape = OrbitShape::circle)
        {
            set (id::orbit, amount);  set (id::orbitRate, rateHz);  set (id::orbitSync, 0.0f);
            return set (id::orbitShape, (float) shape);
        }

        B& orbitSync (float amount, int division, OrbitShape shape = OrbitShape::circle)
        {
            set (id::orbit, amount);  set (id::orbitDiv, (float) division);  set (id::orbitSync, 1.0f);
            return set (id::orbitShape, (float) shape);
        }

        // MOTION: tempo-synced lane
        B& lane (Lane l, LfoShape shape, float depth, int division, float phaseDeg = 0.0f, float stereoDeg = 0.0f)
        {
            const int i = (int) l;
            set (id::lane (i, id::laneShape), (float) shape);   set (id::lane (i, id::laneDepth), depth);
            set (id::lane (i, id::laneSync), 1.0f);             set (id::lane (i, id::laneDiv), (float) division);
            set (id::lane (i, id::lanePhase), phaseDeg);
            return set (id::lane (i, id::laneStereo), stereoDeg);
        }

        // MOTION: free-running lane
        B& laneHz (Lane l, LfoShape shape, float depth, float rateHz, float phaseDeg = 0.0f, float stereoDeg = 0.0f)
        {
            const int i = (int) l;
            set (id::lane (i, id::laneShape), (float) shape);   set (id::lane (i, id::laneDepth), depth);
            set (id::lane (i, id::laneSync), 0.0f);             set (id::lane (i, id::laneRate), rateHz);
            set (id::lane (i, id::lanePhase), phaseDeg);
            return set (id::lane (i, id::laneStereo), stereoDeg);
        }

        B& curve (Lane l, std::vector<CurvePoint> points)
        {
            p.curves.emplace_back ((int) l, std::move (points));
            return *this;
        }

        operator FactoryPreset() const { return p; }
    };

    using M   = Mode;
    using L   = Lane;
    using S   = LfoShape;
    using O   = OrbitShape;
    using ST  = SatType;
    using SM  = StereoMode;
    using Pts = std::vector<CurvePoint>;        // { x, y, bend }: y is 0..1, 0.5 = no change
    using Presets = std::vector<FactoryPreset>;

    // Notes for whoever tunes these:
    //  - values are in the parameters' own units (%, ms, Hz, dB, semitones, degrees)
    //  - a() / b() are the algorithm's two dedicated controls, see Params/Modes.h
    //  - DIGITAL's a() is COLOR: 50 is pristine, below 50 band-limits, above 50 adds grit
    //  - the speed lane works in octaves (100 % = 1 octave), so small depths go a long way

    // ========================================================================
    void analogTape (Presets& v)
    {
        const Cat c = Cat::analogTape;

        // Preset 0 is what a new instance loads.
        v.push_back (B ("Golden Reel", c, "Warm dotted-eighth tape echo with a gentle stereo sway. Works on everything.")
                        .mode (M::tape).div (Div::n8d).fb (44).mix (28).a (22).b (28).cut (70, 9500).sat (15).pp (25).width (115));

        v.push_back (B ("Radioactive Tape", c, "Overcooked, glowing repeats from a tape that should have been retired decades ago.")
                        .mode (M::tape).div (Div::n4).fb (80).mix (34).a (55).b (85).sat (75, ST::tape).drive (10).tapeMem (60)
                        .cut (120, 4800, 12).pp (40));

        v.push_back (B ("Tape Collapse", c, "Each repeat tumbles faster into the next while the reel slowly loses speed.")
                        .mode (M::tape).div (Div::n4).fb (52).mix (36).a (40).b (45).collapse (78).inertia (72).tapeMem (35).sat (35)
                        .cut (80, 6500).lane (L::speed, S::saw, -18, Div::bar2));

        v.push_back (B ("Melting Memories", c, "Sagging pitch, soft dropouts and smeared repeats, like a tape left on the dashboard.")
                        .mode (M::tape).div (Div::n4d).fb (64).mix (38).a (85).b (62).tapeMem (78).inertia (88).cut (90, 4600).diff (28)
                        .laneHz (L::speed, S::random, 28, 0.13f).width (130));

        v.push_back (B ("Half-Speed Reel", c, "Long, dark half-note repeats from a machine running at half speed.")
                        .mode (M::tape).div (Div::n2).fb (50).mix (30).a (30).b (58).cut (50, 4000).sat (30).inertia (60));

        v.push_back (B ("Varispeed Flutter", c, "Bright eighth notes with a nervous capstan: fast flutter and wide stereo drift.")
                        .mode (M::tape).div (Div::n8).fb (42).mix (30).a (92).b (18).mod (22, 5.6f).cut (100, 11000).spread (60));

        v.push_back (B ("Oxide Shedder", c, "Triplet ping-pong on a tape with half its oxide gone: dull, dusty, full of dropouts.")
                        .mode (M::tape).div (Div::n4t).fb (60).mix (32).a (35).b (96).tapeMem (42).cut (140, 3600, 12).sat (40).pp (100));

        v.push_back (B ("Analog Sunset", c, "Round, slowly wandering quarter notes that glow and melt into each other.")
                        .mode (M::warmAnalog).div (Div::n4).fb (62).mix (32).a (66).b (38).sat (25, ST::tube).pp (35).width (135).mod (12, 0.3f));

        v.push_back (B ("Reel to Reel Ping", c, "Clean, well-kept machine bouncing dotted eighths hard left and right.")
                        .mode (M::tape).div (Div::n8d).fb (56).mix (30).a (16).b (20).pp (100).cut (80, 10500).sat (12));

        v.push_back (B ("Bias Drift", c, "Driven tube front end into tape at a free 333 ms, the right side trailing behind.")
                        .mode (M::tape).ms (333).fb (48).mix (30).a (48).b (36).drive (12).sat (48, ST::tube).cut (60, 7200).offset (18));

        v.push_back (B ("Sound on Sound", c, "One-bar tape loop with high regeneration: layer phrases on top of themselves.")
                        .mode (M::tape).div (Div::bar1).fb (88).mix (40).a (12).b (30).sat (38).cut (120, 6500).inertia (55));

        v.push_back (B ("Velvet Doubler", c, "Automatic double-tracking: one short, drifting tape repeat spread wide.")
                        .mode (M::tape).ms (40).fb (0).mix (45).a (70).b (15).spread (80).width (160).cut (40, 14000));

        v.push_back (B ("Capstan Wobble", c, "Middle head only, worn pinch roller: quarter-note repeats with a seasick sway.")
                        .mode (M::tapeEcho).div (Div::n8).fb (46).mix (30).a (20).b (88).cut (120, 5200).sat (28));

        v.push_back (B ("Tube Tape Lead", c, "The classic lead-line quarter note: forward, slightly driven, tucked under the playing.")
                        .mode (M::tape).div (Div::n4).fb (34).mix (24).a (24).b (32).drive (6).sat (30, ST::tube).cut (120, 7000).duck (35));
    }

    // ========================================================================
    void vintageEchoes (Presets& v)
    {
        const Cat c = Cat::vintageEchoes;

        v.push_back (B ("Bucket Brigade 300", c, "The classic 300 ms analog pedal: dark, compressed, slightly seasick.")
                        .mode (M::bbd).ms (300).fb (50).mix (30).a (62).b (28).mod (14, 0.6f).cut (60, 8000));

        v.push_back (B ("Memory Lane", c, "Dotted-eighth bucket-brigade echo with its chorus switched in.")
                        .mode (M::bbd).div (Div::n8d).fb (56).mix (30).a (74).b (18).mod (38, 0.8f).spread (50));

        v.push_back (B ("Dark Alley Slap", c, "Short, murky analog slap with clock grit in the tail.")
                        .mode (M::bbd).ms (96).fb (26).mix (34).a (28).b (42).cut (90, 5000));

        v.push_back (B ("Space Chorus Echo", c, "Two heads, chorus on, a touch of spring: a well-loved tape echo unit.")
                        .mode (M::tapeEcho).div (Div::n8t).fb (46).mix (30).a (50).b (28).mod (30, 0.7f).diff (12).cut (100, 6000));

        v.push_back (B ("Three Head Monster", c, "All three playback heads at once: a dense, saturated gallop of repeats.")
                        .mode (M::tapeEcho).div (Div::n8).fb (52).mix (32).a (95).b (34).sat (42).cut (90, 5600).pp (20));

        v.push_back (B ("Oil Can Wobble", c, "Murky, warbling repeats from a spinning can of oil.")
                        .mode (M::warmAnalog).ms (185).fb (46).mix (36).a (82).b (64).mod (62, 1.3f).cut (160, 2800, 10));

        v.push_back (B ("Rockabilly Slap", c, "Fifties studio slap: one snappy repeat right behind the note.")
                        .mode (M::slapback).ms (118).fb (14).mix (38).a (62).b (0).sat (22).cut (100, 8000));

        v.push_back (B ("Solid State Echo", c, "Seventies transistor echo box: firm, mid-forward repeats with a little crunch.")
                        .mode (M::warmAnalog).ms (345).fb (48).mix (28).a (40).b (18).sat (32, ST::diode).drive (6).cut (80, 6500));

        v.push_back (B ("Magnetic Drum", c, "Four heads around a spinning drum: a tight cluster of fading repeats.")
                        .mode (M::multiTap).div (Div::n4d).fb (34).mix (28).a (33).b (30).tapeMem (36).sat (28).cut (120, 5200).spread (40));

        v.push_back (B ("Bucket Chorus", c, "A very short bucket-brigade line with deep modulation: chorus that turns into echo.")
                        .mode (M::bbd).ms (27).fb (32).mix (45).a (8).b (22).mod (55, 0.45f).spread (100).width (150));

        v.push_back (B ("Vintage Rack 12-Bit", c, "Early digital rack delay: band-limited, grainy tails, a hint of modulation.")
                        .mode (M::digital).div (Div::n4d).fb (42).mix (28).a (74).b (40).cut (40, 7500).mod (10, 0.35f));

        v.push_back (B ("Dub Station", c, "Head one, feedback up, filter closing in: ready for the siren.")
                        .mode (M::tapeEcho).div (Div::n8d).fb (80).mix (34).a (10).b (40).cut (200, 3000, 12).sat (48).diff (22).pp (30));

        v.push_back (B ("Garage Spring Echo", c, "Analog repeats splashing through a spring tank.")
                        .mode (M::warmAnalog).div (Div::n8).fb (40).mix (30).a (30).b (25).diff (78).cut (180, 4800));

        v.push_back (B ("Sputnik Echo", c, "Fast, nasal, fluttering repeats from the early space age.")
                        .mode (M::bbd).div (Div::n16d).fb (70).mix (30).a (40).b (60).cut (250, 4200, 25).mod (20, 4.5f));
    }

    // ========================================================================
    void modernDigital (Presets& v)
    {
        const Cat c = Cat::modernDigital;

        v.push_back (B ("Crystal Quarter", c, "Pristine quarter notes. Nothing added, nothing taken away.")
                        .mode (M::digital).div (Div::n4).fb (40).mix (26).a (50).b (50));

        v.push_back (B ("Glass Eighths", c, "Bright, thinning eighth notes that float above the mix.")
                        .mode (M::digital).div (Div::n8).fb (55).mix (26).a (50).b (68).cut (120, 15000).width (120));

        v.push_back (B ("Wide Dotted", c, "Clean dotted eighths with the right channel pulled late for instant width.")
                        .mode (M::digital).div (Div::n8d).fb (48).mix (28).a (50).b (50).offset (30).width (155));

        v.push_back (B ("Ducked Half Note", c, "Long, transparent repeats that step aside while you play.")
                        .mode (M::digital).div (Div::n2).fb (38).mix (30).a (50).b (45).duck (55).cut (80, 12000));

        v.push_back (B ("Filtered Sixteenths", c, "A band-passed run of sixteenths with a resonant edge.")
                        .mode (M::digital).div (Div::n16).fb (68).mix (26).a (50).b (50).cut (320, 4200, 30));

        v.push_back (B ("Triplet Sparkle", c, "Airy eighth-note triplets that lean from side to side.")
                        .mode (M::digital).div (Div::n8t).fb (52).mix (26).a (50).b (78).pp (60).cut (200, 18000));

        v.push_back (B ("Mod Delay Deluxe", c, "Studio modulation delay: slow, wide pitch drift on clean triplet quarters.")
                        .mode (M::digital).div (Div::n4t).fb (46).mix (30).a (50).b (50).mod (46, 0.35f).spread (70).cut (60, 11000));

        v.push_back (B ("Clean Whole Bar", c, "A full bar of perfect memory for call-and-response phrases.")
                        .mode (M::digital).div (Div::bar1).fb (32).mix (24).a (50).b (50).cut (40, 16000));

        v.push_back (B ("Diffuse Digital Hall", c, "Eighth notes softened into a small hall by the diffusion network.")
                        .mode (M::digital).div (Div::n8).fb (60).mix (28).a (50).b (42).diff (72).cut (150, 7000));

        v.push_back (B ("Thirty-Second Roll", c, "A tight machine-gun roll that hangs off every transient.")
                        .mode (M::digital).div (Div::n32).fb (84).mix (24).a (50).b (50).cut (100, 9000).duck (30));

        v.push_back (B ("Mid/Side Bloom", c, "Mid and side are delayed separately: echoes open outward from the centre.")
                        .mode (M::digital).stereo (SM::midSide).div (Div::n8d).fb (50).mix (30).a (50).b (50).offset (50).width (165));

        v.push_back (B ("Darkening Quarter", c, "Each repeat a shade darker than the last, with digital precision.")
                        .mode (M::digital).div (Div::n4).fb (66).mix (28).a (50).b (8));

        v.push_back (B ("Precision Slap", c, "One tight, full-range slap. The engineer's ruler.")
                        .mode (M::digital).ms (50).fb (0).mix (32).a (50).b (50));

        v.push_back (B ("Offset Twins", c, "Two identical lines pulled slightly apart: eighth notes with built-in width and shimmer.")
                        .mode (M::stereo).div (Div::n8).fb (50).mix (28).a (50).b (60).spread (100).width (140).mod (10, 0.2f));

        v.push_back (B ("Digital Ghosts", c, "Gritty dotted eighths haunted by faint, drifting, filtered doubles.")
                        .mode (M::digital).div (Div::n8d).fb (55).mix (32).a (85).b (45).ghost (85).cut (300, 7000).rnd (20).sat (10));
    }

    // ========================================================================
    void rhythmic (Presets& v)
    {
        const Cat c = Cat::rhythmic;

        v.push_back (B ("Dotted Eighth Hero", c, "The dotted-eighth figure that turns picked eighths into cascading sixteenths.")
                        .mode (M::digital).div (Div::n8d).fb (36).mix (36).a (50).b (55).cut (110, 9000).pp (15));

        v.push_back (B ("Multi-Tap Gallop", c, "Four fading taps per half note, fanned out left and right.")
                        .mode (M::multiTap).div (Div::n2).fb (36).mix (26).a (33).b (28).spread (72).cut (90, 10000));

        v.push_back (B ("Eight Step Crescendo", c, "Eight taps swelling toward the bar line across the whole stereo field.")
                        .mode (M::multiTap).div (Div::bar1).fb (18).mix (26).a (100).b (96).spread (100).cut (120, 12000));

        v.push_back (B ("Triplet Cascade", c, "Three taps across a half note: quarter-note triplets that keep rolling.")
                        .mode (M::multiTap).div (Div::n2).fb (45).mix (26).a (17).b (40).spread (60));

        v.push_back (B ("Three Against Four", c, "Delay A in quarters on the left, delay B in dotted eighths on the right.")
                        .mode (M::dual).div (Div::n4).fb (55).mix (30).a (50).b (50).cut (100, 9000));

        v.push_back (B ("Swing Shuffle", c, "Straight eighths on the left, triplet quarters on the right: instant swing.")
                        .mode (M::stereo).div (Div::n8).fb (44).mix (28).a (64).b (52).cut (120, 8500));

        v.push_back (B ("Sixteenth Machine", c, "Ducked ping-pong sixteenths with a little converter grit: a sequencer for anything.")
                        .mode (M::digital).div (Div::n16).fb (60).mix (26).a (60).b (50).cut (200, 8000).duck (50).pp (100).sat (5));

        v.push_back (B ("Syncopated Heads", c, "Heads two and three of a tape echo at sixteenth spacing: eighth plus dotted eighth.")
                        .mode (M::tapeEcho).div (Div::n16).fb (42).mix (30).a (64).b (20).cut (100, 7000));

        v.push_back (B ("Bouncing Ball", c, "Every hit drops and bounces, each rebound quicker than the last.")
                        .mode (M::digital).div (Div::n4).fb (18).mix (36).a (50).b (50).collapse (85).cut (80, 12000));

        v.push_back (B ("Down the Stairs", c, "Echoes start as a flurry and land further and further apart.")
                        .mode (M::digital).div (Div::n16).fb (10).mix (36).a (50).b (40).collapse (-80).cut (100, 8000));

        v.push_back (B ("Fractal Rhythm", c, "Four nested echo scales near the golden ratio: a rhythm that never quite repeats.")
                        .mode (M::fractal).div (Div::n2).fb (50).mix (30).a (50).b (39).spread (80));

        v.push_back (B ("Dub Techno Chords", c, "Filtered, diffused dotted eighths that roll on for bars.")
                        .mode (M::bbd).div (Div::n8d).fb (72).mix (34).a (70).b (15).cut (190, 2400, 26).diff (36).pp (55).mod (18, 0.25f));

        v.push_back (B ("Offbeat Skank", c, "One bright, thin offbeat answer, ducked under the chop.")
                        .mode (M::digital).div (Div::n8).fb (28).mix (40).a (65).b (60).cut (420, 5000).duck (62));

        v.push_back (B ("Quintuplet Clock", c, "Five against four at 120 bpm: 400 ms ping-pong for odd-meter tension.")
                        .mode (M::digital).ms (400).fb (58).mix (28).a (50).b (50).pp (100).cut (150, 10000));

        v.push_back (B ("Double Time Warp", c, "Quarter notes warped to double speed. Automate TIME WARP to shift gears with a tape-style bend.")
                        .mode (M::digital).div (Div::n4).fb (52).mix (28).a (50).b (50).warp (50).inertia (55).pp (30).cut (90, 13000));

        v.push_back (B ("Random Access Rhythm", c, "Four taps re-rolled every cycle but locked to the sixteenth-note grid.")
                        .mode (M::random).div (Div::n2).fb (46).mix (30).a (58).b (85).spread (70));
    }

    // ========================================================================
    void pingPongStereo (Presets& v)
    {
        const Cat c = Cat::pingPongStereo;

        v.push_back (B ("Classic Ping Pong", c, "Clean eighth notes bouncing hard left, hard right.")
                        .mode (M::digital).div (Div::n8).fb (56).mix (28).a (50).b (50).pp (100));

        v.push_back (B ("Wide Screen", c, "Quarter notes on the left, quarter triplets on the right: a wide, shifting picture.")
                        .mode (M::stereo).div (Div::n4).fb (45).mix (28).a (22).b (50).width (170));

        v.push_back (B ("Pattern L-R-R-L", c, "The bounce doubles back on itself: left, right, right, left.")
                        .mode (M::pingPong).div (Div::n8).fb (66).mix (30).a (75).b (92).cut (100, 11000));

        v.push_back (B ("Triangle Throw", c, "Left, right, centre in eighth-note triplets.")
                        .mode (M::pingPong).div (Div::n8t).fb (72).mix (30).a (40).b (100));

        v.push_back (B ("Haas Widener", c, "Two very short, unequal delays: turns a mono source wide without an audible echo.")
                        .mode (M::digital).ms (17).fb (0).mix (50).a (50).b (50).offset (85).width (200));

        v.push_back (B ("Spiral Galaxy", c, "Diffused repeats winding outward and back along a slow spiral.")
                        .mode (M::diffused).div (Div::n8d).fb (66).mix (34).a (45).b (55).orbit (100, 0.22f, O::spiral).ghost (28)
                        .cut (120, 9000).mod (15, 0.2f));

        v.push_back (B ("Circular Motion", c, "Tape eighths travelling around your head every two seconds.")
                        .mode (M::tape).div (Div::n8).fb (55).mix (30).a (18).b (24).orbit (92, 0.5f, O::circle).cut (90, 9000));

        v.push_back (B ("Figure Eight", c, "Dotted sixteenths tracing a slow figure of eight, front to back and side to side.")
                        .mode (M::digital).div (Div::n16d).fb (60).mix (28).a (50).b (50).orbitSync (100, Div::bar2, O::figure8).cut (150, 12000));

        v.push_back (B ("Pendulum Clock", c, "Eighth notes swinging from wall to wall once a bar.")
                        .mode (M::digital).div (Div::n8).fb (50).mix (28).a (50).b (45).orbitSync (100, Div::bar1, O::pendulum));

        v.push_back (B ("Scatter Brain", c, "Every sixteenth lands somewhere new in the stereo field.")
                        .mode (M::digital).div (Div::n16).fb (64).mix (28).a (50).b (55).orbitSync (100, Div::n16, O::scatter).cut (200, 10000));

        v.push_back (B ("Mid/Side Mirage", c, "Tape repeats where the sides arrive before the middle and trade places.")
                        .mode (M::tape).stereo (SM::midSide).div (Div::n4).fb (52).mix (30).a (25).b (25).offset (-40).pp (35).width (150));

        v.push_back (B ("Tennis Match", c, "A long, slow rally of darkening quarter notes.")
                        .mode (M::digital).div (Div::n4).fb (72).mix (30).a (50).b (30).pp (100).cut (100, 6000));

        v.push_back (B ("Cross-Feed Tape", c, "Triplet tape echo whose channels leak into each other and drift apart.")
                        .mode (M::tape).div (Div::n4t).fb (58).mix (30).a (28).b (35).pp (60).offset (25).spread (40));

        v.push_back (B ("Dotted Left, Quarter Right", c, "Two note values at once: dotted eighths on one side, quarters on the other.")
                        .mode (M::stereo).div (Div::n8d).fb (50).mix (28).a (64).b (50));

        v.push_back (B ("Width Breather", c, "Left and right echoes drift toward each other and apart again.")
                        .mode (M::digital).div (Div::n8).fb (55).mix (28).a (50).b (50).lane (L::pan, S::sine, 100, Div::bar1, 0, 180));

        v.push_back (B ("Dual Pan Split", c, "Sixteenths on the left answered by eighths on the right.")
                        .mode (M::dual).div (Div::n16).fb (48).mix (28).a (80).b (40).width (140));

        v.push_back (B ("Ping Pong Slap", c, "A quick 130 ms slap that hops sides as it fades.")
                        .mode (M::digital).ms (130).fb (36).mix (30).a (50).b (50).pp (100).cut (120, 9000));

        v.push_back (B ("Centre Left Centre Right", c, "Sixteenths that keep returning to the middle between throws.")
                        .mode (M::pingPong).div (Div::n16).fb (70).mix (30).a (90).b (80));
    }

    // ========================================================================
    void ambient (Presets& v)
    {
        const Cat c = Cat::ambient;

        v.push_back (B ("Dream Sequence", c, "Soft-focus repeats dissolving into a slow, shimmering wash.")
                        .mode (M::diffused).div (Div::n4d).fb (70).mix (38).a (70).b (72).mod (30, 0.2f).cut (120, 6000).ghost (35).width (140));

        v.push_back (B ("Neon Rain", c, "Tiny bright grains scattered an octave up: droplets of light across the stereo field.")
                        .mode (M::granular).div (Div::n8).fb (55).mix (50).a (14).b (88).pitch (12).rnd (55).spread (100)
                        .cut (300, 13000).diff (15));

        v.push_back (B ("Cloud Chamber", c, "Long overlapping grains smear every note into a drifting cloud.")
                        .mode (M::granular).div (Div::n4).fb (62).mix (44).a (82).b (60).diff (52).spread (90).cut (100, 8000));

        v.push_back (B ("Fog Bank", c, "Huge, dark and slow: echoes roll in like weather.")
                        .mode (M::diffused).div (Div::n2).fb (78).mix (36).a (100).b (92).cut (80, 2200).mod (20, 0.1f));

        v.push_back (B ("Aurora", c, "Octave shimmer rising out of every note, slowly circling overhead.")
                        .mode (M::shimmer).div (Div::n4d).fb (72).mix (40).a (45).b (80).pitch (12).cut (150, 8500).orbit (35, 0.08f, O::circle));

        v.push_back (B ("Slow Tide", c, "A bar-long analog wash that swells and recedes over four bars.")
                        .mode (M::warmAnalog).div (Div::bar1).fb (64).mix (36).a (72).b (60).diff (62).lane (L::mix, S::sine, 30, Div::bar4)
                        .cut (60, 5000));

        v.push_back (B ("Whale Song", c, "Reversed swells that drop an octave each pass, calling from very far away.")
                        .mode (M::reverse).div (Div::n2).fb (55).mix (42).a (92).b (72).rev (30).diff (56).pitch (-12).cut (40, 4000));

        v.push_back (B ("Glacier", c, "Every frequency freezes as it passes and melts away slowly.")
                        .mode (M::spectral).div (Div::n2).fb (36).mix (20).a (50).b (60).diff (40).cut (80, 9000));

        v.push_back (B ("Morning Mist", c, "Dotted eighths blurred until only their glow is left.")
                        .mode (M::digital).div (Div::n8d).fb (56).mix (32).a (50).b (40).diff (88).cut (200, 5200).mod (25, 0.15f).ghost (20));

        v.push_back (B ("Vapor Trails", c, "A cloud made only of reversed grains: every note leaves a trail leading back to it.")
                        .mode (M::granular).div (Div::n8d).fb (60).mix (42).a (60).b (40).rev (100).diff (40).spread (70).cut (150, 9000));

        v.push_back (B ("Deep Field", c, "Six nested scales of echo, diffused until they read as distance.")
                        .mode (M::fractal).div (Div::bar1).fb (62).mix (34).a (100).b (62).diff (58).spread (100).cut (100, 7000));

        v.push_back (B ("Cathedral of Tape", c, "Three tape heads feeding a wall of diffusion: an echo unit in a stone nave.")
                        .mode (M::tapeEcho).div (Div::n4).fb (60).mix (34).a (95).b (30).diff (82).cut (120, 5200));

        v.push_back (B ("Lighthouse", c, "Long, patient repeats sweeping a full circle every two bars.")
                        .mode (M::tape).div (Div::n2d).fb (58).mix (32).a (20).b (40).orbitSync (100, Div::bar2, O::circle).diff (35).cut (80, 6000));
    }

    // ========================================================================
    void experimental (Presets& v)
    {
        const Cat c = Cat::experimental;

        v.push_back (B ("Quantum Echo", c, "You can know when an echo will arrive or where - never both.")
                        .mode (M::random).div (Div::n8).fb (60).mix (32).a (88).b (10).rnd (70).spread (100).cut (120, 11000));

        v.push_back (B ("Ghost Machine", c, "Between the repeats, pale filtered doubles drift in and out of tune.")
                        .mode (M::bbd).div (Div::n4).fb (60).mix (36).a (55).b (35).ghost (100).cut (150, 3600).diff (42)
                        .orbit (55, 0.11f, O::scatter).tapeMem (28));

        v.push_back (B ("Comb Robot", c, "An 8 ms mono loop turns everything into a pitched metallic voice.")
                        .mode (M::digital).stereo (SM::mono).ms (8).fb (90).mix (36).a (50).b (50).cut (80, 9000).sat (45, ST::diode));

        v.push_back (B ("Fractal Fern", c, "Echoes inside echoes at the golden ratio, unfolding like a fern.")
                        .mode (M::fractal).div (Div::n2).fb (72).mix (32).a (100).b (39).spread (100).cut (120, 10000));

        v.push_back (B ("Spectral Prism", c, "Splits each echo into its colours: low notes land first, the highs trail behind.")
                        .mode (M::spectral).div (Div::n4).fb (55).mix (30).a (100).b (18).spread (80));

        v.push_back (B ("Spectral Undertow", c, "The treble arrives on time; the bass is dragged out after it.")
                        .mode (M::spectral).div (Div::n4d).fb (50).mix (24).a (0).b (30).spread (30).cut (60, 12000));

        v.push_back (B ("Sympathetic Strings", c, "A string resonator tuned near D: it rings along with whatever you feed it.")
                        .mode (M::digital).ms (3.4f).fb (97).mix (30).a (50).b (50).cut (60, 5200).spread (100));

        v.push_back (B ("Ring Tunnel", c, "Fast, deep clock modulation on a tiny loop: a metallic vibrato tunnel.")
                        .mode (M::bbd).ms (12).fb (70).mix (40).a (20).b (50).mod (85, 6.5f).cut (120, 7000));

        v.push_back (B ("Black Hole", c, "Echoes fall inward, faster and lower, and nothing quite comes back out.")
                        .mode (M::diffused).div (Div::n4).fb (76).mix (32).a (30).b (85).collapse (100).warp (-20).cut (40, 3200)
                        .pitch (-1).inertia (70));

        v.push_back (B ("Hall of Mirrors", c, "Nested echoes, half of them reversed, reflected from side to side.")
                        .mode (M::fractal).div (Div::n4).fb (70).mix (36).a (75).b (80).rev (55).pp (100).spread (60).cut (200, 9000));

        v.push_back (B ("Alien Radio", c, "A sample-and-hold filter jumping over a crushed, band-limited loop.")
                        .mode (M::loFi).div (Div::n8).fb (62).mix (34).a (55).b (68).cut (250, 5000, 45)
                        .lane (L::filter, S::sampleHold, 70, Div::n16));

        v.push_back (B ("Elastic Band", c, "The delay time is stretched and released twice a second: rubbery bends on every repeat.")
                        .mode (M::digital).div (Div::n8).fb (58).mix (30).a (50).b (50).laneHz (L::speed, S::sine, 16, 2.1f).inertia (30));

        v.push_back (B ("Inside the Piano", c, "A short, dense, resonant loop: the body of an instrument that is not there.")
                        .mode (M::diffused).ms (46).fb (82).mix (30).a (20).b (100).cut (100, 4200, 10));

        v.push_back (B ("Barber Pole", c, "Every sixteenth a semitone higher: an endless staircase.")
                        .mode (M::pitch).div (Div::n16).fb (86).mix (30).a (0).b (35).pitch (1).cut (100, 9000));

        v.push_back (B ("Time Dilation", c, "The same eighth note, stretched by TIME WARP and a heavy flywheel: turn the control for slow-motion bends.")
                        .mode (M::tape).div (Div::n8).fb (60).mix (32).a (30).b (35).warp (-36).inertia (90).tapeMem (20).cut (90, 8000));

        v.push_back (B ("Wormhole", c, "Each pass drops a fourth and spirals further out.")
                        .mode (M::pitch).div (Div::n8d).fb (78).mix (32).a (60).b (80).pitch (-5).diff (45).orbit (70, 0.3f, O::spiral));
    }

    // ========================================================================
    void glitch (Presets& v)
    {
        const Cat c = Cat::glitch;

        v.push_back (B ("Broken Clock", c, "Time skips, stutters and occasionally runs backwards.")
                        .mode (M::glitch).div (Div::n4).fb (52).mix (36).a (55).b (72).rnd (42)
                        .lane (L::speed, S::sampleHold, 44, Div::n8).inertia (10).spread (60));

        v.push_back (B ("Stutter Step", c, "Sixteenth-note slices that repeat, ratchet and flip on a looping pattern.")
                        .mode (M::glitch).div (Div::n4).fb (34).mix (36).a (45).b (55).cut (80, 12000));

        v.push_back (B ("Micro Edit", c, "Thirty-second-note cuts thrown all over the stereo field.")
                        .mode (M::glitch).div (Div::n2).fb (46).mix (36).a (95).b (88).spread (100));

        v.push_back (B ("Tape Stop Shop", c, "Long quarter-note slices: half-speed drops, tape stops and rewinds.")
                        .mode (M::glitch).div (Div::n2).fb (40).mix (36).a (18).b (62).sat (30).cut (60, 8000));

        v.push_back (B ("Granular Shredder", c, "Tiny 14 ms grains, maximum scatter, random octaves.")
                        .mode (M::granular).div (Div::n8).fb (50).mix (42).a (4).b (100).rnd (100).spread (100).cut (200, 14000));

        v.push_back (B ("Buffer Override", c, "Every slice is an accident: full chaos, no repeating pattern, bit-crushed feedback.")
                        .mode (M::glitch).div (Div::n4).fb (62).mix (36).a (70).b (100).rnd (100).sat (42, ST::crush).spread (80));

        v.push_back (B ("Ratchet Rocket", c, "Eighth-note slices that ratchet and climb an octave with every pass.")
                        .mode (M::glitch).div (Div::n4d).fb (44).mix (34).a (30).b (80).pitch (12).cut (150, 12000));

        v.push_back (B ("Data Moshing", c, "Frequency bands arrive in the wrong order and smear into each other.")
                        .mode (M::spectral).div (Div::n8).fb (50).mix (22).a (50).b (40).rnd (90).spread (100).sat (35, ST::crush));

        v.push_back (B ("IDM Scatter", c, "Random taps on a strict grid, each thrown to a new position every eighth.")
                        .mode (M::random).div (Div::n4).fb (56).mix (34).a (100).b (100).orbitSync (100, Div::n8, O::scatter).cut (150, 12000));

        v.push_back (B ("Skipping CD", c, "Tiny slices and sudden jumps in time: the laser keeps losing its place.")
                        .mode (M::glitch).div (Div::n8).fb (40).mix (36).a (40).b (48).laneHz (L::speed, S::sampleHold, 12, 3.0f).inertia (0));

        v.push_back (B ("Pitch Ladder", c, "Every sixteenth lands on a different random pitch.")
                        .mode (M::digital).div (Div::n16).fb (50).mix (32).a (50).b (50).lane (L::pitch, S::sampleHold, 100, Div::n16)
                        .cut (100, 10000));

        v.push_back (B ("Crushed Sixteenths", c, "Three-bit feedback: each repeat loses more resolution than the last.")
                        .mode (M::digital).div (Div::n16).fb (62).mix (26).a (92.5).b (50).sat (92, ST::crush).cut (120, 7000).pp (50));

        v.push_back (B ("Reverse Stutter", c, "Slices and reversed chunks of the last half note, cut together.")
                        .mode (M::glitch).div (Div::n2).fb (48).mix (36).a (45).b (60).rev (70).diff (15));

        v.push_back (B ("Fold and Mutilate", c, "A wave-folder in the feedback loop: repeats turn inside out.")
                        .mode (M::digital).div (Div::n8).fb (66).mix (26).a (70).b (50).sat (100, ST::fold).drive (18).cut (150, 6500, 20));

        v.push_back (B ("Random Access Memory", c, "Triplet-time taps that jump to new grid positions whenever they like.")
                        .mode (M::random).div (Div::n4t).fb (52).mix (32).a (70).b (45).cut (100, 9000));

        v.push_back (B ("Morse Code", c, "Mono, band-passed, gated slices: a message tapped out in dots and dashes.")
                        .mode (M::glitch).stereo (SM::mono).div (Div::n4).fb (42).mix (40).a (70).b (100).cut (600, 3500, 45));

        v.push_back (B ("Bitstream", c, "A crushed thirty-second-note buzz that turns hits into data bursts.")
                        .mode (M::loFi).div (Div::n32).fb (88).mix (30).a (70).b (40).cut (200, 8000));
    }

    // ========================================================================
    void pitchShimmer (Presets& v)
    {
        const Cat c = Cat::pitchShimmer;

        v.push_back (B ("Octave Stairway", c, "Each eighth note climbs another octave.")
                        .mode (M::pitch).div (Div::n8).fb (60).mix (30).a (8).b (40).pitch (12).cut (100, 11000));

        v.push_back (B ("Shimmer Cathedral", c, "A vast diffused space where every repeat blooms an octave higher.")
                        .mode (M::shimmer).div (Div::n4).fb (76).mix (38).a (60).b (92).pitch (12).cut (200, 9000));

        v.push_back (B ("Fifth Dimension", c, "Quarter notes stacking in perfect fifths.")
                        .mode (M::pitch).div (Div::n4).fb (55).mix (30).a (18).b (55).pitch (7));

        v.push_back (B ("Falling Through Time", c, "Every repeat a whole tone lower and a little later than the last.")
                        .mode (M::pitch).div (Div::n8d).fb (76).mix (34).a (25).b (70).pitch (-2).diff (32).collapse (-45).warp (-10)
                        .cut (60, 7000).orbit (45, 0.1f, O::spiral));

        v.push_back (B ("Micro Pitch Wide", c, "Fifty cents up on the left, fifty down on the right, 24 ms behind: the classic thickener.")
                        .mode (M::pitch).ms (24).fb (0).mix (42).a (100).b (25).width (170));

        v.push_back (B ("Crystal Reverse", c, "Reversed quarter notes that rise an octave each time they come around.")
                        .mode (M::reverse).div (Div::n4).fb (56).mix (34).a (60).b (40).rev (50).pitch (12).cut (200, 12000));

        v.push_back (B ("Sub Octave Shadow", c, "A dark double an octave below, trailing a quarter note behind.")
                        .mode (M::pitch).div (Div::n4).fb (42).mix (32).a (0).b (60).pitch (-12).cut (40, 3200));

        v.push_back (B ("Whole Tone Climb", c, "Ping-pong sixteenths rising by whole tones into the distance.")
                        .mode (M::pitch).div (Div::n16).fb (80).mix (28).a (10).b (30).pitch (2).cut (120, 10000).pp (100));

        v.push_back (B ("Perfect Fifth Halo", c, "Diffused dotted eighths with a ring of fifths above them.")
                        .mode (M::shimmer).div (Div::n8d).fb (66).mix (38).a (42).b (62).pitch (7).cut (180, 9500));

        v.push_back (B ("Dark Shimmer", c, "Shimmer turned upside down: each pass sinks an octave into the dark.")
                        .mode (M::shimmer).div (Div::n2).fb (70).mix (38).a (55).b (85).pitch (-12).cut (50, 3600));

        v.push_back (B ("Minor Third Ladder", c, "Triplet eighths climbing a diminished staircase.")
                        .mode (M::pitch).div (Div::n8t).fb (72).mix (30).a (12).b (35).pitch (3));

        v.push_back (B ("Octave Ping Pong", c, "Dotted eighths that jump sides and octaves at the same time.")
                        .mode (M::pitch).div (Div::n8d).fb (62).mix (30).a (0).b (45).pitch (12).pp (100));

        v.push_back (B ("Tritone Paradox", c, "Quarter triplets shifted by a tritone: rising and falling at once.")
                        .mode (M::pitch).div (Div::n4t).fb (68).mix (30).a (20).b (50).pitch (6).cut (100, 8000));

        v.push_back (B ("Angel Dust", c, "A cloud of grains a fifth above the note, softened by diffusion.")
                        .mode (M::granular).div (Div::n4).fb (52).mix (40).a (40).b (70).pitch (7).spread (100).diff (55).cut (200, 12000));

        v.push_back (B ("Detuned Dream", c, "No transposition, just forty cents of drift between left and right on every pass.")
                        .mode (M::pitch).div (Div::n4d).fb (48).mix (30).a (80).b (85).pitch (0).mod (20, 0.4f).diff (30));

        v.push_back (B ("Glass Harmonica", c, "A 90 ms shimmer loop: struck notes ring upward like wet glass.")
                        .mode (M::shimmer).ms (90).fb (82).mix (36).a (70).b (20).pitch (12).cut (400, 12000));
    }

    // ========================================================================
    void vocal (Presets& v)
    {
        const Cat c = Cat::vocal;

        v.push_back (B ("Vocal Plate Slap", c, "A short diffused slap with a wide double: depth without a tail.")
                        .mode (M::slapback).ms (86).fb (10).mix (22).a (22).b (42).cut (150, 7000).diff (30));

        v.push_back (B ("Ducked Quarter Throw", c, "Quarter-note repeats that only appear in the gaps between phrases.")
                        .mode (M::digital).div (Div::n4).fb (46).mix (30).a (50).b (45).duck (85).cut (250, 5500));

        v.push_back (B ("Telephone Callback", c, "Mono, band-limited, slightly distorted eighths: the answer down the line.")
                        .mode (M::bbd).stereo (SM::mono).div (Div::n8).fb (40).mix (30).a (35).b (45).cut (420, 3000, 22).sat (32));

        v.push_back (B ("Whisper Doubler", c, "Two close, slowly drifting slaps: an automatic double-track for intimate vocals.")
                        .mode (M::slapback).ms (48).fb (0).mix (40).a (0).b (100).mod (30, 0.5f).cut (200, 9000));

        v.push_back (B ("Dotted Vocal Halo", c, "Soft, ducked dotted eighths on tape with a diffused edge.")
                        .mode (M::tape).div (Div::n8d).fb (36).mix (26).a (15).b (30).diff (38).duck (60).cut (220, 6500));

        v.push_back (B ("Arena Shout", c, "Three fading slaps off the back wall of a stadium.")
                        .mode (M::multiTap).ms (420).fb (22).mix (26).a (17).b (18).cut (200, 5000).diff (26).spread (50));

        v.push_back (B ("Dream Pop Wash", c, "Reversed, diffused, ducked: the vocal swells back in behind itself.")
                        .mode (M::reverse).div (Div::n2t).fb (48).mix (40).a (82).b (50).rev (20).diff (62).duck (40).cut (250, 7000));

        v.push_back (B ("Harmony Up a Third", c, "A major third above, 26 ms behind and off to the right: an instant harmony singer.")
                        .mode (M::pitch).ms (26).fb (0).mix (38).a (10).b (45).pitch (4).cut (150, 9000)
                        .lane (L::pan, S::custom, 55, Div::bar1).curve (L::pan, Pts { { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }));

        v.push_back (B ("Lead Vocal Ping", c, "Ducked eighth-note ping-pong that keeps the lead in the centre.")
                        .mode (M::digital).div (Div::n8).fb (32).mix (26).a (50).b (50).pp (100).duck (70).cut (300, 6000));

        v.push_back (B ("Tape Vocal Slap", c, "150 ms of tape: the classic rock'n'roll vocal thickener.")
                        .mode (M::tape).ms (150).fb (15).mix (24).a (14).b (20).sat (25).cut (120, 8000));

        v.push_back (B ("Ghost Choir", c, "Quiet octave shimmer and drifting ghost voices filling the space behind the lead.")
                        .mode (M::shimmer).div (Div::n4).fb (58).mix (30).a (30).b (70).pitch (12).ghost (65).duck (50).cut (250, 8000));

        v.push_back (B ("Long Vocal Throw", c, "Half-note throws for the last word of a line, held back until the phrase ends.")
                        .mode (M::digital).div (Div::n2).fb (56).mix (34).a (50).b (35).duck (90).cut (300, 4500).diff (20).pp (40));

        v.push_back (B ("Radio Voice", c, "A crushed, mono, 110 ms repeat: the voice through a cheap receiver.")
                        .mode (M::loFi).stereo (SM::mono).ms (110).fb (10).mix (40).a (30).b (55).cut (350, 3200));

        v.push_back (B ("Formant Echo", c, "A narrow resonant band gives every repeat a vowel.")
                        .mode (M::digital).div (Div::n8).fb (62).mix (30).a (50).b (50).cut (650, 1800, 70));

        v.push_back (B ("Sixteenth Whisper", c, "Dark, ducked bucket-brigade sixteenths that add urgency without clutter.")
                        .mode (M::bbd).div (Div::n16).fb (32).mix (24).a (60).b (15).cut (200, 4000).duck (40));

        v.push_back (B ("Reverse Breath", c, "Each phrase is answered by its own reversed, swelling echo.")
                        .mode (M::reverse).div (Div::n4).fb (20).mix (38).a (50).b (100).rev (0).cut (300, 9000).duck (30));
    }

    // ========================================================================
    void guitar (Presets& v)
    {
        const Cat c = Cat::guitar;

        v.push_back (B ("Edge of the Stadium", c, "Straight eighths on the left, dotted eighths on the right, a little modulation.")
                        .mode (M::dual).div (Div::n8).fb (40).mix (34).a (72).b (58).cut (110, 8500).mod (12, 0.6f));

        v.push_back (B ("Surf Slap", c, "Tube-warmed slap with a splash of spring.")
                        .mode (M::slapback).ms (96).fb (22).mix (34).a (72).b (15).sat (20, ST::tube).diff (35).cut (120, 9000));

        v.push_back (B ("Country Chicken", c, "One loud, bright, mono slap for fast picking.")
                        .mode (M::slapback).stereo (SM::mono).ms (160).fb (0).mix (40).a (100).b (0).cut (200, 11000));

        v.push_back (B ("Volume Swell", c, "Reversed, swelling dotted quarters: bowed-guitar pads from single notes.")
                        .mode (M::reverse).div (Div::n4d).fb (45).mix (40).a (70).b (92).rev (0).diff (46).cut (100, 8000));

        v.push_back (B ("Post-Rock Horizon", c, "Saturated, diffused dotted eighths that pile up into a wall.")
                        .mode (M::tape).div (Div::n8d).fb (68).mix (36).a (30).b (40).diff (52).mod (25, 0.25f).sat (30).cut (120, 7000).width (140));

        v.push_back (B ("Analog Solo Lead", c, "380 ms of bucket brigade behind a lead: dark enough to stay out of the way.")
                        .mode (M::bbd).ms (380).fb (42).mix (26).a (66).b (20).mod (15, 0.5f));

        v.push_back (B ("Funk Sixteenths", c, "A single tight sixteenth that doubles the groove.")
                        .mode (M::digital).div (Div::n16).fb (22).mix (25).a (50).b (55).cut (300, 7000));

        v.push_back (B ("Shoegaze Wall", c, "Driven tube feedback through heavy diffusion and slow modulation.")
                        .mode (M::diffused).div (Div::n8).fb (80).mix (34).a (60).b (92).sat (55, ST::tube).drive (10).mod (35, 0.3f).cut (100, 6500));

        v.push_back (B ("Piano Halo", c, "Diffused dotted quarters that sit behind the notes like a sustain pedal.")
                        .mode (M::digital).div (Div::n4d).fb (50).mix (26).a (50).b (45).diff (76).cut (150, 5500).duck (30));

        v.push_back (B ("Electric Piano Chorus", c, "Wide bucket-brigade quarter notes with a deep, slow chorus.")
                        .mode (M::bbd).div (Div::n4).fb (38).mix (28).a (76).b (12).mod (46, 0.9f).spread (80));

        v.push_back (B ("Arp Enhancer", c, "Sixteenths against dotted sixteenths: turns a simple arpeggio into a pattern.")
                        .mode (M::stereo).div (Div::n16).fb (55).mix (26).a (78).b (55).pp (40).cut (200, 10000));

        v.push_back (B ("Tight Bass Slap", c, "Mono, band-limited, 82 ms: adds size to a bass without clouding the low end.")
                        .mode (M::digital).stereo (SM::mono).ms (82).fb (0).mix (18).a (50).b (50).cut (150, 2500));

        v.push_back (B ("Acoustic Space", c, "One soft, diffused tape reflection: a small room around an acoustic instrument.")
                        .mode (M::tape).ms (165).fb (20).mix (20).a (10).b (25).diff (58).cut (100, 6000).width (130));

        v.push_back (B ("Slide Guitar Spring", c, "Single-head tape echo into a long spring.")
                        .mode (M::tapeEcho).div (Div::n8).fb (40).mix (30).a (8).b (46).diff (66).cut (150, 5000));

        v.push_back (B ("Pedal Steel Clouds", c, "A gentle octave halo on half-note repeats.")
                        .mode (M::shimmer).div (Div::n2).fb (50).mix (30).a (22).b (55).pitch (12).mod (18, 0.3f).cut (150, 7500));

        v.push_back (B ("Drum Room Slap", c, "44 ms with a wide double: instant small room for drums and percussion.")
                        .mode (M::slapback).ms (44).fb (0).mix (26).a (40).b (62).diff (34).cut (80, 10000));

        v.push_back (B ("Synth Lead Triplet", c, "Quarter triplets leaning left and right with a slow chorus.")
                        .mode (M::digital).div (Div::n4t).fb (52).mix (26).a (50).b (50).pp (70).mod (20, 0.5f));
    }

    // ========================================================================
    void cinematic (Presets& v)
    {
        const Cat c = Cat::cinematic;

        v.push_back (B ("The Last Transmission", c, "A voice from very far away, breaking up a little more each time it repeats.")
                        .mode (M::degraded).stereo (SM::mono).div (Div::n4d).fb (74).mix (38).a (82).b (88).tapeMem (52)
                        .cut (300, 2600, 20).ghost (50).rnd (25).lane (L::mix, S::random, 35, Div::bar2));

        v.push_back (B ("Reverse Universe", c, "Everything you play comes back before it happened, circling slowly.")
                        .mode (M::reverse).div (Div::n2).fb (66).mix (40).a (86).b (60).rev (72).diff (52)
                        .orbit (70, 0.12f, O::circle).ghost (30).cut (80, 9000));

        v.push_back (B ("Braam Tail", c, "Each repeat sinks an octave into a wall of low-end pressure.")
                        .mode (M::diffused).div (Div::n2).fb (82).mix (38).a (90).b (80).pitch (-12).sat (42).cut (30, 1900).drive (6));

        v.push_back (B ("Tension Riser", c, "Sixteenths climbing two octaves and thinning out over four bars.")
                        .mode (M::digital).div (Div::n16).fb (82).mix (34).a (50).b (60).lane (L::pitch, S::saw, 100, Div::bar4)
                        .lane (L::filter, S::saw, 50, Div::bar4).cut (100, 12000));

        v.push_back (B ("Deep Space Probe", c, "Dispersed, sustained spectra and ghost signals on a slow figure-of-eight orbit.")
                        .mode (M::spectral).div (Div::n2).fb (45).mix (22).a (75).b (62).spread (100).ghost (40)
                        .orbit (80, 0.06f, O::figure8).cut (100, 10000));

        v.push_back (B ("Underwater", c, "Everything above 900 Hz is gone; what is left sways with the current.")
                        .mode (M::warmAnalog).div (Div::n8).fb (64).mix (40).a (90).b (50).cut (40, 900, 32).mod (40, 0.3f).diff (42));

        v.push_back (B ("Horror Strings", c, "Sixteenths sliding flat by a semitone each pass, half-reversed and resonant.")
                        .mode (M::pitch).div (Div::n16).fb (84).mix (34).a (55).b (20).pitch (-1).diff (30).rev (30).cut (200, 6000, 8));

        v.push_back (B ("Jet Engine", c, "A tiny loop swept slowly through its range: the jet flange, turned up to eleven.")
                        .mode (M::digital).ms (11).fb (84).mix (40).a (50).b (50).laneHz (L::speed, S::triangle, 90, 0.08f).sat (30).cut (60, 12000));

        v.push_back (B ("Doppler Flyby", c, "Echoes rise in pitch as they approach and fall as they pass to the other side.")
                        .mode (M::digital).div (Div::n8).fb (55).mix (34).a (50).b (50).laneHz (L::speed, S::sine, 12, 0.25f, 0)
                        .laneHz (L::pan, S::sine, 100, 0.25f, 90).inertia (40).cut (100, 9000));

        v.push_back (B ("Ghost Ship", c, "A worn tape echo below decks, with pale voices drifting between its repeats.")
                        .mode (M::tapeEcho).div (Div::n4).fb (62).mix (36).a (80).b (70).ghost (92).tapeMem (60).cut (120, 2800).diff (40));

        v.push_back (B ("Clockwork Factory", c, "Eight resonant, clipped taps per beat with rebounds: gears, ratchets and springs.")
                        .mode (M::multiTap).div (Div::n4).fb (46).mix (22).a (100).b (25).cut (500, 6000, 40).sat (40, ST::diode)
                        .spread (100).collapse (30));

        v.push_back (B ("Event Horizon", c, "Dark shimmer collapsing inward on a slow spiral: the point of no return.")
                        .mode (M::shimmer).div (Div::n2).fb (88).mix (38).a (70).b (100).pitch (-12).collapse (100).warp (-25)
                        .cut (30, 5000).orbit (90, 0.05f, O::spiral));

        v.push_back (B ("Nebula", c, "Huge, slow grains an octave down, scattered and diffused into a glowing cloud.")
                        .mode (M::granular).div (Div::n2).fb (70).mix (38).a (100).b (100).diff (70).spread (100).pitch (-12).rnd (60).cut (60, 7000));

        v.push_back (B ("Canyon", c, "A distant 900 ms reflection, dull and wide, the far wall answering late.")
                        .mode (M::digital).ms (900).fb (55).mix (34).a (50).b (22).cut (120, 3500).diff (26).width (160).offset (20));

        v.push_back (B ("Cave Drip", c, "Thin, resonant repeats with quickening rebounds, like water in a cavern.")
                        .mode (M::digital).ms (140).fb (58).mix (34).a (50).b (70).cut (600, 9000, 54).diff (30).collapse (42));
    }

    // ========================================================================
    void loFi (Presets& v)
    {
        const Cat c = Cat::loFi;

        v.push_back (B ("VHS Dreams", c, "Soft, wobbling, slightly out-of-tracking repeats from a tape rented too many times.")
                        .mode (M::loFi).div (Div::n4).fb (50).mix (34).a (22).b (34).tapeMem (62).mod (25, 0.4f).cut (100, 5200).diff (22).width (130));

        v.push_back (B ("Midnight Transmission", c, "A distant AM station fading in and out after dark.")
                        .mode (M::degraded).stereo (SM::mono).div (Div::n8d).fb (56).mix (34).a (66).b (56).cut (350, 2800, 16)
                        .ghost (30).tapeMem (30));

        v.push_back (B ("Worn Cassette", c, "Eighth notes on a cassette that has been through the wash.")
                        .mode (M::tape).div (Div::n8).fb (45).mix (32).a (62).b (88).tapeMem (52).cut (120, 4200));

        v.push_back (B ("8-Bit Arcade", c, "Ping-pong sixteenths at five bits: coin-op echoes.")
                        .mode (M::loFi).div (Div::n16).fb (50).mix (30).a (88).b (60).sat (30, ST::crush).cut (100, 8000).pp (100));

        v.push_back (B ("Dusty Sampler", c, "Twelve-bit, band-limited eighths with the grain of an old sampler.")
                        .mode (M::loFi).div (Div::n8).fb (36).mix (30).a (34).b (24).cut (60, 6000).sat (20));

        v.push_back (B ("Bit Rot", c, "Four bits, a kilohertz of bandwidth: triplets dissolving into digital rubble.")
                        .mode (M::loFi).div (Div::n8t).fb (72).mix (32).a (96).b (86).rnd (30));

        v.push_back (B ("Generation Loss", c, "A copy of a copy of a copy: every repeat narrower than the one before.")
                        .mode (M::degraded).div (Div::n4).fb (82).mix (32).a (92).b (40));

        v.push_back (B ("Dictaphone", c, "A worn, mono micro-cassette: narrow, crushed and unsteady.")
                        .mode (M::loFi).stereo (SM::mono).ms (180).fb (30).mix (42).a (50).b (66).tapeMem (72).cut (500, 2200));

        v.push_back (B ("Thrift Store Delay", c, "Too few bucket stages and a leaking clock: gritty, aliased quarter notes.")
                        .mode (M::bbd).div (Div::n4).fb (56).mix (32).a (12).b (92));

        v.push_back (B ("Vinyl Crackle Echo", c, "Dotted eighths with surface noise and crackle riding on every repeat.")
                        .mode (M::degraded).div (Div::n8d).fb (46).mix (30).a (28).b (100).cut (80, 7000).tapeMem (20));

        v.push_back (B ("Blown Speaker", c, "Hard-clipped, mid-range repeats through a torn cone.")
                        .mode (M::degraded).div (Div::n8).fb (50).mix (32).a (50).b (30).sat (85, ST::diode).drive (16).cut (300, 2500));

        v.push_back (B ("Answering Machine", c, "Mono, heavily decimated, 250 ms: please leave a message.")
                        .mode (M::loFi).stereo (SM::mono).ms (250).fb (22).mix (40).a (42).b (82).cut (400, 3000));

        v.push_back (B ("Sun-Baked Tape", c, "Everything a tape can suffer at once: warble, dropouts, hiss and a very tired motor.")
                        .mode (M::tape).div (Div::n4).fb (56).mix (34).a (92).b (100).tapeMem (92).inertia (92).cut (150, 3200));

        v.push_back (B ("Shortwave", c, "A resonant band that wanders like a hand on the tuning dial.")
                        .mode (M::degraded).stereo (SM::mono).div (Div::n16d).fb (60).mix (34).a (55).b (70).cut (400, 2500, 52)
                        .laneHz (L::filter, S::random, 32, 0.35f));

        v.push_back (B ("Lo-Fi Beat Slap", c, "A crunchy, wobbly 68 ms slap for drums that sound sampled off a record.")
                        .mode (M::loFi).ms (68).fb (15).mix (30).a (30).b (45).tapeMem (40));
    }

    // ========================================================================
    void creativeMotion (Presets& v)
    {
        const Cat c = Cat::creativeMotion;

        v.push_back (B ("Liquid Motion", c, "Time, position and tone all flow in slow sine waves; nothing ever stands still.")
                        .mode (M::tape).div (Div::n8).fb (60).mix (34).a (15).b (25).diff (30).cut (80, 14000)
                        .lane (L::speed, S::sine, 16, Div::bar2, 0, 90)
                        .lane (L::pan, S::sine, 70, Div::bar1)
                        .lane (L::filter, S::sine, -35, Div::bar4));

        v.push_back (B ("Time Traveler", c, "Echoes accelerate into the future, snap back to the past and slowly return to now.")
                        .mode (M::tape).div (Div::n4).fb (66).mix (36).a (25).b (35).inertia (78).cut (90, 9000)
                        .lane (L::speed, S::custom, 100, Div::bar4)
                        .curve (L::speed, Pts { { 0.0f, 0.5f, 0.0f }, { 0.35f, 0.5f, 0.6f }, { 0.6f, 1.0f, 0.0f },
                                                { 0.62f, 0.05f, -0.5f }, { 1.0f, 0.5f, 0.0f } }));

        v.push_back (B ("Hyperdrive", c, "Repeats accelerate, rise and thin out until they jump to light speed and start again.")
                        .mode (M::digital).div (Div::n8).fb (72).mix (34).a (50).b (60).inertia (35)
                        .lane (L::speed, S::saw, 96, Div::bar2)
                        .lane (L::pitch, S::saw, 50, Div::bar2)
                        .lane (L::filter, S::saw, 30, Div::bar2)
                        .orbit (60, 0.9f, O::circle));

        v.push_back (B ("Pan Painter", c, "A hand-drawn zig-zag throws each sixteenth to a different spot, narrowing to the centre.")
                        .mode (M::digital).div (Div::n16).fb (64).mix (30).a (50).b (50)
                        .lane (L::pan, S::custom, 100, Div::bar1)
                        .curve (L::pan, Pts { { 0.0f, 0.5f, 0.0f }, { 0.12f, 0.0f, 0.0f }, { 0.25f, 1.0f, 0.0f }, { 0.4f, 0.2f, 0.0f },
                                              { 0.55f, 0.8f, 0.0f }, { 0.7f, 0.35f, 0.0f }, { 0.85f, 0.65f, 0.0f }, { 1.0f, 0.5f, 0.0f } }));

        v.push_back (B ("Feedback Swells", c, "Regeneration builds toward the bar line and is cut back, again and again.")
                        .mode (M::bbd).div (Div::n8d).fb (50).mix (32).a (70).b (20)
                        .lane (L::feedback, S::custom, 80, Div::bar2)
                        .curve (L::feedback, Pts { { 0.0f, 0.3f, 0.5f }, { 0.7f, 0.98f, 0.0f }, { 0.72f, 0.2f, 0.0f }, { 1.0f, 0.3f, 0.0f } }));

        v.push_back (B ("Filter Staircase", c, "Sixteenth-note repeats with a new resonant filter setting on every eighth.")
                        .mode (M::digital).div (Div::n16).fb (60).mix (30).a (50).b (50).cut (60, 16000, 56)
                        .lane (L::filter, S::sampleHold, -62, Div::n8));

        v.push_back (B ("Pitch Arpeggiator", c, "A drawn staircase transposes the echoes: root, third, fifth, octave, once per bar.")
                        .mode (M::digital).div (Div::n16).fb (34).mix (34).a (50).b (50)
                        .lane (L::pitch, S::custom, 100, Div::bar1)
                        .curve (L::pitch, Pts { { 0.0f, 0.5f, 0.0f }, { 0.249f, 0.5f, 0.0f }, { 0.25f, 0.6667f, 0.0f }, { 0.499f, 0.6667f, 0.0f },
                                                { 0.5f, 0.7917f, 0.0f }, { 0.749f, 0.7917f, 0.0f }, { 0.75f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }));

        v.push_back (B ("Flip-Flop Gate", c, "The echoes are switched on and off every sixteenth: a hard rhythmic gate on the repeats only.")
                        .mode (M::digital).div (Div::n4).fb (55).mix (40).a (50).b (45).cut (100, 9000)
                        .lane (L::mix, S::square, -40, Div::n16));

        v.push_back (B ("Tape Scrub", c, "A restless hand on the reel: the speed lurches somewhere new every moment.")
                        .mode (M::tape).div (Div::n8).fb (58).mix (34).a (20).b (30).inertia (62).cut (80, 10000)
                        .laneHz (L::speed, S::random, 72, 1.5f));

        v.push_back (B ("Stereo Chase", c, "Left and right speed up and slow down in opposition, chasing each other across the field.")
                        .mode (M::digital).div (Div::n8).fb (60).mix (30).a (50).b (50)
                        .lane (L::speed, S::sine, 20, Div::bar1, 0, 180)
                        .lane (L::pan, S::triangle, 60, Div::n2, 0, 180));

        v.push_back (B ("Breathing Room", c, "A diffused space that inhales and exhales: mix and feedback rise and fall out of step.")
                        .mode (M::diffused).div (Div::n4).fb (60).mix (32).a (55).b (60).cut (120, 7000)
                        .lane (L::mix, S::sine, 28, Div::bar2)
                        .lane (L::feedback, S::sine, 40, Div::bar2, 90));

        v.push_back (B ("Accelerando", c, "Over four bars the echoes speed up from half time to double time, then start over.")
                        .mode (M::digital).div (Div::n4).fb (70).mix (32).a (50).b (50).inertia (45)
                        .lane (L::speed, S::custom, 100, Div::bar4)
                        .curve (L::speed, Pts { { 0.0f, 0.0f, 0.3f }, { 1.0f, 1.0f, 0.0f } }));

        v.push_back (B ("Ritardando", c, "A flurry of fast tape repeats that slows to a crawl every two bars.")
                        .mode (M::tape).div (Div::n16).fb (70).mix (32).a (20).b (30).inertia (55).cut (110, 12000)
                        .lane (L::speed, S::custom, 100, Div::bar2)
                        .curve (L::speed, Pts { { 0.0f, 1.0f, -0.3f }, { 1.0f, 0.0f, 0.0f } }));

        v.push_back (B ("Wobble Echo", c, "A resonant filter wobbling in triplets over quarter-note repeats.")
                        .mode (M::digital).div (Div::n4).fb (58).mix (32).a (57.5).b (50).cut (40, 18000, 60)
                        .lane (L::filter, S::sine, -72, Div::n8t));

        v.push_back (B ("Six Lane Highway", c, "All six MOTION lanes at once, each on its own clock.")
                        .mode (M::tape).div (Div::n8d).fb (55).mix (32).a (20).b (25)
                        .lane (L::speed, S::triangle, 16, Div::bar4)
                        .lane (L::pan, S::sine, 60, Div::bar1d)
                        .lane (L::feedback, S::random, 30, Div::bar2)
                        .lane (L::filter, S::sine, -30, Div::bar8)
                        .lane (L::pitch, S::custom, 100, Div::bar2)
                        .curve (L::pitch, Pts { { 0.0f, 0.5f, 0.0f }, { 0.499f, 0.5f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } })
                        .lane (L::mix, S::sine, 20, Div::bar4, 180));

        v.push_back (B ("Sidechain Pump", c, "The echoes duck on every beat and swell back in, like a compressor keyed to the kick.")
                        .mode (M::digital).div (Div::n8).fb (46).mix (40).a (50).b (42)
                        .lane (L::mix, S::saw, 90, Div::n4));

        v.push_back (B ("Random Walk", c, "Speed, position and tone each wander at their own unhurried pace.")
                        .mode (M::warmAnalog).div (Div::n8).fb (58).mix (32).a (50).b (30)
                        .laneHz (L::speed, S::random, 20, 0.4f)
                        .laneHz (L::pan, S::random, 90, 0.7f)
                        .laneHz (L::filter, S::random, 45, 0.25f));

        v.push_back (B ("Bullet Time", c, "Normal speed, then everything drops into slow motion and gradually recovers.")
                        .mode (M::tape).div (Div::n8).fb (68).mix (36).a (25).b (35).inertia (85).cut (100, 9000)
                        .lane (L::speed, S::custom, 100, Div::bar4)
                        .curve (L::speed, Pts { { 0.0f, 0.5f, 0.0f }, { 0.4f, 0.5f, 0.0f }, { 0.45f, 0.0f, 0.0f },
                                                { 0.8f, 0.0f, 0.4f }, { 1.0f, 0.5f, 0.0f } }));

        v.push_back (B ("Freehand Sweep", c, "A hand-drawn filter contour - closing, opening, thinning, home again - while the mix slowly swells.")
                        .mode (M::bbd).div (Div::n8).fb (58).mix (30).a (72).b (18)
                        .lane (L::filter, S::custom, 80, Div::bar2)
                        .curve (L::filter, Pts { { 0.0f, 0.5f, 0.0f }, { 0.3f, 0.1f, 0.4f }, { 0.6f, 0.5f, 0.0f }, { 0.85f, 0.85f, -0.3f }, { 1.0f, 0.5f, 0.0f } })
                        .lane (L::mix, S::custom, 30, Div::bar4)
                        .curve (L::mix, Pts { { 0.0f, 0.2f, 0.4f }, { 0.75f, 0.9f, 0.0f }, { 1.0f, 0.2f, 0.0f } }));

        v.push_back (B ("Dervish", c, "Sixteenths spinning from side to side four times a second.")
                        .mode (M::digital).div (Div::n16).fb (66).mix (30).a (50).b (50).cut (150, 10000)
                        .laneHz (L::pan, S::sine, 100, 4.0f));
    }

    // ========================================================================
    void extremeFeedback (Presets& v)
    {
        const Cat c = Cat::extremeFeedback;

        v.push_back (B ("Infinite Corridor", c, "Sound enters and never leaves: an endless hallway of diffuse reflections.")
                        .mode (M::diffused).div (Div::n2).infinite().fb (90).mix (36).a (86).b (80).cut (200, 5000, 14)
                        .orbit (40, 0.07f, O::circle).mod (18, 0.12f));

        v.push_back (B ("Frozen Echo", c, "Infinite regeneration through a spectral hold: every note is suspended in ice.")
                        .mode (M::spectral).div (Div::n4).infinite().fb (90).mix (28).a (50).b (100).diff (60).cut (200, 6000));

        v.push_back (B ("Self-Oscillator", c, "Feedback past unity: play a note and the loop keeps singing by itself.")
                        .mode (M::warmAnalog).ms (140).fb (116).mix (28).a (60).b (30).sat (85, ST::tube).cut (100, 3000, 42).out (-3));

        v.push_back (B ("Dub Chamber", c, "Two tape heads at the edge of runaway, filtered and saturated.")
                        .mode (M::tapeEcho).div (Div::n8d).fb (99).mix (28).a (50).b (40).cut (180, 2800, 36).sat (78).diff (30).out (-3));

        v.push_back (B ("Feedback Howl", c, "A short, resonant bucket-brigade loop driven well past unity.")
                        .mode (M::bbd).ms (62).fb (121).mix (26).a (40).b (60).sat (90).cut (200, 2000, 72).out (-4));

        v.push_back (B ("Runaway Train", c, "Regeneration over 100 % while the tape slowly accelerates.")
                        .mode (M::tape).div (Div::n8).fb (106).mix (30).a (30).b (40).sat (56).inertia (60).cut (80, 6000)
                        .lane (L::speed, S::saw, 60, Div::bar4));

        v.push_back (B ("Seraphim", c, "Infinite octave shimmer: a choir that keeps climbing and never fades.")
                        .mode (M::shimmer).div (Div::n2).infinite().fb (90).mix (38).a (62).b (100).pitch (12).cut (250, 10000));

        v.push_back (B ("Eternal Drone", c, "An infinite, slowly breathing bed that sinks toward the sub-bass.")
                        .mode (M::shimmer).div (Div::n4).infinite().fb (90).mix (38).a (35).b (100).pitch (-12).cut (40, 2400)
                        .lane (L::filter, S::sine, -30, Div::bar8));

        v.push_back (B ("Endless Overdub", c, "Unity feedback on a one-bar loop: everything you play stays, layer on layer.")
                        .mode (M::digital).div (Div::bar1).fb (100).mix (42).a (50).b (50).cut (30, 18000));

        v.push_back (B ("Resonant Scream", c, "A 4 ms wave-folded loop screaming at its filter peak.")
                        .mode (M::digital).ms (4.2f).fb (112).mix (30).a (50).b (50).sat (80, ST::fold).cut (300, 1500, 92).out (-9));

        v.push_back (B ("Spring Tank Abuse", c, "A tiny diffusion loop fed back over unity: kick the reverb tank.")
                        .mode (M::diffused).ms (36).fb (102).mix (28).a (18).b (100).sat (82).cut (150, 5000, 20).out (-3));

        v.push_back (B ("Supernova", c, "An infinite tape loop collapsing inward while it spirals around the room.")
                        .mode (M::tape).div (Div::n4).infinite().fb (90).mix (32).a (40).b (50).collapse (90).orbit (100, 0.4f, O::spiral)
                        .sat (50).warp (15).cut (100, 7000));

        v.push_back (B ("Disintegration Loop", c, "A long loop that almost never fades but loses a little of itself on every pass.")
                        .mode (M::degraded).div (Div::n2d).fb (98).mix (40).a (55).b (60).tapeMem (50));

        v.push_back (B ("Feedback Choir", c, "Over-unity octave feedback: voices stack upward until they vanish above hearing.")
                        .mode (M::pitch).div (Div::n4).fb (104).mix (32).a (50).b (60).pitch (12).sat (50).cut (200, 6000));

        v.push_back (B ("Infinite Granular Cloud", c, "Grains re-granulated for ever: a texture that keeps rearranging itself.")
                        .mode (M::granular).div (Div::n4).infinite().fb (90).mix (38).a (70).b (80).spread (100).diff (40).rnd (40).cut (100, 8000));

        v.push_back (B ("Avalanche", c, "Six nested echo scales held at infinite sustain: each note starts a slide that never stops.")
                        .mode (M::fractal).div (Div::n2).infinite().fb (95).mix (34).a (100).b (50).sat (48).cut (100, 7000).spread (100));

        v.push_back (B ("Dub Siren", c, "A 5 ms loop pushed into self-oscillation and swept up and down: the sound-system siren.")
                        .mode (M::bbd).ms (5).fb (110).mix (26).a (30).b (20).sat (85, ST::tube).cut (120, 2600).inertia (10).out (-4)
                        .laneHz (L::speed, S::sine, 40, 0.5f));
    }

    Presets build()
    {
        Presets v;
        v.reserve (256);

        analogTape (v);
        vintageEchoes (v);
        modernDigital (v);
        rhythmic (v);
        pingPongStereo (v);
        ambient (v);
        experimental (v);
        glitch (v);
        pitchShimmer (v);
        vocal (v);
        guitar (v);
        cinematic (v);
        loFi (v);
        creativeMotion (v);
        extremeFeedback (v);

        return v;
    }
}

const std::vector<FactoryPreset>& getFactoryPresets()
{
    static const std::vector<FactoryPreset> presets = build();
    return presets;
}

} // namespace dlay
