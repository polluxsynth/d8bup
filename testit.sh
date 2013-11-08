#!/bin/sh
#

SAMPLEPARAMS="-b 16 -c 2 -s -r 44.1k"

log_and_print() {
  tee -a $LOGFILE
}

log() {
  cat >> $LOGFILE
}

makeraw() {
  for filename in $*; do
    infile=$filename
    outfile=$(basename $infile .wav).raw
    echo "Converting $infile to $outfile" | log
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
  echo "Test $testname: $testdescr" | log_and_print
  echo "Running $command" | log
  $command < $infile > $testfile 2>> $LOGFILE
  echo "Comparing result" | log
  cmp -b $testfile $reffile 2>&1 >> $LOGFILE 
  if [ $? -eq 0 ]; then
    echo "Test $testname OK" | log_and_print
  else
    echo "Test $testname FAILED" | log_and_print
    FAILED=y
  fi
}

LOGFILE=d8bup.log
rm $LOGFILE

echo "Running d8bupc tests" | log_and_print
echo "First create raw files" | log_and_print

makeraw 12345678.wav passthru.wav truncated.wav expanded.wav

run_test 1 "pass through using -t" "./d8bupc -t" 12345678.raw passthru.raw
run_test 2 "cut using -c" "./d8bupc -c 2" 12345678.raw truncated.raw
cp result.raw test.raw
run_test 3 "expand using -x" "./d8bupc -x 2" test.raw expanded.raw

if [ "$FAILED" ]; then
  echo "Something FAILED!" | log_and_print
else
  echo "All tests OK!" | log_and_print
fi
echo "Log in $LOGFILE"
