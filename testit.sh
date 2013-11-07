#!/bin/sh
#

SAMPLEPARAMS="-b 16 -c 2 -s -r 44.1k"

makeraw() {
  for filename in $*; do
    infile=$filename
    outfile=$(basename $infile .wav).raw
    echo "Converting $infile to $outfile"
    sox $infile $SAMPLEPARAMS -t raw $outfile
    shift
  done
}

run_test() {
  testname=$1
  testdescr=$2
  command=$3
  infile=$4
  reffile=$5
  testfile=result.raw
  echo "Test $testname: $testdescr"
  echo "Running $command"
  $command < $infile > $testfile
  echo "Comparing result"
  cmp -b $testfile $reffile
  if [ $? -eq 0 ]; then
    echo "Test $testname OK"
  else
    echo "Test $testname FAILED"
  fi
}

echo "Running d8bupc tests"
echo "First create raw files"

makeraw 12345678.wav passthru.wav truncated.wav expanded.wav

run_test 1 "pass through using -t" "./d8bupc -t" 12345678.raw passthru.raw
run_test 2 "cut using -c" "./d8bupc -c 2" 12345678.raw truncated.raw
cp result.raw test.raw
run_test 3 "expand using -x" "./d8bupc -x 2" test.raw expanded.raw
