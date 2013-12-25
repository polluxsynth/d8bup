# Makefile for d8bup

BINARIES = d8bupc

TESTFILES = 12345678.raw 23456789.raw passthru.raw 
TESTFILES += truncated-2.raw expanded-2.raw 
TESTFILES += truncated-4.raw expanded-4.raw 
TESTFILES += truncated-6.raw expanded-6.raw 
TESTFILES += se-options.raw
SAMPLEPARAMS =-b 16 -c 2 -s -r 44.1k

# Log file for tests
LOGFILE = d8bup.log

# Make .raw files from .wav files
%.raw: %.wav
	sox $< $(SAMPLEPARAMS) -t raw $@

all: $(BINARIES) $(TESTFILES) test

.PHONY : test
test:
	@sh testit.sh $(LOGFILE)

clean:
	rm -f $(BINARIES) $(TESTFILES) $(LOGFILE)
	rm -f result.raw test.raw 12345678-1.raw combined.raw

