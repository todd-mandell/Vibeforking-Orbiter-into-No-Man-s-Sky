#pragma once
#include "orbitersdk.h"
#include <string>

struct PlanetHazardProfile {
    double baseTemp;        // °C at surface (approx)
    double tempVariance;    // °C day/night swing
    double surfacePressure; // Pa at surface (approx)
    double surfaceRadiation;// 0–1 baseline radiation at surface
    double atmToxicity;     // 0–1 baseline atmospheric toxicity
    bool   hasAtmosphere;
    bool   corrosive;       // true for acid atmospheres (e.g., Venus)
    bool   oceanWorld;      // true for subsurface/global oceans (e.g., Europa)
    bool   gasGiant;        // true for gas giants (Jupiter, Saturn, etc.)
};

// Get hazard profile for the current primary body (planet or moon).
// If body not recognized, falls back to a neutral default.
PlanetHazardProfile GetPlanetHazardProfile(OBJHANDLE hBody);
