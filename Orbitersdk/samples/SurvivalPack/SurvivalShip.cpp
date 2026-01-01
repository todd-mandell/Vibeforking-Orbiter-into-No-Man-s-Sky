#include "SurvivalShip.h"
#include <cmath>
#include <cstring>
#include <algorithm>

// ======================================================================
// Utility
// ======================================================================

double SurvivalShip::Clamp(double v, double lo, double hi) const
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// ======================================================================
// Constructor
// ======================================================================

SurvivalShip::SurvivalShip(OBJHANDLE hVessel, int flightmodel)
    : VESSEL2(hVessel, flightmodel)
{
    // Core state
    hullIntegrity    = 1.0;
    internalPressure = 1.0e5;   // ~1 atm
    internalOxygen   = 3600.0;  // 1 hour
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

    // Propulsion
    ph_main        = nullptr;
    th_main        = nullptr;
    th_pulse       = nullptr;
    for (int i = 0; i < 6; ++i) th_rcs[i] = nullptr;

    maxFuelMass    = 20000.0; // kg of propellant
    baseIsp_main   = 450.0;   // s, efficient main
    baseIsp_rcs    = 280.0;   // s
    baseIsp_pulse  = 200.0;   // s, brute-force pulse
    mainMaxThrust  = 6.0e5;   // 600 kN (≈3 g on 20 t)
    pulseMaxThrust = 2.0e6;   // 2 MN

    hoverMode      = false;
    pulseActive    = false;

    // Thermal
    hullTemp          = 20.0;   // °C
    overheatThreshold = 900.0;  // °C
    meltThreshold     = 1500.0; // °C

    // Electronics / computer
    computerHealth         = 1.0;
    computerShield         = 0.9;   // most radiation deflected
    computerRepairProgress = 0.0;
    repairMaterials        = 0;
    repairingComputer      = false;
}

// ======================================================================
// Thrusters and fuel
// ======================================================================

void SurvivalShip::SetupRCS()
{
    double rcsThrust = 5000.0; // N

    // Positions and directions for 6 simple RCS thrusters
    VECTOR3 pos[6] = {
        _V( 2, 0, 0),
        _V(-2, 0, 0),
        _V(0,  2, 0),
        _V(0, -2, 0),
        _V(0, 0,  2),
        _V(0, 0, -2)
    };
    VECTOR3 dir[6] = {
        _V( 1, 0, 0),
        _V(-1, 0, 0),
        _V(0,  1, 0),
        _V(0, -1, 0),
        _V(0, 0,  1),
        _V(0, 0, -1)
    };

    double exhaustVel_rcs = baseIsp_rcs * 9.80665;

    for (int i = 0; i < 6; ++i) {
        th_rcs[i] = CreateThruster(pos[i], dir[i], rcsThrust);
        SetThrusterResource(th_rcs[i], ph_main);
        SetThrusterIsp(th_rcs[i], exhaustVel_rcs);
        AddExhaust(th_rcs[i], 0.5, 0.1);
    }
}

void SurvivalShip::SetupThrustersAndFuel()
{
    // Shared propellant resource
    ph_main = CreatePropellantResource(maxFuelMass);

    // Main engine
    VECTOR3 pos_main  = _V(0, 0, -5);
    VECTOR3 dir_main  = _V(0, 0, 1);
    th_main = CreateThruster(pos_main, dir_main, mainMaxThrust);
    SetThrusterResource(th_main, ph_main);
    SetThrusterIsp(th_main, baseIsp_main * 9.80665);
    AddExhaust(th_main, 2.0, 0.5);

    // Pulse engine (same direction, higher thrust, lower ISP)
    th_pulse = CreateThruster(pos_main, dir_main, pulseMaxThrust);
    SetThrusterResource(th_pulse, ph_main);
    SetThrusterIsp(th_pulse, baseIsp_pulse * 9.80665);
    AddExhaust(th_pulse, 3.0, 0.8);

    // RCS thrusters
    SetupRCS();
}

// ======================================================================
// Class caps
// ======================================================================

void SurvivalShip::clbkSetClassCaps(FILEHANDLE cfg)
{
    SetEmptyMass(20000.0);
    SetSize(10.0);

    SetPMI(_V(100.0, 100.0, 100.0));
    SetCrossSections(_V(50.0, 50.0, 50.0));

    // If you have a mesh named "SurvivalShip.msh", uncomment:
    // AddMesh("SurvivalShip");

    SetupThrustersAndFuel();
}

