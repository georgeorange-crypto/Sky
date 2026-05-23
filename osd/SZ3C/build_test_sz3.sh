gcc -fopenmp -march=native test_sz3c_compress.c -o  test_sz3c_compress -L. -Wl,-rpath,.  -lSZ3c -fopenmp -lgomp -O3
gcc -fopenmp -march=native test_sz3c_decompress.c -o  test_sz3c_decompress -L. -Wl,-rpath,.  -lSZ3c -fopenmp -lgomp -O3
