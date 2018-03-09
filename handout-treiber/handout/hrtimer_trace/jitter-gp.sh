#!/bin/bash

echo set log y
echo set grid xtics ytics
# echo set xtics 100
# echo set ytics 10000
echo set title \"Abweichung - hrtimer: get_ktime - expires\"

while true; do

echo plot \"/debug/tracing/trace\" using \(\$8\) title \"min\" with lines, \"/debug/tracing/trace\" using \(\$9\) title \"max\" with lines, \"/debug/tracing/trace\" using \(\$6\) title \"akt\" with points
sleep 3

done

