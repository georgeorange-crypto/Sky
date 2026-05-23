mkdir -p test_files && cd test_files
for i in {0..511}; do
    dd if=/dev/urandom of=file_$i.bin bs=1M count=1 status=none
done
cd ..
