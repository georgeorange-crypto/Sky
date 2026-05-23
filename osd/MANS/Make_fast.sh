#g++  -mavx512f -fopenmp -march=native unmappingcpu_uint16.cpp -o unmapping_16 -fopenmp -lgomp -lpthread -O3

#gcc -mavx512f -fopenmp -march=native call_mapping_u16.c -o call_mapping -L. -Wl,-rpath,.  -lmapping_cpu_uint16 -fopenmp -lgomp -O3
#gcc -mavx512f -fopenmp -march=native call_unmapping_u16.c -o call_unmapping -L. -Wl,-rpath,.  -lmapping_cpu_uint16 -fopenmp -lgomp -O3
gcc  -fopenmp -march=native call_pans.c -o call_pans -L. -Wl,-rpath,.  -lpans_cpu -fopenmp -lgomp -O3
gcc  -fopenmp -march=native test_call_pans.c -o test_call_pans -L. -Wl,-rpath,.  -lpans_cpu_buf -fopenmp -lgomp -O3

gcc -fopenmp -march=native test_new_pans.c -o  test_new_pans -L. -Wl,-rpath,.  -lnew_pan -fopenmp -lgomp -O3

#/opt/gcc-11/bin/g++  -fopenmp -march=native compress.cpp -o compress_new -lpthread   -O3
#/opt/gcc-11/bin/g++  -fopenmp -march=native decompress.cpp -o decompress_new -lpthread   -O3
