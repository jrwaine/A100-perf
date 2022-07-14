cd bin

./000sim_D3Q19_sm80 >000.txt
./001sim_D3Q19_sm80 >001.txt
./002sim_D3Q19_sm80 >002.txt
./003sim_D3Q19_sm80 >003.txt
./004sim_D3Q19_sm80 >004.txt
./005sim_D3Q19_sm80 >005.txt
./006sim_D3Q19_sm80 >006.txt
./007sim_D3Q19_sm80 >007.txt
./008sim_D3Q19_sm80 >008.txt
./009sim_D3Q19_sm80 >009.txt
./010sim_D3Q19_sm80 >010.txt
ncu -o A100F --set full ./020sim_D3Q19_sm80
ncu -o A100D --set full ./021sim_D3Q19_sm80