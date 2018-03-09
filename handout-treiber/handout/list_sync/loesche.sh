#!/bin/sh

zaehler=/sys/class/rcul/rcul1/delete

while true 
do 
  echo 100 > $zaehler; 
  sleep 1;
done; 


