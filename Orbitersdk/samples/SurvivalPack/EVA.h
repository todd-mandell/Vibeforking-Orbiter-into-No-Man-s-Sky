#pragma once
#include "orbitersdk.h"
#include <string>
#include <map>

class EVA : public VESSEL2 {
public:
    EVA(OBJHANDLE hVessel, int flightmodel);

    void clbkSetClassCaps(FILEHANDLE cfg) override;
    void clbkPreStep(double simt, double simdt, double mjd) override;
    int  clbkConsumeBufferedKey(DWORD key, bool down, char *kstate) override;
    void clbkDrawHUD(int mode, const HUDPAINTSPEC *hps, HDC hDC) override;

private:
    // Suit systems
    double suitOxygen;      // seconds
    double suitPower;       // 0–1
    double suitIntegrity;   // 0–1
    double health;          // 0–1

    // Suit limits / protection
    double maxSafePressure;       // Pa
    double maxSafeTempLow;        // °C
    double maxSafeTempHigh;       // °C
    double radiationShieldFactor; // 0–1
    double toxicityProtection;    // 0–1
    double thermalInsulation;     // 0–1
    double suitInternalTemp;      // °C
    double suitCoolingPower;      // °C “absorbed”
    double suitHeatingPower;      // °C “added”

    // Inventory
    std::map<std::string, int> inventory;

    // Mining
    bool    inMiningRange;
    VECTOR3 resourcePos;
    double  miningRange;

    // Environment state
    double envPressure;     // Pa
    double envRadiation;    // arbitrary units
    double envToxicity;     // 0–1
    double envTemperature;  // °C
    bool   underwater;
    bool   inAtmosphere;
    bool   inVacuum;

    // Internal helpers
    bool   CheckProximity(const VECTOR3 &target, double range);
    void   MineResource();
    void   UpdateEnvironment(double simdt);
    void   ApplyEnvironmentEffects(double simdt);

    double ComputeVacuumTemperature();
    double ComputeAtmosphereTemperature();
    double ComputeWaterTemperature(double depth);

    void   ApplyRandomMicrometeorites(double simdt);
};
