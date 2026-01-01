#include "EVA.h"
#include <cmath>
#include <cstring>

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

    maxSafePressure     = 5.0e5;
    maxSafeTempLow      = -40.0;
    maxSafeTempHigh     =  60.0;
    baseRadiationShield = 0.7;
    baseToxicProtection = 0.4;
    thermalInsulation   = 0.7;
    suitInternalTemp    = 20.0;
    suitCoolingPower    = 5.0;
    suitHeatingPower    = 5.0;

    toxicShieldCapacity   = 100.0;
    toxicShieldCharge     = 0.0;
    toxicShieldEfficiency = 0.5;
    toxicShieldDrainRate  = 2.0;
    toxicShieldOnline     = true;

    ion.capacity  = 200.0;
    ion.charge    = 0.0;
    ion.damaged   = false;
    ionLeakFactor = 0.02;

    miningRange   = 3.0;
    inMiningRange = false;
    resourcePos   = _V(10, 0, 10);

    inventory["Crystal"]  = 0;
    inventory["ION_CORE"] = 0;

    envPressure    = 0.0;
    envRadiation   = 0.0;
    envToxicity    = 0.0;
    envTemperature = 0.0;
    underwater     = false;
    inAtmosphere   = false;
    inVacuum       = true;

    currentProfile = PlanetHazardProfile{};
}

void EVA::clbkSetClassCaps(FILEHANDLE cfg)
{
    SetEmptyMass(120.0);
    SetSize(0.5);

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

    double flux  = 1361.0 * (AU * AU) / (dist * dist);
    double sigma = 5.670374419e-8;
    double tempK = pow(flux / sigma, 0.25);
    return tempK - 273.15;
}

double EVA::ComputeAtmosphereTemperature()
{
    double T = GetAtmTemperature(); // K
    double tempC_atm = T - 273.15;

    double temp = 0.5 * tempC_atm + 0.5 * currentProfile.baseTemp;

    VECTOR3 nml;
    GetSurfaceNormal(GetSurfaceRef(), GetLongitude(), GetLatitude(), nml);
    VECTOR3 sunDir;
    OBJHANDLE hSun = oapiGetObjectByName("Sun");
    if (hSun) {
        VECTOR3 sunPos, myPos;
        oapiGetGlobalPos(hSun, &sunPos);
        Local2Global(_V(0,0,0), myPos);
        sunDir = sunPos - myPos;
        sunDir /= length(sunDir);
        double dot = dotp(nml, sunDir);
        double factor = Clamp(dot, -1.0, 1.0);
        temp += currentProfile.tempVariance * factor * 0.5;
    }

    double moderated = suitInternalTemp +
                       (temp - suitInternalTemp) * (1.0 - thermalInsulation);
    return moderated;
}

double EVA::ComputeWaterTemperature(double depth)
{
    double temp = 4.0;
    if (currentProfile.oceanWorld) {
        temp = 0.0;
    }
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
        envPressure  = 0.0;
        envRadiation = 0.7;
        envToxicity  = 0.0;
        inVacuum     = true;
        currentProfile = PlanetHazardProfile{};
        envTemperature = ComputeVacuumTemperature();
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
            if (alt < 0.0 && currentProfile.oceanWorld) {
                underwater = true;
            }
        } else {
            inVacuum = true;
        }
    } else {
        envPressure = 0.0;
        inVacuum    = true;
    }

    double baseRad = currentProfile.surfaceRadiation;
    if (currentProfile.gasGiant) {
        if (alt < 0) baseRad = 1.0;
    } else {
        if (alt > 1.0e6) baseRad += 0.2;
    }
    envRadiation = Clamp(baseRad, 0.0, 1.0);

    if (inAtmosphere) {
        envToxicity = currentProfile.atmToxicity;
        if (currentProfile.corrosive) {
            envToxicity = 1.0;
        }
    } else {
        envToxicity = 0.0;
    }

    if (underwater) {
        envPressure = std::max(envPressure, 2.0e5);
    }

    if (inVacuum)         envTemperature = ComputeVacuumTemperature();
    else if (underwater)  envTemperature = ComputeWaterTemperature(-alt);
    else                  envTemperature = ComputeAtmosphereTemperature();
}

// Micrometeorites

