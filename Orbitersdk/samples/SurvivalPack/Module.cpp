#include "orbitersdk.h"
#include "EVA.h"
#include "SurvivalShip.h"

DLLCLBK VESSEL *ovcInit(OBJHANDLE hVessel, int flightmodel)
{
    char name[64] = {0};
    oapiGetObjectName(hVessel, name, 63);

    // Decide which class to instantiate based on config ClassName
    // Orbiter handles this automatically: we just need to match ClassName in .cfg
    // So we can ignore 'name' here and let Orbiter route by ClassName.
    // We just return the appropriate class based on the vessel's class.
    // But the simpler pattern: always return EVA or SurvivalShip based on class name:

    char classname[64] = {0};
    oapiGetVesselClass(hVessel, classname, 63);

    if (!strcmp(classname, "EVA"))
        return new EVA(hVessel, flightmodel);
    if (!strcmp(classname, "SurvivalShip"))
        return new SurvivalShip(hVessel, flightmodel);

    // Fallback (should not happen if cfgs are correct)
    return new VESSEL2(hVessel, flightmodel);
}

DLLCLBK void ovcExit(VESSEL *v)
{
    if (v) delete v;
}