// ======================================================================
// Environment sampling
// ======================================================================

void SurvivalShip::UpdateEnvironment(double simdt)
{
    inAtmosphere = false;
    inVacuum     = false;

    OBJHANDLE hRef = GetSurfaceRef();
    if (!hRef) {
        // Deep space fallback
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
    bool hasAtm = (GetAtmosphericParams(atm) != 0) && currentProfile.hasAtmosphere;
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

    // Radiation baseline from profile, increased at high altitude, maxed in gas giants
    double baseRad = currentProfile.surfaceRadiation;
    if (currentProfile.gasGiant) {
        if (alt < 0) baseRad = 1.0;
    } else {
        if (alt > 1.0e6) baseRad += 0.2;
    }
    envRadiation = Clamp(baseRad, 0.0, 1.0);

    // Temperature blending between actual atmosphere and profile
    if (inAtmosphere && currentProfile.hasAtmosphere) {
        double T = GetAtmTemperature(); // K
        double tempC = T - 273.15;
        envTemperature = 0.5 * tempC + 0.5 * currentProfile.baseTemp;
    } else {
        envTemperature = currentProfile.baseTemp;
    }
}

// ======================================================================
// Micrometeorites
// ======================================================================

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

        // Leak some internal pressure
        internalPressure *= 0.9;
    }
}

// ======================================================================
// Solar flux (for thermal model)
// ======================================================================

double SurvivalShip::ComputeSolarFlux() const
{
    OBJHANDLE hSun = oapiGetObjectByName("Sun");
    if (!hSun) return 0.0;

    VECTOR3 myPos, sunPos;
    ((VESSEL2*)this)->Local2Global(_V(0,0,0), myPos);
    oapiGetGlobalPos(hSun, &sunPos);

    double dist = length(myPos - sunPos);
    double AU   = 1.496e11;

    double flux = 1361.0 * (AU * AU) / (dist * dist);
    return flux; // W/m^2
}

// ======================================================================
// Thermal model (includes gravity-based engine overheating)
// ======================================================================

void SurvivalShip::UpdateThermalModel(double simdt, double g)
{
    double solarFlux = ComputeSolarFlux();

    // Radiative heating from the Sun (scaled for gameplay)
    double solarHeating = solarFlux * 0.0001;

    // Convective heating in atmosphere, proportional to q*v
    double convectiveHeating = 0.0;
    if (inAtmosphere) {
        double q = GetDynPressure();
        double v = GetAirspeed();
        convectiveHeating = q * v * 1e-4;
    }

    // Ambient environment pulling hull temp toward envTemperature
    double ambientEffect = (envTemperature - hullTemp) * 0.01;

    // Engine overheating under gravity
    double mainLevel  = th_main  ? GetThrusterLevel(th_main)  : 0.0;
    double pulseLevel = th_pulse ? GetThrusterLevel(th_pulse) : 0.0;

    double gravityFactor = g / 9.81; // 1.0 at Earth g

    double mainHeat  = mainLevel  * gravityFactor * 5.0;   // °C/s at full thrust
    double pulseHeat = pulseLevel * gravityFactor * 15.0;  // hotter pulse

    // Cooling from radiators
    double cooling = 8.0;

    double dTemp = (solarHeating + convectiveHeating + ambientEffect +
                    mainHeat + pulseHeat - cooling) * simdt;

    hullTemp += dTemp;

    // Overheat damage
    if (hullTemp > overheatThreshold) {
        double excess = hullTemp - overheatThreshold;
        double dmg = excess * 1e-5;
        hullIntegrity -= dmg * simdt;
    }

    // Extreme melting regime
    if (hullTemp > meltThreshold) {
        hullIntegrity   -= 0.1 * simdt;
        internalPressure*= (1.0 - 0.5 * simdt);
        if (internalPressure < 0.0) internalPressure = 0.0;
    }

    if (hullIntegrity < 0.0) hullIntegrity = 0.0;
}

// ======================================================================
// Gravity stress & buckling
// ======================================================================

