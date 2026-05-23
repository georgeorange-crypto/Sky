/opt/gcc-11/bin/g++ -fopenmp -march=native -mavx512f -fPIC -shared mappingcpu_uint16_so.cpp -o libmapping_cpu_uint16.so -fopenmp -lgomp -lpthread -O3
cp libmapping_cpu_uint16.so /usr/lib64/
cp libmapping_cpu_uint16.so /usr/lib/
