#!/bin/sh

messwert=/sys/class/rcul/rcul1/messwert
i=1

while true 
do 
  echo $i > $messwert;
  i=$[$i+1];
  sleep 1;
done; 


