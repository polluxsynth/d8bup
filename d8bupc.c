#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define CHUNKSIZE 4096
#define SAMPLESIZE 4 /* 2 bytes per sample * 2 channels */
#define SAMPLERATE 44100
#define ONE_SECOND SAMPLERATE

#define NAMELEN 16 /* length of D8 name string */
#define NAME_OFFSET 11 /* #samples from 1. syncblip */

#define PLURAL(s) ((s) == 1 ? "" : "s")

/* Global debugging */
#define D(x)

#define SYNCBLIPSIZE 4 /* #samples */
static const char syncblip_data[] = "\x76\x53\x19\x52"
                                    "\x76\x53\x19\x52"
                                    "\x76\x53\x19\x52"
                                    "\x76\x53\x19\x52";

#define SYNCTONESIZE 4 /* #samples */
static const char synctone_data[] = "\x00\x00\x00\x00"
                                    "\x00\x10\x00\x10"
                                    "\x00\x10\x00\x10"
                                    "\x00\x10\x00\x10";

/* Scheme for how to extract name: for each stereo sample of 4
 * (i.e. SAMPLESIZE) bytes, extract byte #1 and byte #0 (i.e.
 * left channel, byte swapped), then wait for next sample */
static const how_name[] = { 1, 0, -1 };

static const char *tempfilename = "d8bup.tmp.raw";

struct stream
{
  char *buf;
  int fd;
  int bytecount;
  int bufptr;
  int eof;
};

struct sa_stream
{
  char *buf;
  struct stream *stream;
  int bytecount;
  int samplecount;
  int eof;
};

/* stream functions */

/* Read chunk to a struct stream */
int read_chunk(struct stream *stream)
{
  int res;

  stream->bytecount = 0;
  stream->bufptr = 0;
  while (1) {
    res = read(stream->fd, &stream->buf[stream->bytecount],
               CHUNKSIZE - stream->bytecount);
    if (res < 0) {
      if (errno == EINTR) /* interrupted system call */
        continue;
      else {
        perror("reading input stream");
        stream->eof = 1;
        break;
      }
    }
    if (stream->bytecount >= CHUNKSIZE)
      break;
    if (res == 0) {
      stream->eof = 1;
      break;
    }
    stream->bytecount += res;
  }
  return res;
}

int write_chunk(struct stream *stream)
{
  int res;
  int writeptr = 0;

  while (writeptr < stream->bufptr) {
    res = write(stream->fd, &stream->buf[writeptr], stream->bufptr - writeptr);
    if (res < 0) {
      if (errno != EINTR)
        return res;
    } else
      writeptr += res;
  }

  stream->bufptr = 0; /* ready for next chunk */
  return writeptr;
}

/* read bytes bytes from input stream */
/* CHUNK_SIZE must be a multiple of bytes */
int read_bytes(struct stream *stream, char *buf, int bytes)
{
  int res;

  if (stream->bytecount - stream->bufptr < bytes) {
    if (stream->eof)
      return 0;
    res = read_chunk(stream);
    if (res < 0)
      return res;
    if (stream->bytecount < bytes) /* must be at end of stream */
      return 0;
  }
  memcpy(buf, &stream->buf[stream->bufptr], bytes);
  stream->bufptr += bytes;
  return bytes;
}

/* write bytes to output stream */
/* CHUNK_SIZE must be a multiple of bytes */
int write_bytes(struct stream *stream, const char *buf, int bytes)
{
  memcpy(&stream->buf[stream->bufptr], buf, bytes);
  stream->bufptr += bytes;

  if (stream->bufptr < CHUNKSIZE)
    return 0;

  return write_chunk(stream);
}

/* flush output stream */
int flush(struct stream *stream)
{
  return write_chunk(stream); /* write final chunk */
}

/* misc structures and functions */ 

char *sampletime(int samples)
{
  char *ret = malloc(50);
  int minutes, seconds, sample_remain = samples;
  
  seconds = sample_remain / SAMPLERATE;
  sample_remain = sample_remain - seconds * SAMPLERATE; /* rimainder */
  minutes = seconds / 60;
  seconds = seconds - minutes * 60;

  sprintf(ret, "%d:%02d (%d sample%s)", minutes, seconds, samples,
          PLURAL(samples));

  return ret;
}

/* sample stream (sa_stream) functions */

int read_sample(struct sa_stream *sa_stream)
{
  int res;
  int size = 0;

  while (size < SAMPLESIZE) {
    res = read_bytes(sa_stream->stream, sa_stream->buf, SAMPLESIZE);
    if (res < 0)
      return res;
    if (res == 0) {
      sa_stream->eof = 1;
      break;
    }
    if (res > 0) {
      sa_stream->bytecount += res;
      size += res;
    }
  }
  if (size == SAMPLESIZE)
    sa_stream->samplecount++;

  return res;
}

