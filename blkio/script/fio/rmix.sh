source conf.sh

test_name=randrw
blk_size=4K
pread=50 #50% read, %50 write
if [ $# -ge 2 ]; then
test_nr_jobs=$1
fi
if [ $# -ge 3 ]; then
test_io_depth=$2
fi
logfile=log/$test_name.log
echo "" > $logfile

fio \
-cpus_allowed=$test_cpus_allow \
--output=$logfile \
-time_based=$test_group_reporting \
-ioengine=$test_ioengine \
-name=$test_name \
-rw=$test_name \
-filename=$test_file_name \
-size=$test_file_size \
-direct=$test_direct \
-bs=$blk_size \
-rwmixread=$pread \
-numjobs=$test_nr_jobs \
--terse-version=3 \
--minimal \
-iodepth=$test_io_depth \
-time_based=$test_time_based \
-runtime=$test_time

#cat $logfile
\cp -f $logfile log/$test_name"_"$test_nr_jobs"_"$test_io_depth".log"
./log/pmix.sh $logfile
