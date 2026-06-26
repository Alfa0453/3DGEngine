#pragma once 

#include <glm/glm.hpp>

namespace engine {

// Maps a normalised time-of-day to everything a scene needs to look like that
// time: the key light (sun by day, a dim cool moon by night), ambient fill, and
// the sky palette. Header-only — it's just math.
//
//   t in [0,1):  0.0 = midnight, 0.25 = sunrise, 0.5 = noon, 0.75 = sunset.
struct DayNightCycle {
    struct Sample {
        glm::vec3 keyLightDirection;    // travel direction (use as Light::direction)
        glm::vec3 keyLightColor;        // radiance (colour * intensity)
        glm::vec3 ambient;              // ambient fill
        float     dayFactor;            // 0 night .. 1 day

        // Sky.
        glm::vec3 sunToward, moonToward;   // directions toward sun / moon
        glm::vec3 horizon, zenith;         // gradient colours
        glm::vec3 sunDisc, moonDisc;       // disc/glow colours
    };

    static Sample At(float t) {
        const float TWO_PI = 6.2831853f;
        auto sstep = [](float a, float b, float x) {
            const float u = glm::clamp((x - a) / (b - a), 0.0f, 1.0f);
            return u * u * (3.0f - 2.0f * u);
        };

        // Sun arc: rises in the east, peaks at noon, sets in the west.
        const float ang = (t - 0.25f) * TWO_PI;
        const glm::vec3 sunToward = glm::normalize(glm::vec3(-std::cos(ang), std::sin(ang), 0.25f));
        const glm::vec3 moonToward = -sunToward;
        const float elev = sunToward.y;

        const float dayFactor = sstep(-0.10f, 0.20f, elev);
        const float highSun   = sstep(0.0f, 0.35f, elev);   // 0 at horizon, 1 overhead

        // Sun colour: warm/orange near the horizon, white overhead.
        const glm::vec3 sunWarm = glm::mix(glm::vec3(1.0f, 0.45f, 0.20f),
                                           glm::vec3(1.0f, 0.96f, 0.90f), highSun);
        
        // Sky palette: blend night -> day, then add a sunset tint to the horizon
        // while the sun is low but still up.  
        const glm::vec3 dayHorizon(0.55f, 0.62f, 0.78f), dayZenith(0.16f, 0.33f, 0.72f);
        const glm::vec3 nightHorizon(0.02f, 0.03f, 0.06f), nightZenith(0.005f, 0.01f, 0.03f);
        glm::vec3 horizon = glm::mix(nightHorizon, dayHorizon, dayFactor);
        glm::vec3 zenith  = glm::mix(nightZenith,  dayZenith,  dayFactor); 
        const float sunset = dayFactor * (1.0f - highSun);
        horizon = glm::mix(horizon, glm::vec3(0.95f, 0.5f, 0.3f), sunset * 0.6f);
        
        // Key light: the sun by day, a dim cool moon by night.
        const glm::vec3 sunRad  = sunWarm * (3.3f * dayFactor);
        const glm::vec3 moonRad = glm::vec3(0.35f, 0.42f, 0.6f) * 0.18f;

        Sample s;
        s.dayFactor        = dayFactor;
        s.sunToward        = sunToward;
        s.moonToward       = moonToward;
        s.horizon          = horizon;
        s.zenith           = zenith;
        s.sunDisc          = sunWarm;
        s.moonDisc         = glm::vec3(0.8f, 0.85f, 0.95f);
        s.keyLightColor    = sunRad + moonRad * (1.0f - dayFactor);
        s.keyLightDirection = -glm::normalize(glm::mix(moonToward, sunToward, dayFactor));
        s.ambient          = glm::mix(glm::vec3(0.010f, 0.012f, 0.020f),
                                      glm::vec3(0.050f, 0.055f, 0.070f), dayFactor);
        return s;
    }
};

} // namespace engine