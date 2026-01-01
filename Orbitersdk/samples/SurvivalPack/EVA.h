#pragma once
#include "orbitersdk.h"
#include "PlanetHazards.h"
#include <string>
#include <map>

struct IonBattery {
    double capacity;   // max charge units
    double charge;     // current charge
    bool   damaged;    // if true, leaks radiation
};

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

    // Base suit limits / protection
    double maxSafePressure;       // Pa
    double maxSafeTempLow;        // °C
    double maxSafeTempHigh;       // °C
    double baseRadiationShield;   // 0–1
    double baseToxicProtection;   // 0–1
    double thermalInsulation;     // 0–1
    double suitInternalTemp;      // °C
    double suitCoolingPower;      // °C “absorbed”
    double suitHeatingPower;      // °C “added”

    // Toxic shield (upgrade)
    double toxicShieldCapacity;   // max shield units
    double toxicShieldCharge;     // current shield units
    double toxicShieldEfficiency; // extra protection scaling
    double toxicShieldDrainRate;  // per second in toxic env
    bool   toxicShieldOnline;

    // ION battery
    IonBattery ion;
    double     ionLeakFactor;     // radiation per unit charge when damaged

    // Inventory
    std::map<std::string, int> inventory;

    // Mining
    bool    inMiningRange;
    VECTOR3 resourcePos;
    double  miningRange;

    // Environment
    double envPressure;     // Pa
    double envRadiation;    // 0–1
    double envToxicity;     // 0–1
    double envTemperature;  // °C
    bool   underwater;
    bool   inAtmosphere;
    bool   inVacuum;
    PlanetHazardProfile currentProfile;

    // Internal helpers
    bool   CheckProximity(const VECTOR3 &target, double range);
    void   MineResource();

    void   UpdateEnvironment(double simdt);
    void   ApplyEnvironmentEffects(double simdt);

    double ComputeVacuumTemperature();
    double ComputeAtmosphereTemperature();
    double ComputeWaterTemperature(double depth);

    void   ApplyRandomMicrometeorites(double simdt);

    void   TryReenterShip();

    // Upgrades & ION
    void   UpdateToxicShield(double simdt);
    void   ApplyIonBatteryEffects(double simdt);
    bool   ConsumeIonCharge(double amount);
    void   RechargeToxicShieldFromIon();
    void   CraftIonCell();

    // Gravity effects
    void   ApplyGravityEffects(double simdt);
};
