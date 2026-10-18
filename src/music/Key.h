#pragma once

#include <string>

namespace lb
{

// A major or minor key. Tonic is a pitch class, 0 (C) to 11 (B); -1 means no key.
struct Key
{
    int tonic = -1;
    bool minor = false;

    bool isValid() const noexcept { return tonic >= 0 && tonic < 12; }
    // 0..11 for C major..B major, 12..23 for C minor..B minor.
    int index() const noexcept { return isValid() ? tonic + (minor ? 12 : 0) : -1; }
    static Key fromIndex(int index) noexcept { return index >= 0 && index < 24 ? Key { index % 12, index >= 12 } : Key {}; }
    Key transposed(int semitones) const noexcept { return isValid() ? Key { ((tonic + semitones) % 12 + 12) % 12, minor } : Key {}; }
    Key withMode(bool isMinor) const noexcept { return Key { tonic, isMinor }; }

    // "G major", "E minor", with the usual names for the black keys: C#, Eb, F#, Ab, Bb.
    std::string name() const;
    std::string shortName() const; // "G", "Em"

    bool operator==(const Key& other) const noexcept { return tonic == other.tonic && minor == other.minor; }
    bool operator!=(const Key& other) const noexcept { return !(*this == other); }
};

// The MIREX key score of an estimate against a reference, as mir_eval computes it: 1 for the
// same key, 0.5 for the key a fifth above (same mode), 0.3 for the relative key, 0.2 for the
// parallel key, else 0.
double mirexKeyScore(const Key& estimate, const Key& reference) noexcept;

} // namespace lb
