#g++  -mavx512f -fopenmp -march=native unmappingcpu_uint16.cpp -o unmapping_16 -fopenmp -lgomp -lpthread -O3

gcc -mavx512f -fopenmp -march=native call_mapping_u16.c -o call_mapping -L. -Wl,-rpath,.  -lmapping_cpu_uint16 -fopenmp -lgomp -O3
