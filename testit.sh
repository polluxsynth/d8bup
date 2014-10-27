#!/bin/sh
#

SAMPLEPARAMS="-b 16 -c 2 -s -r 44.1k"

log_and_print() {
  tee -a $LOGFILE
}

log() {
  cat >> $LOGFILE
}

# Not used, moved to makefile
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
[ "$1" = "" ] || LOGFILE=$1
rm -f $LOGFILE

echo "Running d8bup tests" | log_and_print

run_test 1 "pass through using -t" "./d8bup -t" 0 12345678.raw passthru.raw

run_test 2 "cut using -c 2" "./d8bup -c 2" 0 12345678.raw truncated-2.raw
cp result.raw test.raw
run_test 3 "expand using -x 2" "./d8bup -x 2" 0 test.raw expanded-2.raw

run_test 4 "cut using -c 4" "./d8bup -c 4" 0 12345678.raw truncated-4.raw
cp result.raw test.raw
run_test 5 "expand using -x 4" "./d8bup -x 4" 0 test.raw expanded-4.raw

run_test 6 "cut using -c 6" "./d8bup -c 6" 0 12345678.raw truncated-6.raw
cp result.raw test.raw
run_test 7 "expand using -x 6" "./d8bup -x 6" 0 test.raw expanded-6.raw

run_test 8 "extract name using -n" "./d8bup -n" 0 12345678.raw 12345678.txt
run_test 9 "write to file" "./d8bup -t" 1 12345678.raw passthru.raw
run_test 10 "write to songname" "./d8bup -t" 2 12345678.raw passthru.raw

cat 12345678.raw 23456789.raw > combined.raw
run_test 11 "extract using -C 1" "./d8bup -t -C 1" 0 combined.raw passthru.raw

cat 23456789.raw 12345678.raw > combined.raw
run_test 12 "extract using -C 2" "./d8bup -t -C 2" 0 combined.raw passthru.raw

# -S -E is normally used on mixdowns, but since it delimits a file based on
# silence (zero bytes) we can use it on a backup file for test purposes.
# In this case, since the file starts with ordinary audio, with a slight
# amount of nice, the output will start from the same sample as the input.
# The output then ends with the pause after the initial (including name)
# data burst.
run_test 13 "-S -E options" "./d8bup -S -E" 0 12345678.raw se-options.raw

if [ "$FAILED" ]; then
  echo "Something FAILED!" | log_and_print
else
  echo "All tests OK!" | log_and_print
fi
echo "Log in $LOGFILE"
