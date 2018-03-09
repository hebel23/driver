#!/bin/sh

killall -9 loesche.sh
killall -9 schreibe.sh
killall -9 lese.sh
killall -9 zeige.sh

#rmmod list_spinlock
rmmod list_mutex
#rmmod list_rcu
#rmmod list_rwlock
#rmmod list_not_safe
