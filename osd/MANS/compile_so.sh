/opt/gcc-11/bin/g++   -fopenmp -mavx512f -march=native -fPIC -shared mappingcpu_uint16_so.cpp -o libmapping_cpu_uint16.so -fopenmp -lgomp  -O3
#/opt/gcc-11/bin/g++   -fopenmp -march=native -fPIC -shared compress_pans_buf_so.cpp -o libcompress_pans_buf -fopenmp -lgomp  -O3
#/opt/gcc-11/bin/g++  -mavx512f  -march=native -fPIC -shared mappingcpu_uint16_so.cpp -o libmapping_cpu_uint16.so  -O3
