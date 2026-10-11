if [ $# -eq 0 ]; then
	echo "input a devie"
	exit
fi
dev=$1
while true
do
t1=`grep -w $dev /proc/diskstats | awk '{print $NF}'`
sleep 1
t2=`grep -w $dev /proc/diskstats | awk '{print $NF}'`
dt=`expr $t2 - $t1`
dt=`expr $dt / 1000`
printf "ov:%d\n" $dt
done
