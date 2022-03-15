import os
import shutil
import sys

BASE_PATH = os.getcwd()
LBM_SRC = "LBM/src/CUDA/"
LBM_VARS = "LBM/src/"
MLBM_SRC = "MLBM/src/"
MLBM_VARS = "MLBM/"


def compile(folder: str, number: int):
    os.chdir(folder)
    command = f"bash compile.sh D3Q19 {number:03d}"
    os.system(command)
    os.chdir(BASE_PATH)

def cp_var(folder_from: str, folder_to: str, number: int) -> int:
    file_from = folder_from+f"var_{number:03d}.h"
    file_to = folder_to+"var.h"
    shutil.copy(file_from, file_to)

def main():
    for folder_var, folder_src in [(LBM_VARS, LBM_SRC), (MLBM_VARS, MLBM_SRC)]:
        for file in os.listdir(folder_var):
            if(not file.endswith(".h")):
                continue
            number = int(file[4:-2])
            cp_var(folder_var, folder_src, number)
            compile(folder_src, number)

if __name__ == "__main__":
    main()