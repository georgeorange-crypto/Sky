gcc -fPIC -shared file_cache.c -o libfilecache.so -pthread

gcc main.c -o main_test -L. -lfilecache -pthread
