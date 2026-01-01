#include "EVA.h"
#include <cmath>

static double Clamp(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

EVA::EVA(OBJHANDLE hVessel, int flightmodel)
    : VESSEL2(hVessel, flightmodel)
{
    suitOxygen    = 600.0;
    suitPower     = 1.0;
    suitIntegrity = 1.0;
    health        = 1.0;

    maxSafePressure       = 5.0e5;
    maxSafeTempLow        = -40.0;
    maxSafeTempHigh       =  60.0;
    radiationShieldFactor = 0.7;
    toxicityProtection    = 0.8;
    thermalInsulation     = 0.7;
    suitInternalTemp      = 20.0;
    suitCoolingPower      = 5.0;
    suitHeatingPower      = 5.0;

    miningRange   = 3.0;
    inMiningRange = false;
    resourcePos   = _V(10, 0, 10);

    inventory["Crystal"] = 0;

    envPressure    = 0.0;
    envRadiation   = 0.0;
    envToxicity    = 0.0;
    envTemperature = 0.0;
    underwater     = false;
    inAtmosphere   = false;
    inVacuum       = true;
}

void EVA::clbkSetClassCaps(FILEHANDLE cfg)
{
    SetEmptyMass(120.0);
    SetSize(0.5);

    // Simple jetpack thruster
    THRUSTER_HANDLE th = CreateThruster(_V(0,0,0), _V(0,1,0), 50);
    AddExhaust(th, 0.1, 0.1);
}

bool EVA::CheckProximity(const VECTOR3 &target, double range)
{
    VECTOR3 pos;
    Local2Global(_V(0,0,0), pos);
    double dist = length(pos - target);
    return (dist <= range);
}

void EVA::MineResource()
{
    if (!inMiningRange) return;
    if (health <= 0.0 || suitIntegrity <= 0.0) return;

    inventory["Crystal"] += 1;
    oapiWriteLog("EVA: Mined 1 Crystal");
}

// Temperature models

double EVA::ComputeVacuumTemperature()
{
    OBJHANDLE hSun = oapiGetObjectByName("Sun");
    if (!hSun) return -270.0;

    VECTOR3 myPos, sunPos;
    Local2Global(_V(0,0,0), myPos);
    oapiGetGlobalPos(hSun, &sunPos);

    double dist = length(myPos - sunPos);
    double AU   = 1.496e11;

    double flux = 1361.0 * (AU * AU) / (dist * dist);
    double sigma = 5.670374419e-8;
    double tempK = pow(flux / sigma, 0.25);
    return tempK - 273.15;
}

double EVA::ComputeAtmosphereTemperature()
{
    double T = GetAtmTemperature(); // K
    double tempC = T - 273.15;
    double moderated = suitInternalTemp + (tempC - suitInternalTemp) * (1.0 - thermalInsulation);
    return moderated;
}

double EVA::ComputeWaterTemperature(double depth)
{
    double temp = 15.0 - depth * 0.02;
    if (temp < 2.0) temp = 2.0;
    double moderated = suitInternalTemp + (temp - suitInternalTemp) * 0.8;
    return moderated;
}

// Environment

void EVA::UpdateEnvironment(double simdt)
{
    underwater   = false;
    inAtmosphere = false;
    inVacuum     = false;

    OBJHANDLE hRef = GetSurfaceRef();
    if (!hRef) {
        envPressure    = 0.0;
        envRadiation   = 2.0;
        envToxicity    = 0.0;
        inVacuum       = true;
        envTemperature = ComputeVacuumTemperature();
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
            if (alt < 0.0 && envPressure > 1.0e5) {
                underwater = true;
            }
        } else {
            inVacuum = true;
        }
    } else {
        envPressure = 0.0;
        inVacuum    = true;
    }

    double baseRad = 1.0;
    if (inVacuum) {
        baseRad = 2.0;
        if (alt > 1.0e6) baseRad = 3.0;
    } else {
        if (envPressure > 5.0e4) baseRad = 0.5;
        if (envPressure > 1.0e5) baseRad = 0.2;
    }
    envRadiation = baseRad;

    if (inAtmosphere) envToxicity = 1.0;
    else              envToxicity = 0.0;
    if (underwater) {
        envToxicity = std::max(envToxicity, 0.5);
        envPressure = std::max(envPressure, 2.0e5);
    }

    if (inVacuum)      envTemperature = ComputeVacuumTemperature();
    else if (underwater) envTemperature = ComputeWaterTemperature(-alt);
    else               envTemperature = ComputeAtmosphereTemperature();
}

// Micrometeorites — probabilistic hits in vacuum