void EVA::ApplyRandomMicrometeorites(double simdt)
{
    if (!inVacuum) return;
    if (health <= 0.0) return;

    double hitsPerHour = 0.1;
    double p = hitsPerHour / 3600.0 * simdt;
    double r = (double)rand() / (double)RAND_MAX;
    if (r < p) {
        double dmg = 0.1;
        suitIntegrity -= dmg;
        if (suitIntegrity < 0.0) suitIntegrity = 0.0;

        if (ion.charge > 0.0) {
            double pDamage = 0.3;
            double r2 = (double)rand() / (double)RAND_MAX;
            if (r2 < pDamage) {
                ion.damaged = true;
                oapiWriteLog("EVA: Micrometeorite hit ION core - damage detected");
            }
        } else {
            oapiWriteLog("EVA: Micrometeorite hit!");
        }
    }
}

// Toxic shield

void EVA::UpdateToxicShield(double simdt)
{
    if (!toxicShieldOnline) return;
    if (toxicShieldCharge <= 0.0) {
        toxicShieldCharge = 0.0;
        return;
    }

    if (envToxicity > 0.05) {
        double drain = toxicShieldDrainRate * envToxicity * simdt;
        toxicShieldCharge -= drain;
        if (toxicShieldCharge < 0.0) toxicShieldCharge = 0.0;
    }
}

// ION battery

bool EVA::ConsumeIonCharge(double amount)
{
    if (ion.charge < amount) return false;
    ion.charge -= amount;
    if (ion.charge < 0.0) ion.charge = 0.0;
    return true;
}

void EVA::ApplyIonBatteryEffects(double simdt)
{
    if (ion.damaged && ion.charge > 0.0) {
        double leak   = ionLeakFactor * (ion.charge / ion.capacity);
        double radHit = leak * simdt;
        health        -= radHit * 0.5;
        suitIntegrity -= radHit * 0.5;
    }

    if (suitIntegrity <= 0.0 && ion.charge > ion.capacity * 0.5) {
        double burst = (ion.charge / ion.capacity);
        double dmg   = burst * 0.5;
        health -= dmg;
        ion.charge = 0.0;
        oapiWriteLog("EVA: ION core catastrophic failure - massive radiation burst");
    }

    health        = Clamp(health, 0.0, 1.0);
    suitIntegrity = Clamp(suitIntegrity, 0.0, 1.0);
}

void EVA::RechargeToxicShieldFromIon()
{
    const double ionPerChunk    = 10.0;
    const double shieldPerChunk = 25.0;

    if (toxicShieldCharge >= toxicShieldCapacity) {
        oapiWriteLog("EVA: Toxic shield already full");
        return;
    }
    if (!ConsumeIonCharge(ionPerChunk)) {
        oapiWriteLog("EVA: Not enough ION charge to recharge shield");
        return;
    }

    toxicShieldCharge += shieldPerChunk;
    if (toxicShieldCharge > toxicShieldCapacity)
        toxicShieldCharge = toxicShieldCapacity;

    oapiWriteLog("EVA: Toxic shield recharged using ION core");
}

void EVA::CraftIonCell()
{
    int crystals = inventory["Crystal"];
    if (crystals < 5) {
        oapiWriteLog("EVA: Not enough Crystals to craft ION charge");
        return;
    }

    inventory["Crystal"] = crystals - 5;

    double addCharge = 20.0;
    ion.charge += addCharge;
    if (ion.charge > ion.capacity) ion.charge = ion.capacity;

    inventory["ION_CORE"] += 1;

    oapiWriteLog("EVA: Crafted ION charge from Crystals");
}

// Gravity effects

void EVA::ApplyGravityEffects(double simdt)
{
    VECTOR3 gvec;
    GetGravityVector(gvec);
    double g = length(gvec); // m/s^2

    // Micro-g (<0.5): slow health degradation
    if (g < 0.5) {
        double dmg = (0.5 - g) * 0.0001;
        health -= dmg * simdt;
    }

    // High-g (12–25): strain
    if (g > 12.0 && g <= 25.0) {
        double excess = g - 12.0;
        health        -= simdt * 0.001 * excess;
        suitIntegrity -= simdt * 0.0005 * excess;
        suitOxygen    -= simdt * 0.2   * excess;
        if (suitOxygen < 0.0) suitOxygen = 0.0;
    }

    // Extreme-g (>25): instant failure
    if (g > 25.0) {
        health        = 0.0;
        suitIntegrity = 0.0;
    }

    health        = Clamp(health, 0.0, 1.0);
    suitIntegrity = Clamp(suitIntegrity, 0.0, 1.0);
}

