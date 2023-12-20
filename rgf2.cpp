#include "rgf2.hpp"

// void printC(const float arr[], int size) {
//     for (int i = 0; i < size; ++i) {
//         std::cout << arr[i] << " ";
//     }
//     std::cout << std::endl;
// }

void rgf2sided(Matrix &A, Matrix &G, bool sym_mat, bool save_off_diag) {
    int processRank, blockSize, matrixSize;
    MPI_Comm_rank(MPI_COMM_WORLD, &processRank);

    A.getBlockSizeAndMatrixSize(blockSize, matrixSize);
    int nblocks = matrixSize / blockSize; // assume divisible
    int nblocks_2 = nblocks / 2;          // assume divisible

    if (processRank == 0) {
        rgf2sided_upperprocess(A, G, nblocks_2, sym_mat, save_off_diag);

        MPI_Recv((void *)(G.mdiag + nblocks_2 * blockSize * blockSize),
                 (nblocks_2)*blockSize * blockSize, MPI_FLOAT, 1, 0,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        MPI_Recv((void *)(G.updiag + nblocks_2 * blockSize * blockSize),
                 (nblocks_2 - 1) * blockSize * blockSize, MPI_FLOAT, 1, 1,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        MPI_Recv((void *)(G.lodiag + nblocks_2 * blockSize * blockSize),
                 (nblocks_2 - 1) * blockSize * blockSize, MPI_FLOAT, 1, 2,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    } else if (processRank == 1) {
        Matrix G_dup(matrixSize, G.getMat());
        G_dup.convertDenseToBlkTridiag(blockSize);
        rgf2sided_lowerprocess(A, G_dup, nblocks - nblocks_2, sym_mat,
                               save_off_diag);
        MPI_Send(
            (const void *)(G_dup.mdiag + nblocks_2 * blockSize * blockSize),
            (nblocks - nblocks_2) * blockSize * blockSize, MPI_FLOAT, 0, 0,
            MPI_COMM_WORLD);

        MPI_Send(
            (const void *)(G_dup.updiag + nblocks_2 * blockSize * blockSize),
            (nblocks - nblocks_2 - 1) * blockSize * blockSize, MPI_FLOAT, 0, 1,
            MPI_COMM_WORLD);

        MPI_Send(
            (const void *)(G_dup.lodiag + nblocks_2 * blockSize * blockSize),
            (nblocks - nblocks_2 - 1) * blockSize * blockSize, MPI_FLOAT, 0, 2,
            MPI_COMM_WORLD);
    }
}

void rgf2sided_upperprocess(Matrix &A, Matrix &G, int nblocks_2, bool sym_mat,
                            bool save_off_diag) {
    int blockSize, matrixSize;
    A.getBlockSizeAndMatrixSize(blockSize, matrixSize);

    int nblocks = matrixSize / blockSize;

    float *A_diagblk_leftprocess = A.mdiag;
    float *A_upperblk_leftprocess = A.updiag;
    float *A_lowerblk_leftprocess = A.lodiag;

    // Zero initialization
    float *G_diagblk_leftprocess =
        new float[(nblocks_2 + 1) * blockSize * blockSize]();
    float *G_upperblk_leftprocess =
        new float[nblocks_2 * blockSize * blockSize]();
    float *G_lowerblk_leftprocess =
        new float[nblocks_2 * blockSize * blockSize]();

    float *temp_result_1 = new float[blockSize * blockSize]();
    float *temp_result_2 = new float[blockSize * blockSize]();
    float *temp_result_3 = new float[blockSize * blockSize]();
    float *temp_result_4 = new float[blockSize * blockSize]();

    float *zeros = new float[blockSize * blockSize]();

    // Initialisation of g - invert first block
    A.invBLAS(blockSize, A_diagblk_leftprocess, G_diagblk_leftprocess);

    // Forward substitution
    for (int i = 1; i < nblocks_2; ++i) {
        A.mmmBLAS(blockSize,
                  A_lowerblk_leftprocess + (i - 1) * blockSize * blockSize,
                  G_diagblk_leftprocess + (i - 1) * blockSize * blockSize,
                  temp_result_1);

        A.mmmBLAS(blockSize, temp_result_1,
                  A_upperblk_leftprocess + (i - 1) * blockSize * blockSize,
                  temp_result_2);

        A.mmSub(blockSize, A_diagblk_leftprocess + i * blockSize * blockSize,
                temp_result_2, temp_result_2);

        A.invBLAS(blockSize, temp_result_2,
                  G_diagblk_leftprocess + i * blockSize * blockSize);
    }

    // Communicate the left connected block and receive the right connected
    // block
    MPI_Send((const void *)(G_diagblk_leftprocess +
                            (nblocks_2 - 1) * blockSize * blockSize),
             blockSize * blockSize, MPI_FLOAT, 1, 0, MPI_COMM_WORLD);

    MPI_Recv(
        (void *)(G_diagblk_leftprocess + nblocks_2 * blockSize * blockSize),
        blockSize * blockSize, MPI_FLOAT, 1, 0, MPI_COMM_WORLD,
        MPI_STATUS_IGNORE);

    // Connection from both sides of the full G
    A.mmmBLAS(blockSize,
              A_lowerblk_leftprocess + (nblocks_2 - 2) * blockSize * blockSize,
              G_diagblk_leftprocess + (nblocks_2 - 2) * blockSize * blockSize,
              temp_result_1);

    A.mmmBLAS(blockSize, temp_result_1,
              A_upperblk_leftprocess + (nblocks_2 - 2) * blockSize * blockSize,
              temp_result_2);

    A.mmmBLAS(blockSize,
              A_upperblk_leftprocess + (nblocks_2 - 1) * blockSize * blockSize,
              G_diagblk_leftprocess + (nblocks_2)*blockSize * blockSize,
              temp_result_3);

    A.mmmBLAS(blockSize, temp_result_3,
              A_lowerblk_leftprocess + (nblocks_2 - 1) * blockSize * blockSize,
              temp_result_4);

    A.mmSub(blockSize,
            A_diagblk_leftprocess + (nblocks_2 - 1) * blockSize * blockSize,
            temp_result_2, temp_result_2);

    A.mmSub(blockSize, temp_result_2, temp_result_4, temp_result_2);

    A.invBLAS(blockSize, temp_result_2,
              G_diagblk_leftprocess + (nblocks_2 - 1) * blockSize * blockSize);

    A.mmmBLAS(blockSize,
              G_diagblk_leftprocess + nblocks_2 * blockSize * blockSize,
              A_lowerblk_leftprocess + (nblocks_2 - 1) * blockSize * blockSize,
              temp_result_1);

    A.mmmBLAS(blockSize, temp_result_1,
              G_diagblk_leftprocess + (nblocks_2 - 1) * blockSize * blockSize,
              temp_result_2);

    A.mmSub(blockSize, zeros, temp_result_2,
            G_lowerblk_leftprocess + (nblocks_2 - 1) * blockSize * blockSize);

    if (sym_mat) {
        // matrix transpose
        A.transposeBLAS(
            blockSize,
            G_lowerblk_leftprocess + (nblocks_2 - 1) * blockSize * blockSize,
            G_upperblk_leftprocess + (nblocks_2 - 1) * blockSize * blockSize);
    } else {
        A.mmmBLAS(
            blockSize,
            G_diagblk_leftprocess + (nblocks_2 - 1) * blockSize * blockSize,
            A_upperblk_leftprocess + (nblocks_2 - 1) * blockSize * blockSize,
            temp_result_1);

        A.mmmBLAS(blockSize, temp_result_1,
                  G_diagblk_leftprocess + (nblocks_2)*blockSize * blockSize,
                  temp_result_2);

        A.mmSub(blockSize, zeros, temp_result_2,
                G_upperblk_leftprocess +
                    (nblocks_2 - 1) * blockSize * blockSize);
    }

    // Backward substitution
    for (int i = nblocks_2 - 2; i >= 0; i -= 1) {
        float *g_ii = G_diagblk_leftprocess + i * blockSize * blockSize;
        float *G_lowerfactor = new float[blockSize * blockSize];

        A.mmmBLAS(
            blockSize, G_diagblk_leftprocess + (i + 1) * blockSize * blockSize,
            A_lowerblk_leftprocess + i * blockSize * blockSize, temp_result_1);

        A.mmmBLAS(blockSize, temp_result_1, g_ii, G_lowerfactor);

        if (save_off_diag) {
            A.mmSub(blockSize, zeros, G_lowerfactor,
                    G_lowerblk_leftprocess + i * blockSize * blockSize);

            if (sym_mat) {
                // matrix transpose
                A.transposeBLAS(
                    blockSize,
                    G_lowerblk_leftprocess + (i)*blockSize * blockSize,
                    G_upperblk_leftprocess + (i)*blockSize * blockSize);
            } else {
                A.mmmBLAS(blockSize, g_ii,
                          A_upperblk_leftprocess + i * blockSize * blockSize,
                          temp_result_1);

                A.mmmBLAS(blockSize, temp_result_1,
                          G_diagblk_leftprocess +
                              (i + 1) * blockSize * blockSize,
                          temp_result_2);

                A.mmSub(blockSize, zeros, temp_result_2,
                        G_upperblk_leftprocess + i * blockSize * blockSize);
            }
        }

        A.mmmBLAS(blockSize, g_ii,
                  A_upperblk_leftprocess + i * blockSize * blockSize,
                  temp_result_1);

        A.mmmBLAS(blockSize, temp_result_1, G_lowerfactor, temp_result_2);

        A.mmAdd(blockSize, g_ii, temp_result_2,
                G_diagblk_leftprocess + i * blockSize * blockSize);

        delete[] G_lowerfactor;
    }

    memcpy(G.mdiag, G_diagblk_leftprocess,
           (nblocks_2 + 1) * blockSize * blockSize * sizeof(float));
    memcpy(G.updiag, G_upperblk_leftprocess,
           nblocks_2 * blockSize * blockSize * sizeof(float));
    memcpy(G.lodiag, G_lowerblk_leftprocess,
           nblocks_2 * blockSize * blockSize * sizeof(float));

    delete[] G_diagblk_leftprocess;
    delete[] G_upperblk_leftprocess;
    delete[] G_lowerblk_leftprocess;
    delete[] temp_result_1;
    delete[] temp_result_2;
    delete[] temp_result_3;
    delete[] temp_result_4;
    delete[] zeros;

    return;
}

void rgf2sided_lowerprocess(Matrix &A, Matrix &G, int nblocks_2, bool sym_mat,
                            bool save_off_diag) {
    int blockSize, matrixSize;
    A.getBlockSizeAndMatrixSize(blockSize, matrixSize);

    float *A_diagblk_rightprocess = A.mdiag + nblocks_2 * blockSize * blockSize;
    float *A_upperblk_rightprocess =
        A.updiag + (nblocks_2 - 1) * blockSize * blockSize;
    float *A_lowerbk_rightprocess =
        A.lodiag + (nblocks_2 - 1) * blockSize * blockSize;
    float *G_diagblk_rightprocess =
        new float[(nblocks_2 + 1) * blockSize * blockSize]();
    float *G_upperblk_rightprocess =
        new float[nblocks_2 * blockSize * blockSize]();
    float *G_lowerblk_rightprocess =
        new float[nblocks_2 * blockSize * blockSize]();

    float *temp_result_1 = new float[blockSize * blockSize]();
    float *temp_result_2 = new float[blockSize * blockSize]();

    float *temp_result_3 = new float[blockSize * blockSize]();
    float *temp_result_4 = new float[blockSize * blockSize]();
    float *zeros = new float[blockSize * blockSize]();

    A.invBLAS(blockSize,
              A_diagblk_rightprocess + (nblocks_2 - 1) * blockSize * blockSize,
              G_diagblk_rightprocess + nblocks_2 * blockSize * blockSize);

    // Forward substitution
    for (int i = nblocks_2 - 1; i >= 1; i -= 1) {
        A.mmmBLAS(blockSize,
                  A_upperblk_rightprocess + i * blockSize * blockSize,
                  G_diagblk_rightprocess + (i + 1) * blockSize * blockSize,
                  temp_result_1);

        A.mmmBLAS(blockSize, temp_result_1,
                  A_lowerbk_rightprocess + i * blockSize * blockSize,
                  temp_result_2);

        A.mmSub(blockSize,
                A_diagblk_rightprocess + (i - 1) * blockSize * blockSize,
                temp_result_2, temp_result_2);

        A.invBLAS(blockSize, temp_result_2,
                  G_diagblk_rightprocess + i * blockSize * blockSize);
    }

    // Communicate the right connected block and receive the right connected
    // block

    MPI_Recv((void *)(G_diagblk_rightprocess), blockSize * blockSize, MPI_FLOAT,
             0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    MPI_Send((const void *)(G_diagblk_rightprocess + 1 * blockSize * blockSize),
             blockSize * blockSize, MPI_FLOAT, 0, 0, MPI_COMM_WORLD);

    // Connection from both sides of the full G
    A.mmmBLAS(blockSize, A_lowerbk_rightprocess, G_diagblk_rightprocess,
              temp_result_1);

    A.mmmBLAS(blockSize, temp_result_1, A_upperblk_rightprocess, temp_result_2);

    A.mmmBLAS(blockSize, A_upperblk_rightprocess + (1) * blockSize * blockSize,
              G_diagblk_rightprocess + (2) * blockSize * blockSize,
              temp_result_3);

    A.mmmBLAS(blockSize, temp_result_3,
              A_lowerbk_rightprocess + (1) * blockSize * blockSize,
              temp_result_4);

    A.mmSub(blockSize, A_diagblk_rightprocess, temp_result_2, temp_result_2);

    A.mmSub(blockSize, temp_result_2, temp_result_4, temp_result_2);

    A.invBLAS(blockSize, temp_result_2,
              G_diagblk_rightprocess + (1) * blockSize * blockSize);

    // Compute the shared off-diagonal upper block
    A.mmmBLAS(blockSize, G_diagblk_rightprocess + 1 * blockSize * blockSize,
              A_lowerbk_rightprocess, temp_result_1);

    A.mmmBLAS(blockSize, temp_result_1, G_diagblk_rightprocess,
              G_lowerblk_rightprocess);

    if (sym_mat) {
        // matrix transpose
        A.transposeBLAS(blockSize, G_lowerblk_rightprocess,
                        G_upperblk_rightprocess);
    } else {
        A.mmmBLAS(blockSize, G_diagblk_rightprocess, A_upperblk_rightprocess,
                  temp_result_1);

        A.mmmBLAS(blockSize, temp_result_1,
                  G_diagblk_rightprocess + 1 * blockSize * blockSize,
                  G_upperblk_rightprocess);
    }

    // Backward substitution
    for (int i = 1; i < nblocks_2; ++i) {
        float *g_ii = G_diagblk_rightprocess + (i + 1) * blockSize * blockSize;
        float *G_lowerfactor = new float[blockSize * blockSize];

        A.mmmBLAS(blockSize, g_ii,
                  A_lowerbk_rightprocess + i * blockSize * blockSize,
                  temp_result_1);

        A.mmmBLAS(blockSize, temp_result_1,
                  G_diagblk_rightprocess + i * blockSize * blockSize,
                  G_lowerfactor);

        if (save_off_diag) {
            A.mmSub(blockSize, zeros, G_lowerfactor,
                    G_lowerblk_rightprocess + i * blockSize * blockSize);

            if (sym_mat) {
                // matrix transpose
                A.transposeBLAS(
                    blockSize,
                    G_lowerblk_rightprocess + i * blockSize * blockSize,
                    G_upperblk_rightprocess + i * blockSize * blockSize);
            } else {
                A.mmmBLAS(blockSize,
                          G_diagblk_rightprocess + i * blockSize * blockSize,
                          A_upperblk_rightprocess + i * blockSize * blockSize,
                          temp_result_1);

                A.mmmBLAS(blockSize, temp_result_1, g_ii, temp_result_2);

                A.mmSub(blockSize, zeros, temp_result_2,
                        G_upperblk_rightprocess + i * blockSize * blockSize);
            }
        }

        A.mmmBLAS(blockSize, G_lowerfactor,
                  A_upperblk_rightprocess + i * blockSize * blockSize,
                  temp_result_1);

        A.mmmBLAS(blockSize, temp_result_1, g_ii, temp_result_2);

        A.mmAdd(blockSize, g_ii, temp_result_2,
                G_diagblk_rightprocess + (i + 1) * blockSize * blockSize);
    }

    memcpy(G.mdiag + nblocks_2 * blockSize * blockSize,
           G_diagblk_rightprocess + blockSize * blockSize,
           nblocks_2 * blockSize * blockSize * sizeof(float));
    memcpy(G.updiag + (nblocks_2 - 1) * blockSize * blockSize,
           G_upperblk_rightprocess,
           nblocks_2 * blockSize * blockSize * sizeof(float));
    memcpy(G.lodiag + (nblocks_2 - 1) * blockSize * blockSize,
           G_lowerblk_rightprocess,
           nblocks_2 * blockSize * blockSize * sizeof(float));

    delete[] G_diagblk_rightprocess;
    delete[] G_upperblk_rightprocess;
    delete[] G_lowerblk_rightprocess;
    delete[] temp_result_1;
    delete[] temp_result_2;
    delete[] zeros;
    delete[] temp_result_3;
    delete[] temp_result_4;

    return;
}
