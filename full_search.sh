#!/bin/bash
# For 100% read operation based 100 millions records inserted into the dataset

thread_num=(0 1 2 5 10 20 30 40 80)
workload=(0 50000000)
base=(0 1000000)

#delete the corresponding file

#rm -f dm_cceh_search.txt
#rm -f dm_cceh_search_base.txt
#rm -f dm_level_search.txt
#rm -f pm_level_search.txt
#rm -f pm_cceh_search_base.txt
#rm pm_spinlock2.txt

rm -f /mnt/pmem0/pmem_hash.data
rm -f /mnt/pmem0/pmem_cceh.data

#{1..6}
for i in 1
do 
	for j in 1
	do
		echo "Begin: ${base[1]} ${workload[${i}]} ${thread_num[${j}]}"
		numaarg=""
		if [ ${thread_num[$j]} -le 24 ]
		then
			numaarg="--cpunodebind=0 --membind=0 --physcpubind=0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23"
		elif [ ${thread_num[$j]} -le 48 ]
		then
			numaarg="--cpunodebind=0,1 --membind=0,1 --physcpubind=0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,2728,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47"
		fi
		echo $numaarg
#		OMP_PLACES=threads OMP_PROC_BIND=true OMP_NESTED=true PMEM_IS_PMEM_FORCE=1 LD_PRELOAD=/usr/lib/libjemalloc.so numactl $numaarg ./new-src/cceh_bench 2 ${workload[$i]} ${thread_num[$j]} 
		OMP_PLACES=threads OMP_PROC_BIND=true OMP_NESTED=true PMEM_IS_PMEM_FORCE=1 numactl $numaarg ./src/Level/test_level 10 ${workload[$i]} ${thread_num[$j]} #>> cuckoo_finger.txt
		printf "Done for cceh dm uni: %d %d\n" ${workload[$i]} ${thread_num[$j]}
	done
done

#./extract_plot2.sh
# OMP_PROC_BIND=true
