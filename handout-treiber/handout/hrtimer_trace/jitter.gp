#!/bin/bash

set xlabel "Timer-Interrupts"
set ylabel "zeitliche Abweichung [ns]"

set log y
set grid xtics ytics
# set xtics 2000
# set ytics 2000
set title "Jitter - hrtimer: get_ktime - expires"

plot "/debug/tracing/trace" using ($8) title "min" with lines, "/debug/tracing/trace" using ($9) title "max" with lines, "/debug/tracing/trace" using ($6) title "akt" with points

pause -1 "Hit <ENTER> to continue"

set terminal svg #  size 800,600
set output "jitter.svg"
replot

set terminal png size 800,600
set output "jitter.png"
replot

