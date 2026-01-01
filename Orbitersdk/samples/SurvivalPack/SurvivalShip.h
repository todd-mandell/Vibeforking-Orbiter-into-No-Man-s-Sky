#pragma once
#include "orbitersdk.h"
#include "PlanetHazards.h"

// ======================================================================
// SurvivalShip
// 
// Features:
// - Main engine with fuel & ISP (realistic Δv behavior)
// - Pulse engine (high thrust, low ISP, vacuum-heavy use)
// - RCS thrusters
// - Hover mode (gravity-aware auto throttle for vertical hold)
// - Gravity-based:
//      * Structural stress & buckling
//      * Maneuverability penalties
//      * Cockpit shake
//      * "Sound" effects via log/warnings
//      * Engine overheating
//      * Fuel consumption via ISP reduction
// - Environment & survival:
//      * External pressure, temperature, radiation
//      * Corrosive atmospheres
//      * Gas giant death zones
//      * Micrometeorites
// - Internals:
//      * Hull integrity
//      * Internal pressure & oxygen
//      * Thermal model
//      * Flight computer health (electronics damage)
//      * Computer repair over time using materials
// - HUD warnings and status readouts
// 
// This class is designed as a single, self-contained Orbiter vessel
// for a hardcore survival gameplay loop.
// ======================================================================

class SurvivalShip : public VESSEL2 {
public:
    SurvivalShip(OBJHANDLE hVessel, int flightmodel);

    void clbkSetClassCaps(FILEHANDLE cfg) override;
    void clbkPreStep(double simt, double simdt, double mjd) override;
    int  clbkConsumeBufferedKey(DWORD key, bool down, char *kstate) override;
    void clbkDrawHUD(int mode, const HUDPAINTSPEC *hps, HDC hDC) override;

private:
    // ------------------------------------------------------------------
    // Core survival state
    // ------------------------------------------------------------------
    double hullIntegrity;     // 0–1, structural health
    double internalPressure;  // Pa, cabin pressure
    double internalOxygen;    // seconds of breathable O2
    double powerLevel;        // 0–1, abstract power reserve

    double radiationShield;   // 0–1, hull-level radiation protection
    double thermalInsulation; // 0–1, hull thermal moderation

    // ------------------------------------------------------------------
    // External environment
    // ------------------------------------------------------------------
    double envPressure;       // Pa
    double envRadiation;      // 0–1
    double envTemperature;    // °C
    bool   inAtmosphere;
    bool   inVacuum;
    PlanetHazardProfile currentProfile;

    bool   airlockOpen;

    // ------------------------------------------------------------------
    // Propulsion & fuel
    // ------------------------------------------------------------------
    PROPELLANT_HANDLE ph_main;

    THRUSTER_HANDLE   th_main;       // main engine
    THRUSTER_HANDLE   th_pulse;      // pulse engine
    THRUSTER_HANDLE   th_rcs[6];     // +X -X +Y -Y +Z -Z

    double maxFuelMass;       // kg
    double baseIsp_main;      // s, nominal ISP main
    double baseIsp_rcs;       // s, nominal ISP RCS
    double baseIsp_pulse;     // s, nominal ISP pulse
    double mainMaxThrust;     // N
    double pulseMaxThrust;    // N

    bool   hoverMode;         // auto-throttle for gravity hold
    bool   pulseActive;       // pulse engine toggle

    // ------------------------------------------------------------------
    // Thermal / overheating
    // ------------------------------------------------------------------
    double hullTemp;          // °C, approximate hull skin temp
    double overheatThreshold; // °C, start thermal damage
    double meltThreshold;     // °C, catastrophic regime

    // ------------------------------------------------------------------
    // Flight computer / electronics
    // ------------------------------------------------------------------
    double computerHealth;        // 0–1
    double computerShield;        // 0–1, radiation protection
    double computerRepairProgress;// 0–1 during repair
    int    repairMaterials;       // abstract “repair units”
    bool   repairingComputer;     // repair in progress

    // ------------------------------------------------------------------
    // Internal utility
    // ------------------------------------------------------------------
    void   SetupThrustersAndFuel();
    void   SetupRCS();

    void   UpdateEnvironment(double simdt);
    void   ApplyEnvironmentToShip(double simdt);

    void   ApplyRandomMicrometeorites(double simdt);

    // Gravity-related effects (structural, maneuver, etc.)
    void   ApplyGravityStress(double simdt, double g);
    void   ApplyGravityBuckling(double simdt, double g);
    void   ApplyGravityManeuverPenalties(double simdt, double g);
    void   ApplyGravityCockpitShake(double simdt, double g);
    void   ApplyGravitySoundEffects(double g);

    // Electronics & thermal
    void   ApplyElectronicsDamage(double simdt);
    double ComputeSolarFlux() const;
    void   UpdateThermalModel(double simdt, double g);

    // Engine control modes
    void   UpdateHoverMode(double simdt, double g);
    void   UpdatePulseEngine(double simdt, double g);
    void   UpdateGravityDependentISP(double g);

    // EVA / crew interaction
    void   SpawnEVA();

    // Repairs
    void   StartComputerRepair();
    void   UpdateComputerRepair(double simdt);

    // Helpers
    double Clamp(double v, double lo, double hi) const;
};