void EVA::ApplyRandomMicrometeorites(double simdt)
{
    if (!inVacuum) return;
    if (health <= 0.0) return;

    // Chance per second (tune)
    double hitsPerHour = 0.1;
    double p = hitsPerHour / 3600.0 * simdt;

    double r = (double)rand() / (double)RAND_MAX;
    if (r < p) {
        double dmg = 0.1; // 10% suit damage
        suitIntegrity -= dmg;
        if (suitIntegrity < 0.0) suitIntegrity = 0.0;
        oapiWriteLog("EVA: Micrometeorite hit!");
    }
}

void EVA::ApplyEnvironmentEffects(double simdt)
{
    if (health <= 0.0 || suitIntegrity <= 0.0)
        return;

    // Oxygen
    double oxyRate = 1.0;
    if (underwater) oxyRate *= 1.2;
    suitOxygen -= simdt * oxyRate;
    if (suitOxygen < 0.0) suitOxygen = 0.0;

    if (suitOxygen <= 0.0) {
        health -= simdt * 0.02;
    }

    // Pressure
    if (envPressure > maxSafePressure) {
        double over = envPressure - maxSafePressure;
        double dmgRate = over / maxSafePressure;
        suitIntegrity -= simdt * 0.01 * dmgRate;
        health        -= simdt * 0.01 * dmgRate;
    }

    // Radiation
    double effectiveRad = envRadiation * (1.0 - radiationShieldFactor);
    if (effectiveRad > 0.1) {
        health -= simdt * 0.001 * effectiveRad;
    }

    // Toxicity
    double effectiveTox = envToxicity * (1.0 - toxicityProtection);
    if (effectiveTox > 0.1) {
        health        -= simdt * 0.002 * effectiveTox;
        suitIntegrity -= simdt * 0.001 * effectiveTox;
    }

    // Temperature
    double delta = envTemperature - suitInternalTemp;
    if (delta > 0.0)      delta -= suitCoolingPower;
    else if (delta < 0.0) delta += suitHeatingPower;

    if (delta > maxSafeTempHigh) {
        double heatStress = (delta - maxSafeTempHigh) * 0.001;
        health        -= heatStress * simdt;
        suitIntegrity -= heatStress * 0.5 * simdt;
    }
    if (delta < maxSafeTempLow) {
        double coldStress = (maxSafeTempLow - delta) * 0.001;
        health        -= coldStress * simdt;
        suitIntegrity -= coldStress * 0.5 * simdt;
    }

    suitIntegrity = Clamp(suitIntegrity, 0.0, 1.0);
    health        = Clamp(health, 0.0, 1.0);
}

void EVA::clbkPreStep(double simt, double simdt, double mjd)
{
    UpdateEnvironment(simdt);
    ApplyEnvironmentEffects(simdt);
    ApplyRandomMicrometeorites(simdt);

    inMiningRange = CheckProximity(resourcePos, miningRange);
}

int EVA::clbkConsumeBufferedKey(DWORD key, bool down, char *kstate)
{
    if (!down) return 0;
    if (health <= 0.0 || suitIntegrity <= 0.0) return 0;

    if (key == OAPI_KEY_M) {
        MineResource();
        return 1;
    }
    return 0;
}

void EVA::clbkDrawHUD(int mode, const HUDPAINTSPEC *hps, HDC hDC)
{
    char buf[256];

    sprintf(buf, "O2: %.0f sec", suitOxygen);
    TextOut(hDC, 20, 20, buf, (int)strlen(buf));

    sprintf(buf, "Suit: %.0f%%", suitIntegrity * 100.0);
    TextOut(hDC, 20, 40, buf, (int)strlen(buf));

    sprintf(buf, "Health: %.0f%%", health * 100.0);
    TextOut(hDC, 20, 60, buf, (int)strlen(buf));

    sprintf(buf, "P: %.1f kPa", envPressure / 1000.0);
    TextOut(hDC, 20, 80, buf, (int)strlen(buf));

    sprintf(buf, "Rad: %.2f Tox: %.2f", envRadiation, envToxicity);
    TextOut(hDC, 20, 100, buf, (int)strlen(buf));

    sprintf(buf, "Temp: %.1f C", envTemperature);
    TextOut(hDC, 20, 120, buf, (int)strlen(buf));

    sprintf(buf, "Crystals: %d", inventory.at("Crystal"));
    TextOut(hDC, 20, 140, buf, (int)strlen(buf));

    if (underwater)
        TextOut(hDC, 20, 160, "UNDERWATER", 10);
    else if (inVacuum)
        TextOut(hDC, 20, 160, "VACUUM", 6);
    else if (inAtmosphere)
        TextOut(hDC, 20, 160, "ATMOSPHERE", 10);

    if (inMiningRange)
        TextOut(hDC, 20, 180, "Press M to Mine", 14);
}
