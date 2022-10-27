#include <R_ext/Rdynload.h>

#include "Cardinal.h"

extern "C" {

    static const R_CallMethodDef callMethods[] = {
        {"C_gaussianFilter", (DL_FUNC) &gaussianFilter, 3},
        {"C_bilateralFilter", (DL_FUNC) &bilateralFilter, 3},
        {"C_readImzML", (DL_FUNC) &readImzML, 1},
        {"C_writeImzML", (DL_FUNC) &writeImzML, 3},
        {"C_findNeighbors", (DL_FUNC) &findNeighbors, 4},
        {"C_spatialOffsets", (DL_FUNC) &spatialOffsets, 3},
        {"C_spatialWeights", (DL_FUNC) &spatialWeights, 4},
        {"C_spatialDistance", (DL_FUNC) &spatialDistance, 8},
        {"C_spatialScores", (DL_FUNC) &spatialScores, 5},
        {"C_spatialFilter", (DL_FUNC) &spatialFilter, 3},
        {NULL, NULL, 0}
    };

    void R_init_Cardinal(DllInfo * info)
    {
        R_registerRoutines(info, NULL, callMethods, NULL, NULL);
    }

}
