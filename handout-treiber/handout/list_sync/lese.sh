#!/bin/sh

zaehler=/sys/class/rcul/rcul1/messwert

while true 
do 
  cat $zaehler > /dev/null; 
done; 


