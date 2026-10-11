#!/bin/bash
root=`dirname $0`

#save test result
result="result_20$(date "+%y-%m-%d_%H:%M:%S")"
echo "" > $result


test_names=(randread randwrite read write)
blk_sizes=(4K 4K 128K 128K)

test_max_cpu=`nproc --all`
test_min_cpu=1
test_cpus_allowed=`cat /sys/devices/system/cpu/online`
test_cpus_allowed_policy="split"
test_file_size=100%
test_file_name="/dev/nvme0n1"
test_time=300
test_ioengine=sync
test_nr_jobs=1
test_io_depth=1
test_cpus_allow=`cat /sys/devices/system/cpu/online`
test_direct=1
test_time_based=1
test_group_reporting=1

for ((j=0;j<${#test_names[*]};j=j+1))
do
	test_name=${test_names[$j]}
	blk_size=${blk_sizes[$j]}

	for ((i=$test_min_cpu; i<=$test_max_cpu; i+=2))
	do
		if [ $i -eq 3 ]; then
			let "i=2"
		fi
		let "test_nr_jobs=i"
		logfile=log/$test_name"_"$test_nr_jobs"_"$test_io_depth".log"

		echo "20$(date "+%y-%m-%d %H:%M:%S") start $logfile" 2>&1|tee -a $result

		fio \
		-cpus_allowed=0-39 \
		-cpus_allowed_policy=split \
		--output=$logfile \
		-group_reporting=$test_group_reporting \
		-ioengine=$test_ioengine \
		-name=$test_name \
		-rw=$test_name \
		-filename=$test_file_name \
		-size=$test_file_size \
		-direct=$test_direct \
		-bs=$blk_size \
		-numjobs=$test_nr_jobs \
		--terse-version=3 \
		--minimal \
		-iodepth=$test_io_depth \
		-time_based=$test_time_based \
		-runtime=$test_time

		cat $logfile 2>&1|tee -a $result
		echo "<------ summary $test_name $test_nr_jobs $test_io_depth ------>" 2>&1|tee -a $result
		$roo$root/parse_fio_terse.sh $logfile 2>&1|tee -a $result

		echo "20$(date "+%y-%m-%d %H:%M:%S") finish $logfile" 2>&1|tee -a $result
		echo -e "\n\n\n\n" 2>&1|tee -a $result
		sleep 120
	done
done

wait
echo "Test Done"