void SurvivalShip::ApplyGravityStress(double simdt, double g)
{
    // Mild stress above 12 m/s² (~1.2 g), stronger above 25
    if (g > 12.0 && g <= 25.0) {
        double excess = g - 12.0;
        hullIntegrity -= simdt * 0.0005 * excess;
        internalOxygen-= simdt * 0.1   * excess;
        if (internalOxygen < 0.0) internalOxygen = 0.0;
    }

    // Extreme-g: heavy damage over time, never instant kill
    if (g > 25.0) {
        double factor = (g - 25.0) * 0.005;
        hullIntegrity    -= factor * simdt;
        internalPressure *= (1.0 - 0.1 * simdt);
        if (internalPressure < 0.0) internalPressure = 0.0;
    }

    hullIntegrity = Clamp(hullIntegrity, 0.0, 1.0);

    // Buckling as a non-linear structural collapse at very high g
    ApplyGravityBuckling(simdt, g);
}

void SurvivalShip::ApplyGravityBuckling(double simdt, double g)
{
    if (g <= 18.0) return;

    double bucklingFactor = std::pow((g - 18.0) / 10.0, 2.0); // grows quadratically
    double bucklingDamage = bucklingFactor * 0.002;

    hullIntegrity -= bucklingDamage * simdt;
    internalPressure *= (1.0 - bucklingFactor * 0.01 * simdt);
    computerHealth -= bucklingFactor * 0.0005 * simdt;

    if (internalPressure < 0.0) internalPressure = 0.0;
    if (computerHealth < 0.0)   computerHealth   = 0.0;
}

// ======================================================================
// Electronics damage (radiation + temperature)
// ======================================================================

void SurvivalShip::ApplyElectronicsDamage(double simdt)
{
    double effectiveRad = envRadiation * (1.0 - computerShield);

    // Temperature extremes also stress electronics
    if (hullTemp > overheatThreshold)
        effectiveRad += 0.1;
    if (hullTemp < -150.0)
        effectiveRad += 0.05;

    if (effectiveRad > 0.05) {
        double dmg = effectiveRad * 0.0005;
        computerHealth -= dmg * simdt;
    }

    if (computerHealth < 0.0) computerHealth = 0.0;
}

// ======================================================================
// Computer repair (time-based, using materials)
// ======================================================================

void SurvivalShip::StartComputerRepair()
{
    if (repairingComputer) {
        oapiWriteLog("SurvivalShip: Computer repair already in progress");
        return;
    }
    if (repairMaterials <= 0) {
        oapiWriteLog("SurvivalShip: No materials available for repair");
        return;
    }

    repairingComputer      = true;
    computerRepairProgress = 0.0;
    repairMaterials       -= 1;

    oapiWriteLog("SurvivalShip: Computer repair initiated");
}

void SurvivalShip::UpdateComputerRepair(double simdt)
{
    if (!repairingComputer) return;

    if (computerHealth >= 1.0) {
        repairingComputer      = false;
        computerRepairProgress = 0.0;
        return;
    }

    // Approx 120 seconds for a full repair from 0 to 1
    double repairRate = 1.0 / 120.0;
    computerRepairProgress += repairRate * simdt;
    if (computerRepairProgress > 1.0) computerRepairProgress = 1.0;

    computerHealth = Clamp(computerHealth + repairRate * simdt, 0.0, 1.0);

    if (computerRepairProgress >= 1.0) {
        repairingComputer = false;
        oapiWriteLog("SurvivalShip: Computer repair completed");
    }
}

// ======================================================================
// Environment effects on ship
// ======================================================================

void SurvivalShip::ApplyEnvironmentToShip(double simdt)
{
    // Hull breach -> depressurization
    if (hullIntegrity <= 0.0) {
        internalPressure *= (1.0 - 0.5 * simdt);
        if (internalPressure < 0.0) internalPressure = 0.0;
    }

    // Airlock equalization
    if (airlockOpen) {
        double rate = 0.5;
        double diff = envPressure - internalPressure;
        internalPressure += diff * rate * simdt;
        if (internalPressure < 0.0) internalPressure = 0.0;
    }

    // Oxygen consumption
    internalOxygen -= simdt * 1.0;
    if (internalOxygen < 0.0) internalOxygen = 0.0;

    // Radiation vs hull
    double effectiveRad = envRadiation * (1.0 - radiationShield);
    if (effectiveRad > 0.1) {
        hullIntegrity -= simdt * 0.0005 * effectiveRad;
    }

    // Corrosive atmospheres
    if (currentProfile.corrosive && inAtmosphere) {
        hullIntegrity -= simdt * 0.0005;
    }

    // Gas giants in atmosphere
    if (currentProfile.gasGiant && inAtmosphere) {
        hullIntegrity -= simdt * 0.01;
    }

    hullIntegrity = Clamp(hullIntegrity, 0.0, 1.0);
}

