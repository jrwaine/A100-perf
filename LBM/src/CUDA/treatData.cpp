/*
*   LBM-CERNN
*   Copyright (C) 2018-2019 Waine Barbosa de Oliveira Junior
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License along
*   with this program; if not, write to the Free Software Foundation, Inc.,
*   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*
*   Contact: cernn-ct@utfpr.edu.br and waine@alunos.utfpr.edu.br
*/

#include "treatData.h"


void treatData(MacrProc* processing)
{
    /* DATA TREATMENT EXAMPLE */
    dfloat denRes = 1.0, numRes = 0.0; // denominator and numerator for residual
    Macroscopics* macrCurr = processing->macrCurr; 
    Macroscopics* macrOld = processing->macrOld; 

    processing->avgRho = 0;

    for(int z = 0; z < NZ_TOTAL; z++)
    {
        for(int y = 0; y < NY; y++)
        {
            for(int x = 0; x < NX; x++)
            {
                size_t idx = idxScalar(x, y, z);
                /* ------- Residual calculation ------- */
                const dfloat rhoVar = macrCurr->rho[idx];
                const dfloat diff_ux = macrCurr->u.x[idx];
                const dfloat diff_uy = macrCurr->u.y[idx];
                const dfloat diff_uz = macrCurr->u.z[idx];

                numRes += rhoVar*(diff_ux * diff_ux + diff_uy * diff_uy + diff_uz * diff_uz);
                /* ------------------------------------ */

                /* ------- Avg. rho calculation ------- */
                processing->avgRho += macrCurr->rho[idx];
                // printf("%d %d %d %f\n", x, y, z, macrCurr->rho[idx]);
                /* ------------------------------------ */
                
                /* ----- Avg. Uz plan calculation ----- */
                processing->avgUzPlanXZ[y] += macrCurr->u.z[idx];
                /* ------------------------------------ */
            }
        }
    }

    /* ------- Residual calculation ------- */
    if(denRes != 0)
        processing->residual = numRes/(2*N*N*N);
    else
        processing->residual = 1;
    /* ------------------------------------ */

    /* ------- Avg. rho calculation ------- */
    processing->avgRho /= TOTAL_NUMBER_LBM_NODES;
    /* ------------------------------------ */

    /* ----- Avg. Uz plan calculation ----- */
    for(int y = 0; y < NY; y++)
        processing->avgUzPlanXZ[y] /= (NX*NZ_TOTAL);
    /* ------------------------------------ */
}


bool stopSim(MacrProc* processing)
{
    /* SIMULATIONS STOP CONDITIONS EXAMPLE */
    if(processing->residual < RESID_MAX)
        return true;
    if(processing->avgRho < 0)
        return true;
    return false;
}


void printTreatData(MacrProc* processing)
{
    /* PRINT TREATED DATA EXAMPLE */
    //printf("\n--------------------------------- TREATED DATA ---------------------------------\n");
    printf("Step: %d %0.7e \n", *(processing->step), processing->residual);
}


void saveTreatData(MacrProc* processing)
{
    /* SAVE TO CSV EXAMPLE */
    std::string strFileAvgUz;
    strFileAvgUz = getVarFilename("avgUz", *(processing->step), ".csv");
    
    FILE* fileAvgUz = nullptr;
    fileAvgUz = fopen(strFileAvgUz.c_str(), "w");
    if(fileAvgUz != nullptr)
    {
        // write header
        fprintf(fileAvgUz, "y\tavg uz\n");
        for(int y = 0; y < NY; y++)
        {
            fprintf(fileAvgUz, "%d\t%.6e\n", y, processing->avgUzPlanXZ[y]);
        }
    }
    else
    {
        printf("Error saving \"%s\" \nProbably wrong path!\n", strFileAvgUz.c_str());
    }
}