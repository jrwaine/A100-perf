#include "particle.h"

#ifdef IBM

void ParticlesSoA::updateParticlesAsSoA(Particle* particles){
    unsigned int totalIbmNodes = 0;

    // Determine the total number of nodes
    for (int p = 0; p < NUM_PARTICLES; p++)
    {
        totalIbmNodes += particles[p].numNodes;
    }

    printf("Total number of nodes: %u\n", totalIbmNodes);
    printf("Total memory used for Particles: %lu Mb\n",
           (unsigned long)((totalIbmNodes * sizeof(particleNode) * N_GPUS + NUM_PARTICLES * sizeof(particleCenter)) / BYTES_PER_MB));
    fflush(stdout);

    printf("Allocating particles in GPU... \t"); fflush(stdout);
    // Allocate nodes in each GPU
    for(int i = 0; i < N_GPUS; i++){
        checkCudaErrors(cudaSetDevice(GPUS_TO_USE[i]));
        this->nodesSoA[i].allocateMemory(totalIbmNodes);
    }

    // Allocate particle center array
    checkCudaErrors(cudaSetDevice(GPUS_TO_USE[0]));
    checkCudaErrors(
        cudaMallocManaged((void**)&(this->pCenterArray), sizeof(ParticleCenter) * NUM_PARTICLES));
    // Allocate array of last positions for Particles
    this->pCenterLastPos = (dfloat3*)malloc(sizeof(dfloat3)*NUM_PARTICLES);
    this->pCenterLastWPos = (dfloat3*)malloc(sizeof(dfloat3) * NUM_PARTICLES);
    printf("Particles allocated in GPU!\n"); fflush(stdout);

    printf("Optimizig memory layout of particles for GPU... \t"); fflush(stdout);

    for (int p = 0; p < NUM_PARTICLES; p++)
    {
        checkCudaErrors(cudaSetDevice(GPUS_TO_USE[0]));
        this->pCenterArray[p] = particles[p].pCenter;
        this->pCenterLastPos[p] = particles[p].pCenter.pos;
        this->pCenterLastWPos[p] = particles[p].pCenter.w_pos;
        for(int i = 0; i < N_GPUS; i++){
            checkCudaErrors(cudaSetDevice(GPUS_TO_USE[i]));
            this->nodesSoA[i].copyNodesFromParticle(particles[p], p, i);
        }
    }
    checkCudaErrors(cudaSetDevice(GPUS_TO_USE[0]));

    printf("Optimized memory layout for GPU!\n"); fflush(stdout);

    printf("Particles positions:\n");
    for(int p = 0; p < NUM_PARTICLES; p++)
    {
        printf("p[%u] -- x: %f - y: %f - z: %f \n",
               p, this->pCenterArray[p].pos.x, 
               this->pCenterArray[p].pos.y, 
               this->pCenterArray[p].pos.z);
    }

    fflush(stdout);
}


