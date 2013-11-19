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
  tofile=$4 # 0 use stdout, 1 use -o <filename>, 2 use song name
  infile=$5
  reffile=$6
  testfile=result.raw
  if [ $tofile = 2 ]; then
    # Extract song name and use as base name for output file.
    # Since song name is "12345678" and our input file is normally 12345678.raw,
    # expect to use 12345678-1.raw .
    testfile=$infile
    if [ -f $testfile ]; then
      testfile=$(basename $testfile .raw)-1.raw
    fi
  fi
  rm -f $testfile
  echo "Test $testname: $testdescr" | log_and_print
  echo "Running $command" | log
  if [ $tofile = 1 ]; then
    $command -o $testfile < $infile 2>> $LOGFILE
  elif [ $tofile = 2 ]; then
    $command -f < $infile 2>> $LOGFILE
  else
    $command < $infile > $testfile 2>> $LOGFILE
  fi
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

run_test 1 "pass through using -t" "./d8bupc -t" 0 12345678.raw passthru.raw
run_test 2 "cut using -c" "./d8bupc -c 2" 0 12345678.raw truncated.raw
cp result.raw test.raw
run_test 3 "expand using -x" "./d8bupc -x 2" 0 test.raw expanded.raw
run_test 4 "extract name using -n" "./d8bupc -n" 0 12345678.raw 12345678.txt
run_test 5 "write to file" "./d8bupc -t" 1 12345678.raw passthru.raw
run_test 6 "write to songname" "./d8bupc -t" 2 12345678.raw passthru.raw

if [ "$FAILED" ]; then
  echo "Something FAILED!" | log_and_print
else
  echo "All tests OK!" | log_and_print
fi
echo "Log in $LOGFILE"
