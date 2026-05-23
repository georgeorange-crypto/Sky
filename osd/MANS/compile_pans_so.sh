#/opt/gcc-11/bin/g++    -march=native -fPIC -shared compress_pans_so.cpp -o libpans_cpu.so  -O3
#/opt/gcc-11/bin/g++    -march=native -lstdc++ -fPIC -shared compress_buf_so.cpp -o libpans_cpu_buf.so  -O3
/opt/gcc-11/bin/g++   -fopenmp -march=native -fPIC -shared new_comp_so.cpp -o libnew_pan.so -fopenmp -lgomp  -O3
/opt/gcc-11/bin/g++   -fopenmp -march=native -fPIC -shared pans_so.cpp -o libpans.so -fopenmp -lgomp  -O3

#/opt/gcc-11/bin/g++  -mavx512f  -march=native -fPIC -shared mappingcpu_uint16_so.cpp -o libmapping_cpu_uint16.so  -O3
#/opt/gcc-11/bin/g++  -mavx512f -fopenmp -march=native -fPIC -shared compress_pans.cpp -o libpans_cpu.so -fopenmp -lgomp -O3
