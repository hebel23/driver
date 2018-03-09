# list_... Synchronisierung von Listenoperationen


# mit list_mutex.ko sieht man contended-Szenarien:

cd /debug/tracing
echo 'lock:*' > set_event
cat trace | grep rcul_list_lock | less


# lockdep:

cat /proc/lock_stat | less