void ParticlesSoA::updateNodesGPUs(){
    // No need to update for 1 GPU
    if(N_GPUS == 1)
        return;

    for(int i = 0; i < NUM_PARTICLES; i++){
        checkCudaErrors(cudaSetDevice(GPUS_TO_USE[0]));
        if(!this->pCenterArray[i].movable)
            continue;

        dfloat3 pos_p = this->pCenterArray[i].pos;
        dfloat3 last_pos = this->pCenterLastPos[i];
        dfloat3 w_pos = this->pCenterArray[i].w_pos;
        dfloat3 last_w_pos = this->pCenterLastWPos[i];
        dfloat3 diff_w_pos = dfloat3(w_pos.x-last_w_pos.x, w_pos.y-last_w_pos.y, w_pos.z-last_w_pos.z);
        dfloat radius = this->pCenterArray[i].radius;

        dfloat min_pos = myMin(pos_p.z,last_pos.z) - (radius - BREUGEM_PARAMETER);
        dfloat max_pos = myMax(pos_p.z,last_pos.z) + (radius - BREUGEM_PARAMETER);
        int min_gpu = (int)(min_pos+NZ_TOTAL)/(NZ)-N_GPUS;
        int max_gpu = (int)(max_pos/NZ);

        // Translation
        dfloat diff_z = (pos_p.z-last_pos.z);
        if (diff_z < 0)
            diff_z = -diff_z;
        // Maximum rotation
        diff_z += (radius-BREUGEM_PARAMETER)*sqrt(diff_w_pos.x*diff_w_pos.x+diff_w_pos.y*diff_w_pos.y);

        // Particle has not moved enoush and nodes that needs to be 
        // updated/synchronized are already considering that
        if(diff_z < IBM_PARTICLE_UPDATE_DIST)
            continue;

        // Update particle's last position
        this->pCenterLastPos[i] = this->pCenterArray[i].pos;
        this->pCenterLastWPos[i] = this->pCenterArray[i].w_pos;
        if(min_gpu == max_gpu)
            continue;

        for(int n = min_gpu; n <= max_gpu; n++){
            // Set current device
            int real_gpu = (n+N_GPUS)%N_GPUS;
            checkCudaErrors(cudaSetDevice(GPUS_TO_USE[real_gpu]));
            int left_shift = 0;
            for(int p = 0; p < this->nodesSoA[real_gpu].numNodes; p++){
                // Shift left nodes, if a node was already removed
                if(left_shift != 0){
                    this->nodesSoA[real_gpu].leftShiftNodesSoA(p, left_shift);
                }

                // Node is from another particle
                if(this->nodesSoA[real_gpu].particleCenterIdx[p] != i)
                    continue;

                // Check in what GPU node is
                dfloat pos_z = this->nodesSoA[real_gpu].pos.z[p];
                int node_gpu = (int) (pos_z/NZ);
                // If node is still in same GPU, continues
                if(node_gpu == real_gpu)
                    continue;
                //printf("b");
                // to not raise any error when setting up device
                #ifdef IBM_BC_Z_PERIODIC
                    node_gpu = (node_gpu+N_GPUS)%N_GPUS;
                #else
                    if(node_gpu < 0)
                        node_gpu = 0;
                    else if(node_gpu >= N_GPUS)
                        node_gpu = N_GPUS-1;
                #endif

                // Nodes will have to be shifted
                left_shift += 1;

                // Get values to move
                ParticleNodeSoA nSoA = this->nodesSoA[real_gpu];
                dfloat copy_S = nSoA.S[p];
                unsigned int copy_pIdx = nSoA.particleCenterIdx[p];
                dfloat3 copy_pos = nSoA.pos.getValuesFromIdx(p);
                dfloat3 copy_vel = nSoA.vel.getValuesFromIdx(p);
                dfloat3 copy_vel_old = nSoA.vel_old.getValuesFromIdx(p);
                dfloat3 copy_f = nSoA.f.getValuesFromIdx(p);
                dfloat3 copy_deltaF = nSoA.deltaF.getValuesFromIdx(p);

                // Set device to move to (unnecessary)
                checkCudaErrors(cudaSetDevice(GPUS_TO_USE[node_gpu]));
                nSoA = this->nodesSoA[node_gpu];
                // Copy values to last position in nodesSoA
                size_t idxMove = nSoA.numNodes;
                nSoA.S[idxMove] = copy_S;
                nSoA.particleCenterIdx[idxMove] = copy_pIdx;
                nSoA.pos.copyValuesFromFloat3(copy_pos, idxMove);
                nSoA.vel.copyValuesFromFloat3(copy_vel, idxMove);
                nSoA.vel_old.copyValuesFromFloat3(copy_vel_old, idxMove);
                nSoA.f.copyValuesFromFloat3(copy_f, idxMove);
                nSoA.deltaF.copyValuesFromFloat3(copy_deltaF, idxMove);
                // Added one node to it
                this->nodesSoA[node_gpu].numNodes += 1;
                // Set back particle device (unnecessary)
                checkCudaErrors(cudaSetDevice(GPUS_TO_USE[real_gpu]));
                // printf("idx %d  gpu curr %d  ", p, n);
                // for(int nnn = 0; nnn < N_GPUS; nnn++){
                //     printf("Nodes GPU %d: %d\t", nnn, this->nodesSoA[nnn].numNodes);
                // }
                // printf("\n");
            }
            // Remove nodes that were added
            this->nodesSoA[real_gpu].numNodes -= left_shift;
        }
    }
    // int sum = 0;
    // for(int nnn = 0; nnn < N_GPUS; nnn++){
    //     sum += this->nodesSoA[nnn].numNodes;
    // }
    // printf("sum nodes %d\n\n", sum);
}


