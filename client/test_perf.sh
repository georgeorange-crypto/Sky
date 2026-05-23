time dd if=/dev/zero of=/mnt/skyfs_db/test_perf3.bin bs=8k count=5000 seek=0 &
time dd if=/dev/zero of=/mnt/skyfs_db/test_perf3.bin bs=8k count=5000 seek=5000 &
time dd if=/dev/zero of=/mnt/skyfs_db/test_perf3.bin bs=8k count=5000 seek=10000 &
time dd if=/dev/zero of=/mnt/skyfs_db/test_perf3.bin bs=8k count=5000 seek=15000 &

