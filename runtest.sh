#!/bin/bash
INTERP=/home/lmc123456/RISC-V-Tomasulo-CPU-Simulator/naive_interpreter
TESTDIR=/home/lmc123456/RISC-V-Tomasulo-CPU-Simulator/data/testcases

for f in $TESTDIR/*.data; do
    name=$(basename $f)
    result=$($INTERP $f)
    printf "%-25s %s\n" "$name" "$result"
done

echo ""
echo "--- sample ---"
$INTERP /home/lmc123456/RISC-V-Tomasulo-CPU-Simulator/data/sample/sample.data