// ======================================================================
// Gravity-dependent ISP (fuel consumption penalty)
// ======================================================================

void SurvivalShip::UpdateGravityDependentISP(double g)
{
    double gravFactor = 1.0 + 0.25 * (g / 9.81); // +25% "fuel hit" per 1 g

    double mainIsp   = baseIsp_main  / gravFactor;
    double rcsIsp    = baseIsp_rcs   / gravFactor;
    double pulseIsp  = baseIsp_pulse / (gravFactor * 1.5); // pulse more penalized

    if (th_main)
        SetThrusterIsp(th_main, mainIsp * 9.80665);
    if (th_pulse)
        SetThrusterIsp(th_pulse, pulseIsp * 9.80665);
    for (int i = 0; i < 6; ++i) {
        if (th_rcs[i])
            SetThrusterIsp(th_rcs[i], rcsIsp * 9.80665);
    }
}

// ======================================================================
// Hover mode (gravity-aware auto throttle)
// ======================================================================

void SurvivalShip::UpdateHoverMode(double simdt, double g)
{
    if (!hoverMode) return;
    if (!th_main || !ph_main) return;

    // If computer badly damaged, hover can glitch off
    if (computerHealth < 0.3) {
        double p = 0.3 * simdt;
        double r = (double)rand() / (double)RAND_MAX;
        if (r < p) {
            hoverMode = false;
            oapiWriteLog("SurvivalShip: Hover mode failed due to computer damage");
            SetThrusterLevel(th_main, 0.0);
            return;
        }
    }

    if (g < 0.1) {
        SetThrusterLevel(th_main, 0.0);
        return;
    }

    double mass   = GetMass();
    double weight = mass * g;

    MATRIX3 R;
    GetRotationMatrix(R);
    VECTOR3 dirLocal  = _V(0,0,1);  // main thrust direction
    VECTOR3 dirGlobal = mul(R, dirLocal);

    VECTOR3 gvec;
    GetGravityVector(gvec);
    double gmag = length(gvec);
    VECTOR3 gDir = gmag > 0 ? gvec / gmag : _V(0, -1, 0);

    double cosAngle = -dotp(dirGlobal, gDir); // thrust vs -g
    if (cosAngle < 0.1) {
        SetThrusterLevel(th_main, 0.0);
        return;
    }

    double effectiveThrust = mainMaxThrust * cosAngle;
    double requiredLevel   = weight / effectiveThrust;
    requiredLevel          = Clamp(requiredLevel, 0.0, 1.0);

    SetThrusterLevel(th_main, requiredLevel);
}

// ======================================================================
// Pulse engine (high thrust, low ISP, vacuum focused)
// ======================================================================

void SurvivalShip::UpdatePulseEngine(double simdt, double g)
{
    if (!th_pulse) return;

    if (!pulseActive) {
        SetThrusterLevel(th_pulse, 0.0);
        return;
    }

    // Only "safe" in vacuum or very thin atmosphere
    if (!inVacuum && envPressure > 100.0) {
        hullTemp += 100.0 * simdt;
        oapiWriteLog("SurvivalShip: Pulse engine fired in atmosphere - overheating!");
    }

    // Computer damage reduces effective control
    double controlFactor = computerHealth;
    if (controlFactor < 0.2) controlFactor = 0.2;

    SetThrusterLevel(th_pulse, controlFactor);

    // Gravity-based penalty is handled via ISP adjustment;
    // here we just ensure we don't run dry silently.
    double fuel = GetPropellantMass(ph_main);
    if (fuel <= 0.0) {
        SetThrusterLevel(th_pulse, 0.0);
        pulseActive = false;
        oapiWriteLog("SurvivalShip: Pulse engine stopped - fuel depleted");
    }
}

// ======================================================================
// Gravity-based maneuver penalties
// ======================================================================

