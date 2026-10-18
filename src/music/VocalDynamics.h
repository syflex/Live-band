#pragma once

#include <array>

namespace lb
{

// Follows how loud the singer is, against their own range over the session, and turns it
// into the band's dynamics.
//
// - Level: the level of voiced frames in dB, smoothed over smoothingSeconds, so that it falls
//   back after a loud passage as quickly as it rose.
// - Percentile: where that level sits in the session's own distribution of it (which fades
//   over historySeconds): 0.5 is the singer's usual level, 0.8 louder than four fifths of
//   the time. Each moment counts as a spread of spreadDb around its level, so that a very
//   even singer's tiny changes do not swing the band: it takes a few dB. Until
//   minVoicedSeconds of singing it stays at 0.5, and without singing it relaxes back.
// - Velocity scale: from minScale for the quietest singing, through 1 at the usual level,
//   to maxScale for the loudest.
// - Intense: after holdSeconds (two bars) above loudPercentile, the band plays its intense
//   variation; after holdSeconds below quietPercentile, or without singing, it calms down.
//   Breaths shorter than breathSeconds neither count nor reset the time.
//
// Real-time safe; used by one thread.
class VocalDynamics
{
public:
    struct Settings
    {
        double smoothingSeconds = 1.5;
        double historySeconds = 600.0;
        double minVoicedSeconds = 8.0;
        double spreadDb = 3.0;
        double relaxSeconds = 3.0;
        double breathSeconds = 1.0;
        double minScale = 0.8;
        double maxScale = 1.15;
        double loudPercentile = 0.8;
        double quietPercentile = 0.6;
    };

    void setSettings(const Settings& settings) noexcept { settings_ = settings; }
    const Settings& settings() const noexcept { return settings_; }
    void reset() noexcept;

    // How long the singer must stay loud (or quiet) to change the band: two bars.
    void setHoldSeconds(double seconds) noexcept { holdSeconds_ = seconds; }

    // One analysis frame: time in seconds, voicing and level (dBFS).
    void process(double seconds, bool voiced, float rmsDb) noexcept;

    double levelDb() const noexcept;
    double percentile() const noexcept { return percentile_; }
    double velocityScale() const noexcept;
    bool intense() const noexcept { return intense_; }
    double voicedSeconds() const noexcept { return voicedSeconds_; }

private:
    static constexpr int numBins = 180;
    static constexpr double lowestDb = -90.0;
    static constexpr double binDb = 0.5;

    Settings settings_;
    std::array<double, numBins> histogram_ {};
    double total_ = 0.0;
    double smoothedDb_ = lowestDb;
    bool haveLevel_ = false;
    double lastSeconds_ = -1.0;
    double voicedSeconds_ = 0.0;
    double unvoicedFor_ = 0.0;
    double percentile_ = 0.5;
    double holdSeconds_ = 4.6;
    double loudFor_ = 0.0;
    double quietFor_ = 0.0;
    bool intense_ = false;
};

} // namespace lb
