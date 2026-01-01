#include "orbitersdk.h"
#include "EVA.h"
#include "SurvivalShip.h"

static VESSEL *ovcInit_EVA(OBJHANDLE hVessel, int flightmodel)
{
    return new EVA(hVessel, flightmodel);
}

static VESSEL *ovcInit_SurvivalShip(OBJHANDLE hVessel, int flightmodel)
{
    return new SurvivalShip(hVessel, flightmodel);
}

static void ovcExit_Generic(VESSEL *v)
{
    if (v) delete v;
}

DLLCLBK void InitModule(HINSTANCE hModule)
{
    oapiRegisterVesselClass("EVA", ovcInit_EVA, ovcExit_Generic);
    oapiRegisterVesselClass("SurvivalShip", ovcInit_SurvivalShip, ovcExit_Generic);
}

DLLCLBK void ExitModule(HINSTANCE hModule)
{
}