void SurvivalShip::ApplyGravityManeuverPenalties(double simdt, double g)
{
    double maneuverPenalty = Clamp(g / 20.0, 0.0, 1.0); // 0–1

    // Reduce RCS thrust
    for (int i = 0; i < 6; ++i) {
        if (th_rcs[i]) {
            double baseThrust = 5000.0;
            double reduced = baseThrust * (1.0 - 0.7 * maneuverPenalty);
            SetThrusterMax0(th_rcs[i], reduced);
        }
    }

    // Reduce rotational agility
    VECTOR3 rot;
    GetAngularVel(rot);
    rot *= (1.0 - 0.5 * maneuverPenalty);
    SetAngularVel(rot);

    // Hover instability at high gravity
    if (hoverMode && th_main) {
        double instability = maneuverPenalty * 0.1; // 10% wobble at max
        double jitter = ((rand() % 200) - 100) / 1000.0; // -0.1 to +0.1
        double level = GetThrusterLevel(th_main);
        SetThrusterLevel(th_main, Clamp(level + jitter * instability, 0.0, 1.0));
    }
}

// ======================================================================
// Gravity-based cockpit shake
// ======================================================================

void SurvivalShip::ApplyGravityCockpitShake(double simdt, double g)
{
    double shake = Clamp((g - 9.81) / 20.0, 0.0, 1.0); // 0–1

    if (shake <= 0.0) return;

    double jx = ((rand() % 200) - 100) / 5000.0; // -0.02 to +0.02
    double jy = ((rand() % 200) - 100) / 5000.0;
    double jz = ((rand() % 200) - 100) / 5000.0;

    VECTOR3 rot;
    GetAngularVel(rot);

    rot.x += jx * shake;
    rot.y += jy * shake;
    rot.z += jz * shake;

    SetAngularVel(rot);
}

// ======================================================================
// Gravity-based "sound effects" (log-based)
// ======================================================================

void SurvivalShip::ApplyGravitySoundEffects(double g)
{
    if (g > 12.0 && g <= 18.0) {
        if ((rand() % 50) == 0)
            oapiWriteLog("SurvivalShip: Structural creaking detected");
    } else if (g > 18.0 && g <= 25.0) {
        if ((rand() % 30) == 0)
            oapiWriteLog("SurvivalShip: Hull groaning under gravity load");
    } else if (g > 25.0) {
        if ((rand() % 20) == 0)
            oapiWriteLog("SurvivalShip: WARNING - Hull resonance approaching failure");
    }
}

// ======================================================================
// EVA spawning
// ======================================================================

