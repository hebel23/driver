#!/bin/sh

mkfifo /tmp/f
head -n4 lock_stat > /tmp/f
echo > /tmp/f
cat lock_stat | grep rcul > /tmp/f

