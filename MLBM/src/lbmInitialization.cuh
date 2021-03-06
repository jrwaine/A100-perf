#ifndef __LBM_INITIALIZATION_CUH
#define __LBM_INITIALIZATION_CUH

#include <string>
#include <math.h>
#include <cuda.h>
#include <curand.h>
#include <cuda_runtime.h>
#include <builtin_types.h>
#include "globalFunctions.h"
#include "errorDef.h"
#include "var.h"

/*
*   @brief Initializes random numbers (useful to initialize turbulence)
*   @param randomNumbers: vector of random numbers (size is NX*NY*NZ)
*   @param seed: seed to use for generation
*/
__host__
void initializationRandomNumbers(
    float* randomNumbers, 
    int seed
);


/*
*   @brief Initializes moments with equilibrium population, with density 
*          and velocity defined in the function itself
*   @param fMom: moments to be inialized to be initialized 
*/
__global__ void gpuInitialization_mom(
    dfloat *fMom, float* randomNumbers);

/*
*   @brief Initializes populations in the intefaces based on the moments 
*          defined in the gpuInitialization_mom
*   @param fMom: moments used to initialize the interface populations
*   @param fGhostX_0: populations for threadIdx.x == 0
*   @param fGhostX_1: populations for threadIdx.x == NX-1
*   @param fGhostY_0: populations for threadIdx.y == 0
*   @param fGhostY_1: populations for threadIdx.y == NY-1
*   @param fGhostZ_0: populations for threadIdx.z == 0
*   @param fGhostZ_1: populations for threadIdx.z == NZ-1
*/
__global__ void gpuInitialization_pop(
    dfloat *fMom,
    dfloat *fGhostX_0, dfloat *fGhostX_1,
    dfloat *fGhostY_0, dfloat *fGhostY_1,
    dfloat *fGhostZ_0, dfloat *fGhostZ_1);

#endif // !__LBM_INITIALIZATION_CUH