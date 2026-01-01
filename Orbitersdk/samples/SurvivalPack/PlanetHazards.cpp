#include "PlanetHazards.h"
#include <map>

static std::map<std::string, PlanetHazardProfile> s_profiles;
static bool s_initialized = false;

static void InitProfiles()
{
    if (s_initialized) return;
    s_initialized = true;

    // Mercury
    s_profiles["Mercury"] = {
        170.0, 250.0, 0.0, 0.6, 0.0,
        false, false, false, false
    };

    // Venus
    s_profiles["Venus"] = {
        460.0, 5.0, 9.2e6, 0.5, 1.0,
        true,  true, false, false
    };

    // Earth
    s_profiles["Earth"] = {
        15.0, 20.0, 1.01e5, 0.1, 0.0,
        true,  false, false, false
    };

    // Moon
    s_profiles["Moon"] = {
        -20.0, 130.0, 0.0, 0.3, 0.0,
        false, false, false, false
    };

    // Mars
    s_profiles["Mars"] = {
        -60.0, 40.0, 600.0, 0.6, 0.7,
        true,  false, false, false
    };

    // Phobos / Deimos
    s_profiles["Phobos"] = {
        -60.0, 40.0, 0.0, 0.6, 0.0,
        false, false, false, false
    };
    s_profiles["Deimos"] = s_profiles["Phobos"];

    // Jupiter
    s_profiles["Jupiter"] = {
        -150.0, 20.0, 0.0, 1.0, 1.0,
        false, false, false, true
    };

    // Io
    s_profiles["Io"] = {
        -130.0, 40.0, 0.0, 0.9, 0.7,
        true,  false, false, false
    };

    // Europa
    s_profiles["Europa"] = {
        -160.0, 30.0, 0.0, 0.9, 0.0,
        false, false, true,  false
    };

    // Ganymede
    s_profiles["Ganymede"] = {
        -150.0, 30.0, 0.0, 0.6, 0.0,
        false, false, false, false
    };

    // Callisto
    s_profiles["Callisto"] = {
        -140.0, 30.0, 0.0, 0.4, 0.0,
        false, false, false, false
    };

    // Saturn
    s_profiles["Saturn"] = {
        -170.0, 20.0, 0.0, 0.7, 1.0,
        false, false, false, true
    };

    // Titan
    s_profiles["Titan"] = {
        -180.0, 10.0, 1.5e5, 0.3, 0.9,
        true,  false, false, false
    };

    // Uranus
    s_profiles["Uranus"] = {
        -200.0, 10.0, 0.0, 0.5, 1.0,
        false, false, false, true
    };

    // Neptune
    s_profiles["Neptune"] = {
        -210.0, 10.0, 0.0, 0.5, 1.0,
        false, false, false, true
    };

    // Pluto
    s_profiles["Pluto"] = {
        -230.0, 10.0, 1.0, 0.3, 0.5,
        true,  false, false, false
    };
}

PlanetHazardProfile GetPlanetHazardProfile(OBJHANDLE hBody)
{
    InitProfiles();

    char name[64] = {0};
    oapiGetObjectName(hBody, name, 63);
    std::string n(name);

    auto it = s_profiles.find(n);
    if (it != s_profiles.end()) {
        return it->second;
    }

    PlanetHazardProfile def;
    def.baseTemp         = -100.0;
    def.tempVariance     = 50.0;
    def.surfacePressure  = 0.0;
    def.surfaceRadiation = 0.5;
    def.atmToxicity      = 0.0;
    def.hasAtmosphere    = false;
    def.corrosive        = false;
    def.oceanWorld       = false;
    def.gasGiant         = false;
    return def;
}