// Apply environment damage

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
        double over    = envPressure - maxSafePressure;
        double dmgRate = over / maxSafePressure;
        suitIntegrity -= simdt * 0.01 * dmgRate;
        health        -= simdt * 0.01 * dmgRate;
    }

    // Radiation
    double effectiveRad = envRadiation * (1.0 - baseRadiationShield);
    if (effectiveRad > 0.1) {
        health -= simdt * 0.001 * effectiveRad;
    }

    // Toxicity with shield
    double shieldFactor = 0.0;
    if (toxicShieldOnline && toxicShieldCharge > 0.0) {
        double frac = toxicShieldCharge / toxicShieldCapacity;
        shieldFactor = toxicShieldEfficiency * frac;
    }
    double totalToxProtection = baseToxicProtection + shieldFactor;
    if (totalToxProtection > 0.95) totalToxProtection = 0.95;

    double toxBase = envToxicity;
    if (currentProfile.corrosive && inAtmosphere) {
        toxBase = 1.0;
    }
    double effectiveTox = toxBase * (1.0 - totalToxProtection);
    if (effectiveTox > 0.05) {
        health        -= simdt * 0.002 * effectiveTox;
        suitIntegrity -= simdt * 0.0015 * effectiveTox;
    }

    if (currentProfile.gasGiant && inAtmosphere) {
        suitIntegrity -= simdt * 1.0;
        health        -= simdt * 1.0;
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

    // Gravity
    ApplyGravityEffects(simdt);

    suitIntegrity = Clamp(suitIntegrity, 0.0, 1.0);
    health        = Clamp(health, 0.0, 1.0);
}

// Re-enter ship

void EVA::TryReenterShip()
{
    OBJHANDLE self = GetHandle();
    VECTOR3 myPos;
    Local2Global(_V(0,0,0), myPos);

    OBJHANDLE bestShip = NULL;
    double    bestDist = 10.0;

    DWORD nv = oapiGetVesselCount();
    for (DWORD i = 0; i < nv; ++i) {
        OBJHANDLE hV = oapiGetVesselByIndex(i);
        if (hV == self) continue;

        VESSEL *v = oapiGetVesselInterface(hV);
        if (!v) continue;

        char classname[64] = {0};
        v->GetClassName(classname, 63);
        if (std::strcmp(classname, "SurvivalShip") != 0)
            continue;

        VECTOR3 pos;
        oapiGetGlobalPos(hV, &pos);
        double dist = length(pos - myPos);
        if (dist < bestDist) {
            bestDist = dist;
            bestShip = hV;
        }
    }

    if (bestShip) {
        oapiWriteLog("EVA: Re-entering SurvivalShip");
        oapiSetFocusObject(bestShip);
        oapiDeleteVessel(self);
    }
}

void EVA::clbkPreStep(double simt, double simdt, double mjd)
{
    UpdateEnvironment(simdt);

    UpdateToxicShield(simdt);
    ApplyIonBatteryEffects(simdt);

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

    if (key == OAPI_KEY_E) {
        TryReenterShip();
        return 1;
    }

    if (key == OAPI_KEY_C) {
        CraftIonCell();
        return 1;
    }

    if (key == OAPI_KEY_R) {
        RechargeToxicShieldFromIon();
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

    sprintf(buf, "Rad: %.2f  Tox: %.2f", envRadiation, envToxicity);
    TextOut(hDC, 20, 100, buf, (int)strlen(buf));

    sprintf(buf, "Temp: %.1f C", envTemperature);
    TextOut(hDC, 20, 120, buf, (int)strlen(buf));

    sprintf(buf, "Crystals: %d", inventory["Crystal"]);
    TextOut(hDC, 20, 140, buf, (int)strlen(buf));

    sprintf(buf, "ION: %.0f / %.0f%s", ion.charge, ion.capacity,
            ion.damaged ? " (DAMAGED)" : "");
    TextOut(hDC, 20, 160, buf, (int)strlen(buf));

    sprintf(buf, "ToxicShield: %.0f / %.0f", toxicShieldCharge, toxicShieldCapacity);
    TextOut(hDC, 20, 180, buf, (int)strlen(buf));

    // Show local gravity
    VECTOR3 gvec;
    GetGravityVector(gvec);
    double g = length(gvec);
    sprintf(buf, "g: %.2f m/s^2", g);
    TextOut(hDC, 20, 200, buf, (int)strlen(buf));

    if (underwater)
        TextOut(hDC, 20, 220, "UNDERWATER", 10);
    else if (inVacuum)
        TextOut(hDC, 20, 220, "VACUUM", 6);
    else if (inAtmosphere)
        TextOut(hDC, 20, 220, "ATMOSPHERE", 10);

    TextOut(hDC, 20, 240, "E: Re-enter | M: Mine", 24);
    TextOut(hDC, 20, 260, "C: Craft ION | R: Refill Toxic Shield", 39);
}