int copy_sample(struct sa_stream *sa_stream)
{
  int res;

  res = write_bytes(sa_stream->stream, sa_stream->buf, SAMPLESIZE);
  if (res < 0)
    return res;
  sa_stream->bytecount += res;
  sa_stream->samplecount++;

  return 0;
}

int discard_sample(struct sa_stream *sa_stream)
{
  sa_stream->bytecount += SAMPLESIZE;
  sa_stream->samplecount++;

  return 0;
}

int output_samples(struct sa_stream *sa_stream, const char *buf, int samples)
{
  int res = write_bytes(sa_stream->stream, buf, samples * SAMPLESIZE);
  if (res < 0)
    return res;
  sa_stream->samplecount += samples;
  sa_stream->bytecount += samples * SAMPLESIZE;
}
  
int silence(struct sa_stream *sa_stream, int samples)
{
  const char quiet[4] = "\0\0\0"; /* 4 bytes of zeros */

  while (samples--) {
    output_samples(sa_stream, quiet, 1);
  }
}

struct match
{
  const char *string;
  int matchlen; /* length of string */
  int matchpoint; /* next point to match */
  int matchsample; /* which sample is at the start of the match */
};

int match(struct sa_stream *sa_stream, struct match *what)
{
  int size = SAMPLESIZE;

  if (memcmp(sa_stream->buf, &what->string[what->matchpoint], size) == 0) {
    what->matchpoint += size;
    if (what->matchpoint >= what->matchlen) {
      what->matchsample = sa_stream->samplecount - what->matchlen / SAMPLESIZE;
      what->matchpoint = 0;
      return 1; /* match */
    }
  } else { /* doesn't match */
    if (what->matchpoint != 0) {
      what->matchpoint = 0; /* start from the beginning of match string */
      return match(sa_stream, what); /* try from beginning of match string */
    }
  }

  return 0; /* no match */
}

struct extractor
{
  int length;
  int bytecount;
  int start_sample;
  int skip_first;
  const int *how;
  char *string;
};

int extract(struct sa_stream *input, struct extractor *extractor)
{
  int byteno;

  if (input->samplecount < extractor->start_sample) /* not yet there */
    return 0;

  if (extractor->bytecount >= extractor->length)
    return 1;

  for (byteno = 0; byteno < SAMPLESIZE; byteno++) {
    if (extractor->how[byteno] < 0) break; /* end of extractor string */
    if (extractor->bytecount == 0) { /* no copying started yet */
      if (byteno < extractor->skip_first)
        continue; /* skip skip_first bytes at start */
    }
    extractor->string[extractor->bytecount++] = 
      input->buf[extractor->how[byteno]];
    D(fprintf(stderr, "\nextract: sampleno %d, byteno %d, data %d", input->samplecount, byteno, input->buf[extractor->how[byteno]]));
    if (extractor->bytecount >= extractor->length) {
      extractor->string[extractor->bytecount] = '\0'; /* terminate it */
      return 1; /* all copied */
    }
  }
  return 0;
}

/* Make output file name from song name, considering cut (-c) option */
char *make_filename(const char *songname, int cut)
{
  static char filename[NAMELEN + 4 + 4 + 4 + 1];
                     /*          cut var ext nul */
                     /* e.g.    -4tr -1  .raw    */

  /* assert(strlen(songname) <= NAMELEN); */
  strncpy(filename, songname, NAMELEN);
  if (cut)
    sprintf(filename + strlen(filename), "-%dtr", cut * 2);

  int name_end = strlen(filename);
  int var = 0;
#define MAX_VARS 10 /* max tries */

  /* loop until a unique name found */
  while (1) {
     strcat(filename, ".raw");
     int try_fd = open(filename, O_RDONLY);
     if (try_fd < 0) {
       if (errno == ENOENT) break; /* file doesn't exist, so we're happy */
       if (var == MAX_VARS) return NULL; /* some error causes us to spin */
     } else
       close(try_fd);
     sprintf(filename + name_end, "-%d", ++var);
  }

  return filename;
}

/* trim spaces from end of string */
char *trim_space(char *s)
{
  int len = strlen(s);

  if (!len) return s; /* string has zero length, we're done */

  /* trim spaces from end of string */
  char *s_end = s + len;
  while (s_end-- > s) {
    if (*s_end != ' ')
      break;
  }
  s_end[1] = '\0';

  /* trim spaces from start */
  while (*s == ' ')
    s++;

  return s;
}

int xc_rangecheck(int *arg, const char *what)
{
  int val = *arg;
  if (val != 2 && val != 4 && val != 6) {
    fprintf(stderr, "%s requires argument 2, 4 or 6, aborting!\n", what);
    return 1;
  }
  *arg /= 2; /* 0 (for none), 1, 2 or 3 */
  return 0;
}

