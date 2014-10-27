d8bup
=====

d8bup is a Linux command line tool for use primarily to process backup and
restore files sent to and received from the Korg D8 digital recording studio
via the S/PDIF interface.

The Korg D8 S/PDIF backup format is rather simply: basically, first, there is a
'sync tone', followed by some initial digital data describing the format of the
song, including the name, followed by the tracks in pairs, each pair of tracks
prefixed by a small click (termed a 'syncblip' in this documentation) in one
channel.

While the data can be saved as standard .wav-files, it can be wasteful
on disk space if all 8 tracks are not used. d8bup therefore includes
functions to cut the output (when reading from the D8) after a number of
track pairs, or to expand a backup file (when sending to the D8) with
empty (silent) tracks.

There are also a couple of useful features such as skipping a number of songs
before outputting data, which can be useful for restoring the nth song from a
file made of a backup DAT tape with several songs on it. The name of a song
is always displayed when reading a backup file; a special option allows
just the song name to be printed and the program then exits.
Finally there are truncate options which trim off zeroes at the start and
end of audio files.

Since d8bup operates as a filter, proceessing data from stdin to stdout, all
data must be in raw (16 bit signed little endian) format, rather than .wav
files. For converting to .wav files, use the standard sox (sound exchange)
program under Linux.

The source code includes a test suite which automatically does regression
testing when using Make.

    Usage: d8bup [options]
    Filter D8 backup files from stdin to stdout
    Options:
    -s <sampleno>  Start output on sampleno
    -m             Start output on sync tone
    -t             (Trim) Output from sync tone to end of song
    -x <2, 4 or 6> Expand output from given number of tracks
    -c <2, 4 or 6> Cut output after given number of tracks
    -z             Don't break input: read input until eof
    -n             Output name to stdout, then exit
    -o <filename>  Use specified filename instead of stdout
    -C <n>         Skip songs until song n found (n = 1,2,..)
    -S             Start when any input sample != 0
    -E             End when 1s of silence detected
    -h             This list
    For -x, -c and -t, output an additional one second of silence at end of file.
