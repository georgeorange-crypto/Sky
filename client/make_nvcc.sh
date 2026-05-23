 nvcc -arch=sm_89 -o zstd_gpu_comp   zstd_cpu_decompression.cu  -lzstd -lnvcomp -L/usr/lib/x86_64-linux-gnu/nvcomp/12/  -I/usr/include/nvcomp_12 -I /usr/local/cuda/targets/x86_64-linux/include/

## make libso
 nvcc -arch=sm_89 -shared -o libzstd_gpu_comp.so  zstd_cpu_decompression_so.cu -Xcompiler -fPIC  -lzstd -lnvcomp -L/usr/lib/x86_64-linux-gnu/nvcomp/12/  -I/usr/include/nvcomp_12 -I /usr/local/cuda/include/

 # make executable file with libzstd_gpu_comp.so
 #gcc -o main main.c -L. -lcusquare -lcuda -I/usr/local/cuda/include  

#real so
nvcc -arch=sm_89 -shared -o libzstd_gpu_nvcomp.so  -Xcompiler -fPIC   zstd_gpu_comp_so.cu  -lzstd -lnvcomp -L/usr/lib/x86_64-linux-gnu/nvcomp/12/  -I/usr/include/nvcomp_12 -I /usr/local/cuda/targets/x86_64-linux/include/


gcc -ogpu_compress_file  gpu_compress_file.c -L. -lzstd_gpu_comp  -lzstd -lnvcomp -L/usr/lib/x86_64-linux-gnu/nvcomp/12/

# 对于 Linux，指定库路径和CUDA头文件路径。Windows下可能需要指定库路径和链接器选项。例如：gcc main.c -LC:\path\to\your\library -lcusquare -lcuda -I"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.1\include" -L"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.1\lib\x64" -lcudart  # 注意替换路径为你的CUDA安装路径和版本。



