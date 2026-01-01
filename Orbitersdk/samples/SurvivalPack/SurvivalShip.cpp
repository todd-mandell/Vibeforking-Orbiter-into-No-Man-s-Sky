#include "SurvivalShip.h"
#include <cmath>

static double Clamp(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

SurvivalShip::SurvivalShip(OBJHANDLE hVessel, int flightmodel)
    : VESSEL2(hVessel, flightmodel)
{
    hullIntegrity   = 1.0;
    internalPressure= 1.0e5;   // ~1 atm
    internalOxygen  = 3600.0;  // 1 hour
    powerLevel      = 1.0;

    radiationShield   = 0.8;
    thermalInsulation = 0.8;

    envPressure    = 0.0;
    envRadiation   = 0.0;
    envTemperature = 0.0;
    inAtmosphere   = false;
    inVacuum       = true;
    airlockOpen    = false;
}

void SurvivalShip::clbkSetClassCaps(FILEHANDLE cfg)
{
    SetEmptyMass(20000.0);
    SetSize(10.0);

    // Simple main engine
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
        envRadiation   = 2.0;
        envTemperature = -150.0;
        inVacuum       = true;
        return;
    }

    double radius = oapiGetSize(hRef);
    VECTOR3 gpos;
    Local2Global(_V(0,0,0), gpos);
    VECTOR3 cpos;
    oapiGetGlobalPos(hRef, &cpos);

    double r   = length(gpos - cpos);
    double alt = r - radius;

    ATMOSPHEREPARAM atm;
    bool hasAtm = GetAtmosphericParams(atm) != 0;
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

    // Radiation similar logic as EVA (simplified)
    double baseRad = 1.0;
    if (inVacuum) {
        baseRad = 2.0;
        if (alt > 1.0e6) baseRad = 3.0;
    } else {
        if (envPressure > 5.0e4) baseRad = 0.5;
        if (envPressure > 1.0e5) baseRad = 0.2;
    }
    envRadiation = baseRad;

    // Ambient temperature: use atmospheric if present, else simple
    if (inAtmosphere) {
        double T = GetAtmTemperature();
        envTemperature = T - 273.15;
    } else {
        envTemperature = -150.0;
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

        // Hit can slightly drop internal pressure
        internalPressure *= 0.9;
    }
}

void SurvivalShip::ApplyEnvironmentToShip(double simdt)
{
    if (hullIntegrity <= 0.0) {
        internalPressure *= (1.0 - 0.5 * simdt);
        if (internalPressure < 0.0) internalPressure = 0.0;
    }

    if (airlockOpen && inVacuum) {
        internalPressure -= 2.0e4 * simdt;
        if (internalPressure < 0.0) internalPressure = 0.0;
    }

    internalOxygen -= simdt * 1.0;
    if (internalOxygen < 0.0) internalOxygen = 0.0;

    double effectiveRad = envRadiation * (1.0 - radiationShield);
    if (effectiveRad > 0.1) {
        hullIntegrity -= simdt * 0.0005 * effectiveRad;
    }

    double tempDelta = envTemperature - 20.0;
    tempDelta *= (1.0 - thermalInsulation);
    if (std::fabs(tempDelta) > 50.0) {
        hullIntegrity -= simdt * 0.0005 * (std::fabs(tempDelta) - 50.0) / 50.0;
    }

    hullIntegrity = Clamp(hullIntegrity, 0.0, 1.0);
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
        if (airlockOpen)
            oapiWriteLog("SurvivalShip: Airlock opened");
        else
            oapiWriteLog("SurvivalShip: Airlock closed");
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

    sprintf(buf, "Env Rad: %.2f  Temp: %.1f C", envRadiation, envTemperature);
    TextOut(hDC, 20, 100, buf, (int)strlen(buf));

    sprintf(buf, "Airlock: %s", airlockOpen ? "OPEN" : "CLOSED");
    TextOut(hDC, 20, 120, buf, (int)strlen(buf));
}