void ParticlesSoA::freeNodesAndCenters(){
    for(int i = 0; i < N_GPUS; i++){
        checkCudaErrors(cudaSetDevice(GPUS_TO_USE[i]));
        this->nodesSoA[i].freeMemory();
    }
    checkCudaErrors(cudaSetDevice(GPUS_TO_USE[0]));
    cudaFree(this->pCenterArray);
    free(this->pCenterLastPos);
    free(this->pCenterLastWPos);
    this->pCenterArray = nullptr;
}


void Particle::makeSpherePolar(dfloat diameter, dfloat3 center, unsigned int coulomb, bool move,
    dfloat density, dfloat3 vel, dfloat3 w)
{
    // Maximum number of layer of sphere
    //unsigned int maxNumLayers = 5000;
    // Number of layers in sphere
    unsigned int nLayer;
    // Number of nodes per layer in sphere
    unsigned int* nNodesLayer;
    // Angles in polar coordinates and node area
    dfloat *theta, *zeta, *S;

    dfloat phase = 0.0;

    // this->pCenter = ParticleCenter();

    // Define the properties of the particle
    dfloat r = diameter / 2.0;
    dfloat volume = r*r*r*4*M_PI/3;

    this->pCenter.radius = r;
    this->pCenter.volume = r*r*r*4*M_PI/3;
    // Particle area
    this->pCenter.S = 4.0 * M_PI * r * r;
    // Particle density
    this->pCenter.density = density;

    // Particle center position
    this->pCenter.pos = center;
    this->pCenter.pos_old = center;

    // Particle velocity
    this->pCenter.vel = vel;
    this->pCenter.vel_old = vel;

    // Particle rotation
    this->pCenter.w = w;
    this->pCenter.w_avg = w;
    this->pCenter.w_old = w;

    // Innertia momentum
    this->pCenter.I.x = 2.0 * volume * this->pCenter.density * r * r / 5.0;
    this->pCenter.I.y = 2.0 * volume * this->pCenter.density * r * r / 5.0;
    this->pCenter.I.z = 2.0 * volume * this->pCenter.density * r * r / 5.0;

    this->pCenter.movable = move;

    //breugem correction
    r -= BREUGEM_PARAMETER;

    // Number of layers in the sphere
    nLayer = (unsigned int)(2.0 * sqrt(2) * r / MESH_SCALE + 1.0); 

    nNodesLayer = (unsigned int*)malloc((nLayer+1) * sizeof(unsigned int));
    theta = (dfloat*)malloc((nLayer+1) * sizeof(dfloat));
    zeta = (dfloat*)malloc((nLayer+1) * sizeof(dfloat));
    S = (dfloat*)malloc((nLayer+1) * sizeof(dfloat));

    this->numNodes = 0;
    for (int i = 0; i <= nLayer; i++) {
        // Angle of each layer
        theta[i] = M_PI * ((double)i / (double)nLayer - 0.5); 
        // Determine the number of node per layer
        nNodesLayer[i] = (unsigned int)(1.5 + cos(theta[i]) * nLayer * sqrt(3)); 
        // Total number of nodes on the sphere
        this->numNodes += nNodesLayer[i]; 
        zeta[i] = r * sin(theta[i]); // Height of each layer
    }

    
    for (int i = 0; i < nLayer; i++) {
        // Calculate the distance to the south pole to the mid distance of the layer and previous layer
        S[i] = (zeta[i] + zeta[i + 1]) / 2.0 - zeta[0]; 
    }
    S[nLayer] = 2 * r;
    for (int i = 0; i <= nLayer; i++) {
        // Calculate the area of sphere segment since the south pole
        S[i] = 2 * M_PI * r * S[i]; 
    }
    for (int i = nLayer; i > 0; i--) {
        // Calculate the area of the layer
        S[i] = S[i] - S[i - 1];
    }
    S[0] = S[nLayer];
    

    this->nodes = (ParticleNode*) malloc(sizeof(ParticleNode) * this->numNodes);

    ParticleNode* first_node = &(this->nodes[0]);

    // South node - define all properties
    first_node->pos.x = 0;
    first_node->pos.y = 0;
    first_node->pos.z = r * sin(theta[0]);

    first_node->S = S[0];

    // Define node velocity
    first_node->vel.x = vel.x + w.y * first_node->pos.z - w.z * first_node->pos.y;
    first_node->vel.y = vel.y + w.z * first_node->pos.x - w.x * first_node->pos.z;
    first_node->vel.z = vel.z + w.x * first_node->pos.y - w.y * first_node->pos.x;

    first_node->vel_old.x = vel.x + w.y * first_node->pos.z - w.z * first_node->pos.y;
    first_node->vel_old.y = vel.y + w.z * first_node->pos.x - w.x * first_node->pos.z;
    first_node->vel_old.z = vel.z + w.x * first_node->pos.y - w.y * first_node->pos.x;

    int nodeIndex = 1;
    for (int i = 1; i < nLayer; i++) {
        if (i % 2 == 1) {
            // Calculate the phase of the segmente to avoid a straight point line
            phase = phase + M_PI / nNodesLayer[i];
        }

        for (int j = 0; j < nNodesLayer[i]; j++) {
            // Determine the properties of each node in the mid layers
            this->nodes[nodeIndex].pos.x = r * cos(theta[i]) * cos((dfloat)j * 2.0 * M_PI / nNodesLayer[i] + phase);
            this->nodes[nodeIndex].pos.y = r * cos(theta[i]) * sin((dfloat)j * 2.0 * M_PI / nNodesLayer[i] + phase);
            this->nodes[nodeIndex].pos.z = r * sin(theta[i]);

            // The area of sphere segment is divided by the number of node
            // in the layer, so all nodes have the same area in the layer
            this->nodes[nodeIndex].S = S[i] / nNodesLayer[i];

            // Define node velocity
            this->nodes[nodeIndex].vel.x = vel.x + w.y * this->nodes[nodeIndex].pos.z - w.z * this->nodes[nodeIndex].pos.y;
            this->nodes[nodeIndex].vel.y = vel.y + w.z * this->nodes[nodeIndex].pos.x - w.x * this->nodes[nodeIndex].pos.z;
            this->nodes[nodeIndex].vel.z = vel.z + w.x * this->nodes[nodeIndex].pos.y - w.y * this->nodes[nodeIndex].pos.x;

            this->nodes[nodeIndex].vel_old.x = vel.x + w.y * this->nodes[nodeIndex].pos.z - w.z * this->nodes[nodeIndex].pos.y;
            this->nodes[nodeIndex].vel_old.y = vel.y + w.z * this->nodes[nodeIndex].pos.x - w.x * this->nodes[nodeIndex].pos.z;
            this->nodes[nodeIndex].vel_old.z = vel.z + w.x * this->nodes[nodeIndex].pos.y - w.y * this->nodes[nodeIndex].pos.x;

            // Add one node
            nodeIndex++;
        }
    }

    // North pole -define all properties
    ParticleNode* last_node = &(this->nodes[this->numNodes-1]);
    
    last_node->pos.x = 0;
    last_node->pos.y = 0;
    last_node->pos.z = r * sin(theta[nLayer]);
    last_node->S = S[nLayer];
    // define last node velocity
    last_node->vel.x = vel.x + w.y * last_node->pos.z - w.z * last_node->pos.y;
    last_node->vel.y = vel.y + w.z * last_node->pos.x - w.x * last_node->pos.z;
    last_node->vel.z = vel.z + w.x * last_node->pos.y - w.y * last_node->pos.x;

    last_node->vel_old.x = vel.x + w.y * last_node->pos.z  - w.z * last_node->pos.y;
    last_node->vel_old.y = vel.y + w.z * last_node->pos.x  - w.x * last_node->pos.z;
    last_node->vel_old.z = vel.z + w.x * last_node->pos.y  - w.y * last_node->pos.x;

    unsigned int numNodes = this->numNodes;

    // Coulomb node positions distribution
    if (coulomb != 0) {
        dfloat3 dir;
        dfloat mag;
        dfloat scaleF;
        dfloat3* cForce;
        cForce = (dfloat3*)malloc(numNodes * sizeof(dfloat3));


        scaleF = 0.001;

        dfloat fx, fy, fz;

        for (unsigned int c = 0; c < coulomb; c++) {
            for (int i = 0; i < numNodes; i++) {
                cForce[i].x = 0;
                cForce[i].y = 0;
                cForce[i].z = 0;
            }

            for (int i = 0; i < numNodes; i++) {
                ParticleNode* node_i = &(this->nodes[i]);

                for (int j = i+1; j < numNodes; j++) {

                    ParticleNode* node_j = &(this->nodes[j]);

                    dir.x = node_j->pos.x - node_i->pos.x;
                    dir.y = node_j->pos.y - node_i->pos.y;
                    dir.z = node_j->pos.z - node_i->pos.z;

                    mag = (dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

                    cForce[i].x -= dir.x / mag;
                    cForce[i].y -= dir.y / mag;
                    cForce[i].z -= dir.z / mag;

                    cForce[j].x -= -dir.x / mag;
                    cForce[j].y -= -dir.y / mag;
                    cForce[j].z -= -dir.z / mag;
                }
            }
            for (int i = 0; i < numNodes; i++) {
                // Move particle
                fx = cForce[i].x / scaleF;
                fy = cForce[i].y / scaleF;
                fz = cForce[i].z / scaleF;
                
                ParticleNode* node_i = &(this->nodes[i]);

                node_i->pos.x += fx;
                node_i->pos.y += fy;
                node_i->pos.z += fz;

                // Return to sphere
                mag = sqrt(node_i->pos.x * node_i->pos.x 
                    + node_i->pos.y * node_i->pos.y 
                    + node_i->pos.z * node_i->pos.z);

                node_i->pos.x *= r / mag;
                node_i->pos.y *= r / mag;
                node_i->pos.z *= r / mag;
            }
        }

        // Area fix
        for (int i = 0; i < numNodes; i++) {
            ParticleNode* node_i = &(this->nodes[i]);

            node_i->S = this->pCenter.S / (numNodes);
        }

        // Free coulomb force
        free(cForce);

        dfloat dA =  this->pCenter.S/this->numNodes;
        for(int i = 0; i < numNodes; i++){
            this->nodes[i].S = dA;
        }
    }
    ParticleNode* node_i;
    for (int i = 0; i < numNodes; i++) {
        node_i = &(this->nodes[i]);
        node_i->pos.x += center.x;
        node_i->pos.y += center.y;
        node_i->pos.z += center.z;
    }

    /*for(int ii = 0;ii<nodeIndex;ii++){
        ParticleNode* node_j = &(this->nodes[ii]);
        printf("%f %f %f \n",node_j->pos.x,node_j->pos.y,node_j->pos.z );
    }*/

    // Free allocated variables
    free(nNodesLayer);
    free(theta);
    free(zeta);
    free(S);

    // Update old position value
    this->pCenter.pos_old = this->pCenter.pos;
}


void Particle::makeOpenCylinder(dfloat diameter, dfloat3 baseOneCenter, dfloat3 baseTwoCenter, bool pattern)
{
    // Define the properties of the cylinder
    dfloat r = diameter / 2.0;
    dfloat x = baseTwoCenter.x - baseOneCenter.x;
    dfloat y = baseTwoCenter.y - baseOneCenter.y;
    dfloat z = baseTwoCenter.z - baseOneCenter.z;
    dfloat length = sqrt (x*x +y*y+z*z);
    dfloat volume = r*r*M_PI*length;

    this->pCenter.radius = r;
    // Particle volume
    this->pCenter.volume = volume;
    // Particle area
    this->pCenter.S = 2.0*M_PI*r*length;

    // Particle center position
    this->pCenter.pos.x = (baseOneCenter.x + baseTwoCenter.x)/2;
    this->pCenter.pos.y = (baseOneCenter.y + baseTwoCenter.y)/2;
    this->pCenter.pos.z = (baseOneCenter.z + baseTwoCenter.z)/2;

    this->pCenter.movable = false;


    int nLayer, nNodesLayer;

    dfloat3* centerLayer;
    

    dfloat scale = MESH_SCALE;

    dfloat layerDistance = scale;
    if (pattern)
        layerDistance = scale*scale*sqrt(3)/4;

    //number of layers
    nLayer = (int)(length/layerDistance);
    //number of nodes per layer
    nNodesLayer = (int)(M_PI * 2.0 *r / scale);

    //total number of nodes
    this->numNodes = nLayer * nNodesLayer;

    this->nodes = (ParticleNode*) malloc(sizeof(ParticleNode) * this->numNodes);

    //layer center position step
    dfloat dx = x / nLayer;
    dfloat dy = y / nLayer;
    dfloat dz = z / nLayer;

    centerLayer = (dfloat3*)malloc((nLayer) * sizeof(dfloat3));
    for (int i = 0; i < nLayer ; i++) {
        centerLayer[i].x = baseOneCenter.x + dx * ((dfloat)i+0.5*scale);
        centerLayer[i].y = baseOneCenter.y + dy * ((dfloat)i+0.5*scale);
        centerLayer[i].z = baseOneCenter.z + dz * ((dfloat)i+0.5*scale);
    }

    //adimensionalise direction vector
    x = x / length;
    y = y / length;
    z = z / length;

    dfloat3 a1;//randon vector for perpicular direction
    a1.x = 0.9; 
    a1.y = 0.8; 
    a1.z = 0.7; 
    //TODO: it will work as long the created cyclinder does not have the same direction
    //      make it can work in any direction
    dfloat a1l = sqrt(a1.x * a1.x + a1.y * a1.y + a1.z * a1.z);
    a1.x = a1.x / a1l; 
    a1.y = a1.y / a1l; 
    a1.z = a1.z / a1l;

    //product of x with a1, v1 is the first axis in the layer plane
    dfloat3 v1;
    v1.x = (y * a1.z - z * a1.y);
    v1.y = (z * a1.x - x * a1.z);
    v1.z = (x * a1.y - y * a1.x);

    //product of x with v1, v2 is perpendicular to v1, creating the second axis in the layer plane
    dfloat3 v2;
    v2.x = (y * v1.z - z * v1.y);
    v2.y = (z * v1.x - x * v1.z);
    v2.z = (x * v1.y - y * v1.x);

    //calculate length and make v1 and v2 unitary
    dfloat v1l = sqrt(v1.x * v1.x + v1.y * v1.y + v1.z * v1.z);
    dfloat v2l = sqrt(v2.x * v2.x + v2.y * v2.y + v2.z * v2.z);

    v1.x = v1.x / v1l; 
    v1.y = v1.y / v1l; 
    v1.z = v1.z / v1l;

    v2.x = v2.x / v2l; 
    v2.y = v2.y / v2l; 
    v2.z = v2.z / v2l;

    int nodeIndex = 0;
    dfloat phase = 0;
    dfloat angle;
    for (int i = 0; i < nLayer; i++) {
        if (pattern) {
            phase = (i % 2) * M_PI / nNodesLayer;
        }
        for (int j = 0; j < nNodesLayer; j++) {
            angle = (dfloat)j * 2.0 * M_PI / nNodesLayer + phase;
            this->nodes[nodeIndex].pos.x = centerLayer[i].x + r * cos(angle) * v1.x + r * sin(angle) * v2.x; 
            this->nodes[nodeIndex].pos.y = centerLayer[i].y + r * cos(angle) * v1.y + r * sin(angle) * v2.y;
            this->nodes[nodeIndex].pos.z = centerLayer[i].z + r * cos(angle) * v1.z + r * sin(angle) * v2.z;

            this->nodes[i].S = this->pCenter.S/((dfloat)nNodesLayer * nLayer);

            nodeIndex++;
        }
    }
}



#endif // !IBM