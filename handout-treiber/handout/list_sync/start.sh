#!/bin/sh

#insmod list_spinlock.ko
insmod list_mutex.ko
#insmod list_rcu.ko
#insmod list_rwlock.ko
#insmod list_not_safe.ko

echo 0 > /proc/lock_stat

./loesche.sh &
./loesche.sh &
# ./schreibe.sh &
./lese.sh &
./lese.sh &