void usage(void)
{
  fprintf(stderr, "Usage: d8bup [options]\n"
                  "Filter D8 backup files from stdin to stdout\n"
                  "Options:\n"
                  "-s <sampleno>  Start output on sampleno\n"
                  "-m             Start output on sync tone\n"
                  "-t             (Trim) Output from sync tone to end of song\n"
                  "-x <2, 4 or 6> Expand output from given number of tracks\n"
                  "-c <2, 4 or 6> Cut output after given number of tracks\n"
                  "-z             Don't break input: read input until eof\n"
                  "-n             Output name to stdout, then exit\n"
                  "-o <filename>  Use specified filename instead of stdout\n"
                  "-h             This list\n"
                  "For -x, -c and -t, output an additional one second of "
                  "silence at end of file\n");
}

int main(int argc, char **argv)
{
  int argcount = 0; /* command line argument count */
  int searchpos = -1; /* set to search position when -s encountered */
  int start_on_sync = 0; /* set in all modes where we start on sync tone */
  int stop_on_song_end = 0; /* set for -t only */
  int expand = 0; /* !=0 when -x encountered */
  int cut = 0; /* !=0 when -c encountered */
  int dont_break_input = 0; /* set for -z mode */
  int name_only = 0; /* set for -n; output name then exit */
  int songname_as_filename = 0; /* set for -f */
  const char *filename = NULL; /* use specified file name instead of stdout */
  int output_fd = 1; /* default to stdout */
  
  while (argcount < argc) {
    if (argv[argcount][0] == '-') {
      switch (argv[argcount][1]) {
        case 's': searchpos = atoi(argv[++argcount]); break;
        case 'm': start_on_sync = 1; break;
        case 'x': expand = atoi(argv[++argcount]);
                  if (xc_rangecheck(&expand, "expand (-x)"))
                    return 1;
                  start_on_sync = 1; break;
        case 't': start_on_sync = 1; stop_on_song_end = 1; break;
        case 'c': cut = atoi(argv[++argcount]);
                  if (xc_rangecheck(&cut, "cut (-c)"))
                    return 1;
                  start_on_sync = 1; break;
        case 'z': dont_break_input = 1; break;
        case 'n': name_only = 1; break;
        case 'o': filename = argv[++argcount]; break;
        case 'f': songname_as_filename = 1; break;
        case 'h': /* fall through */
	default: usage(); return 0;
      }
    }
    ++argcount;
  }
  if (expand && cut) {
    fprintf(stderr, "may only specify one of -x -and -c\n");
    exit(1);
  }

  if (filename && songname_as_filename) {
    fprintf(stderr, "may only specify one of -f and -o\n");
    exit(1);
  }

  if (songname_as_filename)
  {
    filename = tempfilename;
    unlink(tempfilename);
  }

  if (filename) {
    output_fd = open(filename, O_CREAT | O_EXCL | O_WRONLY, 
                     S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (output_fd < 0) {
      perror("Creating output file");
      exit(1);
    }
  }

  struct stream *input_low, *output_low;

  input_low = malloc(sizeof(struct stream));
  memset(input_low, sizeof(struct stream), 0);
  input_low->fd = 0; /* stdin */
  input_low->buf = malloc(CHUNKSIZE);

  output_low = malloc(sizeof(struct stream));
  memset(output_low, sizeof(struct stream), 0);
  output_low->fd = output_fd; /* default stdout */
  output_low->buf = malloc(CHUNKSIZE);

  struct sa_stream *input, *output;

  input = malloc(sizeof(struct sa_stream));
  memset(input, sizeof(struct sa_stream), 0);

  input->buf = malloc(SAMPLESIZE);
  input->stream = input_low;

  output = malloc(sizeof(struct sa_stream));
  memset(output, sizeof(struct sa_stream), 0);

  output->buf = input->buf;
  output->stream = output_low;

  struct match *syncblip = malloc(sizeof(struct match));
  memset(syncblip, sizeof(struct match), 0);
  syncblip->string = syncblip_data;
  syncblip->matchlen = strlen(syncblip_data);

  struct match *synctone = malloc(sizeof(struct match));
  memset(synctone, sizeof(struct match), 0);
  synctone->string = synctone_data;
  synctone->matchlen = SYNCTONESIZE * SAMPLESIZE;

  struct extractor *extract_name = malloc(sizeof(struct extractor));
  memset(extract_name, sizeof(struct extractor), 0);
  extract_name->length = NAMELEN;
  extract_name->string = malloc(NAMELEN + 1);
  extract_name->how = how_name;
  extract_name->skip_first = 1; /* skip 1 byte in first sample */
  extract_name->start_sample = -1; /* not yet started */

  int done = 0; /* looping condition */
  int copying = 0; /* copying data from input to output stream */
  int start_copying = 0; /* trigger to start copying; reset once started */
  int stop_copying = 0; /* trigger to stop copying; reset once done */
  int syncblips = 0; /* # sync blips found in in put stream */
  int synctone_found = 0; /* set to 1 once sync tone found, and never reset */
  int blipsample = 0; /* sample no of latest sync blip */
  int song_delta = 0; /* length of song in samples */
  int delta = 0; /* distance between two previous syncblips */
  int found_name = 0; /* name string found */
  const char *songname = NULL;

  while (!done)
  {
    int res = read_sample(input);

    if (res < 0)
      return 1;

    if (input->eof)
      break;
    
    if (input->samplecount == searchpos)
      start_copying = 1;

    if (!synctone_found && match(input, synctone)) {
      fprintf(stderr, "\nFound synctone at %s", sampletime(input->samplecount));
      synctone_found = 1;
      if (start_on_sync) {
        silence(output, ONE_SECOND);
        /* restore part of sync tone that will be skipped due to matching */
        output_samples(output, synctone_data, SYNCTONESIZE-1);
        start_copying = 1;
      }
    }

    if (!found_name && syncblips == 1 && extract(input, extract_name)) {
      songname = trim_space(extract_name->string);
      fprintf(stderr, "\nSong name: \"%s\"", songname);
      found_name = 1;
      if (name_only) {
        printf("%s\n", songname);
        done = 1;
      }
      if (songname_as_filename) {
        filename = make_filename(songname, cut);
        if (!filename) {
          fprintf(stderr, "\nCan't create an output file: %s, aborting.", 
                  strerror(errno));
          exit(2);
        }
        fprintf(stderr, "\nWill use output file name %s", filename);
      }
    }

    if (match(input, syncblip)) { /* Found a syncblip */
      syncblips++;

#if 0 /* trigger on syncblip */
      if (start_on_sync && syncblips == 1)
        start_copying = 1;
#endif
      if (syncblips == 1)
        extract_name->start_sample = input->samplecount + NAME_OFFSET;

      delta = input->samplecount - blipsample;

      fprintf(stderr, "\nSyncblip at %s, segment length is %s", 
              sampletime(syncblip->matchsample),
              sampletime(delta));

      if (syncblips >= 4) { /* calculate song length */
        if (delta > song_delta)
          song_delta = delta; /* grab maximum of all deltas */
      }

      /* The following can only happen after >= 4 sync blips, so we know
       * song_delta has been set. */
      if (expand && syncblips - 3 == expand) {
        fprintf(stderr, "\nWill expand with silence and blips from %s",
                sampletime(input->samplecount));
        stop_copying = 1;
      }

      /* The following can only happen after >= 4 sync blips, so we know
       * song_delta has been set. */
      if (cut && syncblips - 3 == cut) {
        fprintf(stderr, "\nCutting input from %s, stopping output", 
                sampletime(input->samplecount));
        stop_copying = 1;
      }
      
      blipsample = input->samplecount;
    }

    if (stop_on_song_end && copying && syncblips >= 6 && 
        input->samplecount == blipsample + song_delta) {
      fprintf(stderr, "\nReached end of song at %s, stopping output",
              sampletime(input->samplecount));
      stop_copying = 1;
    }

    if (start_copying && !copying) {
      fprintf(stderr, "\nCopying to output from %s", sampletime(input->samplecount));
      copying = 1;
      start_copying = 0;
    }

    if (copying)
      copy_sample(output);

    if (stop_copying) {
      copying = stop_copying = 0;
      if (dont_break_input)
        break; /* don't consume any more input bytes */
    }
  }

  if (name_only)
    goto exit_ok;

  if (expand) expand = 4 - expand; /* output 3, 2 or 1 segment(s) of silence */
  while (expand--) {
    fprintf(stderr, "\nOutputting %s of silence", sampletime(song_delta));
    silence(output, song_delta);
    if (expand) { /* don't output blip after last expansion */
      fprintf(stderr, "\nOutputting sync blip");
      output_samples(output, syncblip_data, SYNCBLIPSIZE);
    }
  }

  if (expand || cut || stop_on_song_end)
    silence(output, ONE_SECOND);

  flush(output->stream); /* write final bytes */

  if (songname_as_filename) {
    if (rename(tempfilename, filename) < 0)
      perror("Renaming output file");
  }

  fprintf(stderr, "\nRead %d bytes, wrote %d bytes", input->bytecount, output->bytecount);
  fprintf(stderr, "\nRead %d samples, wrote %d samples", input->samplecount, output->samplecount);
  fprintf(stderr, "\nSong length is %s", sampletime(song_delta));

exit_ok:
  fprintf(stderr, "\n");
  return 0;
}
