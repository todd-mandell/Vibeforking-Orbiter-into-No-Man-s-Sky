#include "SurvivalShip.h"
#include <cmath>
#include <cstring>

static double Clamp(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

SurvivalShip::SurvivalShip(OBJHANDLE hVessel, int flightmodel)
    : VESSEL2(hVessel, flightmodel)
{
    hullIntegrity    = 1.0;
    internalPressure = 1.0e5;
    internalOxygen   = 3600.0;
    powerLevel       = 1.0;

    radiationShield   = 0.8;
    thermalInsulation = 0.8;

    envPressure    = 0.0;
    envRadiation   = 0.0;
    envTemperature = 0.0;
    inAtmosphere   = false;
    inVacuum       = true;

    airlockOpen    = false;

    currentProfile = PlanetHazardProfile{};
}

void SurvivalShip::clbkSetClassCaps(FILEHANDLE cfg)
{
    SetEmptyMass(20000.0);
    SetSize(10.0);

    THRUSTER_HANDLE th = CreateThruster(_V(0,0,-5), _V(0,0,1), 2.0e5);
    AddExhaust(th, 2.0, 0.5);
}

void SurvivalShip::UpdateEnvironment(double simdt)
{
    inAtmosphere = false;
    inVacuum     = false;

    OBJHANDLE hRef = GetSurfaceRef();
    if (!hRef) {
        envPressure    = 0.0;
        envRadiation   = 0.7;
        envTemperature = -150.0;
        inVacuum       = true;
        currentProfile = PlanetHazardProfile{};
        return;
    }

    currentProfile = GetPlanetHazardProfile(hRef);

    double radius = oapiGetSize(hRef);
    VECTOR3 gpos;
    Local2Global(_V(0,0,0), gpos);
    VECTOR3 cpos;
    oapiGetGlobalPos(hRef, &cpos);

    double r   = length(gpos - cpos);
    double alt = r - radius;

    ATMOSPHEREPARAM atm;
    bool hasAtm = GetAtmosphericParams(atm) != 0 && currentProfile.hasAtmosphere;
    if (hasAtm) {
        envPressure = GetAtmPressure();
        if (envPressure > 0.0) {
            inAtmosphere = true;
        } else {
            inVacuum = true;
        }
    } else {
        envPressure = 0.0;
        inVacuum    = true;
    }

    // Radiation
    double baseRad = currentProfile.surfaceRadiation;
    if (currentProfile.gasGiant) {
        if (alt < 0) baseRad = 1.0;
    } else {
        if (alt > 1.0e6) baseRad += 0.2;
    }
    envRadiation = Clamp(baseRad, 0.0, 1.0);

    // Temperature
    if (inAtmosphere && currentProfile.hasAtmosphere) {
        double T = GetAtmTemperature();
        double tempC = T - 273.15;
        // Blend with profile baseline
        envTemperature = 0.5 * tempC + 0.5 * currentProfile.baseTemp;
    } else {
        envTemperature = currentProfile.baseTemp;
    }
}

void SurvivalShip::ApplyRandomMicrometeorites(double simdt)
{
    if (!inVacuum) return;
    if (hullIntegrity <= 0.0) return;

    double hitsPerHour = 0.05;
    double p = hitsPerHour / 3600.0 * simdt;
    double r = (double)rand() / (double)RAND_MAX;
    if (r < p) {
        double dmg = 0.05;
        hullIntegrity -= dmg;
        if (hullIntegrity < 0.0) hullIntegrity = 0.0;
        oapiWriteLog("SurvivalShip: Micrometeorite hit!");

        internalPressure *= 0.9;
    }
}

void SurvivalShip::ApplyEnvironmentToShip(double simdt)
{
    // Hull breach
    if (hullIntegrity <= 0.0) {
        internalPressure *= (1.0 - 0.5 * simdt);
        if (internalPressure < 0.0) internalPressure = 0.0;
    }

    // Airlock equalization with environment (dangerous on bad planets)
    if (airlockOpen) {
        double rate = 0.5;
        double diff = envPressure - internalPressure;
        internalPressure += diff * rate * simdt;
        if (internalPressure < 0.0) internalPressure = 0.0;
    }

    internalOxygen -= simdt * 1.0;
    if (internalOxygen < 0.0) internalOxygen = 0.0;

    // Radiation vs hull
    double effectiveRad = envRadiation * (1.0 - radiationShield);
    if (effectiveRad > 0.1) {
        hullIntegrity -= simdt * 0.0005 * effectiveRad;
    }

    // Corrosive atmospheres (like Venus) slowly damage hull
    if (currentProfile.corrosive && inAtmosphere) {
        hullIntegrity -= simdt * 0.0005;
    }

    // Gas giants: impossible environment
    if (currentProfile.gasGiant && inAtmosphere) {
        hullIntegrity -= simdt * 0.01;
    }

    // Temperature stress vs internal ~20°C
    double tempDelta = envTemperature - 20.0;
    tempDelta *= (1.0 - thermalInsulation);
    if (std::fabs(tempDelta) > 50.0) {
        hullIntegrity -= simdt * 0.0005 *
                         (std::fabs(tempDelta) - 50.0) / 50.0;
    }

    hullIntegrity = Clamp(hullIntegrity, 0.0, 1.0);
}

void SurvivalShip::SpawnEVA()
{
    if (internalPressure < 5.0e4) {
        oapiWriteLog("SurvivalShip: Internal pressure too low to EVA");
        return;
    }

    // If atmosphere is highly toxic or corrosive, warn (still allow for now).
    if (currentProfile.corrosive || currentProfile.atmToxicity > 0.8) {
        oapiWriteLog("SurvivalShip: WARNING - EVA into lethal atmosphere");
    }

    VECTOR3 airlockLocal = _V(0, 0, -5);
    VECTOR3 airlockGlobal;
    Local2Global(airlockLocal, airlockGlobal);

    VECTOR3 vel;
    GetGlobalVel(vel);

    char name[64];
    sprintf(name, "EVA-%d", rand() % 10000);

    VESSELSTATUS vs;
    memset(&vs, 0, sizeof(vs));
    vs.version = 2;
    vs.rbody   = GetSurfaceRef();
    vs.rpos    = airlockGlobal;
    vs.rvel    = vel;
    vs.arot    = _V(0,0,0);
    vs.status  = 0;

    OBJHANDLE hEVA = oapiCreateVesselEx(name, "EVA", &vs);
    if (hEVA) {
        oapiWriteLog("SurvivalShip: EVA spawned");
        oapiSetFocusObject(hEVA);
    }

    airlockOpen = true;
}

void SurvivalShip::clbkPreStep(double simt, double simdt, double mjd)
{
    UpdateEnvironment(simdt);
    ApplyEnvironmentToShip(simdt);
    ApplyRandomMicrometeorites(simdt);
}

int SurvivalShip::clbkConsumeBufferedKey(DWORD key, bool down, char *kstate)
{
    if (!down) return 0;

    if (key == OAPI_KEY_A) {
        airlockOpen = !airlockOpen;
        oapiWriteLog(airlockOpen ?
                     "SurvivalShip: Airlock opened" :
                     "SurvivalShip: Airlock closed");
        return 1;
    }

    if (key == OAPI_KEY_E) {
        SpawnEVA();
        return 1;
    }

    return 0;
}

void SurvivalShip::clbkDrawHUD(int mode, const HUDPAINTSPEC *hps, HDC hDC)
{
    char buf[256];

    sprintf(buf, "Hull: %.0f%%", hullIntegrity * 100.0);
    TextOut(hDC, 20, 20, buf, (int)strlen(buf));

    sprintf(buf, "Int P: %.1f kPa", internalPressure / 1000.0);
    TextOut(hDC, 20, 40, buf, (int)strlen(buf));

    sprintf(buf, "Int O2: %.0f sec", internalOxygen);
    TextOut(hDC, 20, 60, buf, (int)strlen(buf));

    sprintf(buf, "Env P: %.1f kPa", envPressure / 1000.0);
    TextOut(hDC, 20, 80, buf, (int)strlen(buf));

    sprintf(buf, "Env Rad: %.2f Temp: %.1f C", envRadiation, envTemperature);
    TextOut(hDC, 20, 100, buf, (int)strlen(buf));

    sprintf(buf, "Airlock: %s", airlockOpen ? "OPEN" : "CLOSED");
    TextOut(hDC, 20, 120, buf, (int)strlen(buf));

    TextOut(hDC, 20, 140, "E: EVA | A: Toggle airlock", 28);
}