void SurvivalShip::SpawnEVA()
{
    if (internalPressure < 5.0e4) {
        oapiWriteLog("SurvivalShip: Internal pressure too low to EVA");
        return;
    }

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
    std::memset(&vs, 0, sizeof(vs));
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

// ======================================================================
// PreStep: main update loop
// ======================================================================

void SurvivalShip::clbkPreStep(double simt, double simdt, double mjd)
{
    UpdateEnvironment(simdt);
    ApplyEnvironmentToShip(simdt);
    ApplyRandomMicrometeorites(simdt);

    // Compute local gravity once per frame
    VECTOR3 gvec;
    GetGravityVector(gvec);
    double g = length(gvec);

    // Gravity-dependent ISP ("fuel tax")
    UpdateGravityDependentISP(g);

    // Thermal, gravity stress, electronics
    UpdateThermalModel(simdt, g);
    ApplyGravityStress(simdt, g);
    ApplyElectronicsDamage(simdt);
    UpdateComputerRepair(simdt);

    // Modes & gravity effects
    UpdateHoverMode(simdt, g);
    UpdatePulseEngine(simdt, g);
    ApplyGravityManeuverPenalties(simdt, g);
    ApplyGravityCockpitShake(simdt, g);
    ApplyGravitySoundEffects(g);
}

// ======================================================================
// Input: key handling
// ======================================================================

int SurvivalShip::clbkConsumeBufferedKey(DWORD key, bool down, char *kstate)
{
    if (!down) return 0;

    // Airlock toggle
    if (key == OAPI_KEY_A) {
        airlockOpen = !airlockOpen;
        oapiWriteLog(airlockOpen ?
                     "SurvivalShip: Airlock opened" :
                     "SurvivalShip: Airlock closed");
        return 1;
    }

    // EVA
    if (key == OAPI_KEY_E) {
        SpawnEVA();
        return 1;
    }

    // Hover mode toggle
    if (key == OAPI_KEY_H) {
        hoverMode = !hoverMode;
        if (!hoverMode && th_main) {
            SetThrusterLevel(th_main, 0.0);
        }
        oapiWriteLog(hoverMode ?
                     "SurvivalShip: Hover mode ON" :
                     "SurvivalShip: Hover mode OFF");
        return 1;
    }

    // Pulse engine toggle
    if (key == OAPI_KEY_P) {
        pulseActive = !pulseActive;
        if (!pulseActive && th_pulse) {
            SetThrusterLevel(th_pulse, 0.0);
        }
        oapiWriteLog(pulseActive ?
                     "SurvivalShip: Pulse engine ON" :
                     "SurvivalShip: Pulse engine OFF");
        return 1;
    }

    // Computer repair (requires materials)
    if (key == OAPI_KEY_R) {
        StartComputerRepair();
        return 1;
    }

    // Debug: add repair materials (hook this to EVA later)
    if (key == OAPI_KEY_M) {
        repairMaterials += 1;
        oapiWriteLog("SurvivalShip: Gained 1 repair material (debug)");
        return 1;
    }

    return 0;
}

// ======================================================================
// HUD drawing
// ======================================================================

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

    sprintf(buf, "HullTemp: %.0f C", hullTemp);
    TextOut(hDC, 20, 120, buf, (int)strlen(buf));

    sprintf(buf, "Computer: %.0f%%", computerHealth * 100.0);
    TextOut(hDC, 20, 140, buf, (int)strlen(buf));

    sprintf(buf, "RepairMat: %d  Repairing: %s",
            repairMaterials, repairingComputer ? "YES" : "NO");
    TextOut(hDC, 20, 160, buf, (int)strlen(buf));

    double fuel = ph_main ? GetPropellantMass(ph_main) : 0.0;
    sprintf(buf, "Fuel: %.0f kg", fuel);
    TextOut(hDC, 20, 180, buf, (int)strlen(buf));

    sprintf(buf, "Hover: %s  Pulse: %s",
            hoverMode ? "ON" : "OFF",
            pulseActive ? "ON" : "OFF");
    TextOut(hDC, 20, 200, buf, (int)strlen(buf));

    // Gravity readout
    VECTOR3 gvec;
    GetGravityVector(gvec);
    double g = length(gvec);
    sprintf(buf, "g: %.2f m/s^2", g);
    TextOut(hDC, 20, 220, buf, (int)strlen(buf));

    // Warning panel (goes offline if computer heavily damaged)
    bool warningsWork = (computerHealth > 0.2);
    int  y = 240;

    if (warningsWork) {
        if (envRadiation > 0.7) {
            TextOut(hDC, 20, y, "WARN: HIGH RADIATION", 20); y += 20;
        }
        if (hullTemp > overheatThreshold) {
            TextOut(hDC, 20, y, "WARN: HULL OVERHEATING", 22); y += 20;
        }
        if (hullIntegrity < 0.5) {
            TextOut(hDC, 20, y, "WARN: HULL DAMAGE", 18); y += 20;
        }
        if (internalPressure < 5.0e4) {
            TextOut(hDC, 20, y, "WARN: CABIN DEPRESSURIZING", 25); y += 20;
        }
        if (computerHealth < 0.5) {
            TextOut(hDC, 20, y, "WARN: COMPUTER DEGRADED", 23); y += 20;
        }

        // Gravity warnings
        if (g > 12.0 && g <= 20.0) {
            TextOut(hDC, 20, y, "WARN: HIGH GRAVITY STRESS", 25); y += 20;
        }
        if (g > 20.0 && g <= 25.0) {
            TextOut(hDC, 20, y, "WARN: EXTREME GRAVITY - SYSTEM STRAIN", 38); y += 20;
        }
        if (g > 25.0) {
            TextOut(hDC, 20, y, "CRITICAL: GRAVITY EXCEEDS STRUCTURAL LIMITS", 47); y += 20;
        }
    } else {
        TextOut(hDC, 20, y, "WARNINGS OFFLINE (COMPUTER FAILURE)", 33);
        y += 20;
    }

    TextOut(hDC, 20, y + 20,
            "E: EVA | A: Airlock | H: Hover | P: Pulse | R: Repair | M: +Material",
            72);
}
