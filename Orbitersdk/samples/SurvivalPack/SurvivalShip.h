#pragma once
#include "orbitersdk.h"

class SurvivalShip : public VESSEL2 {
public:
    SurvivalShip(OBJHANDLE hVessel, int flightmodel);

    void clbkSetClassCaps(FILEHANDLE cfg) override;
    void clbkPreStep(double simt, double simdt, double mjd) override;
    int  clbkConsumeBufferedKey(DWORD key, bool down, char *kstate) override;
    void clbkDrawHUD(int mode, const HUDPAINTSPEC *hps, HDC hDC) override;

private:
    double hullIntegrity;     // 0–1
    double internalPressure;  // Pa
    double internalOxygen;    // seconds of breathable air
    double powerLevel;        // 0–1

    // Protection
    double radiationShield;   // 0–1
    double thermalInsulation; // 0–1

    // Environment at ship location
    double envPressure;
    double envRadiation;
    double envTemperature;
    bool   inAtmosphere;
    bool   inVacuum;

    bool   airlockOpen;

    void   UpdateEnvironment(double simdt);
    void   ApplyEnvironmentToShip(double simdt);
    void   ApplyRandomMicrometeorites(double simdt);
};
