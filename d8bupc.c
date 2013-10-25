#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define CHUNKSIZE 4096
#define SAMPLESIZE 4 /* 2 bytes per sample * 2 channels */
#define SAMPLERATE 44100

static const char syncblip_data[] = "\x76\x53\x19\x52"
                                    "\x76\x53\x19\x52"
                                    "\x76\x53\x19\x52"
                                    "\x76\x53\x19\x52";


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
int write_bytes(struct stream *stream, char *buf, int bytes)
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

struct match
{
  const char *string;
  int matchlen; /* length of string */
  int matchpoint; /* next point to match */
  int matchsample; /* which sample is at the start of the match */
};


char *sampletime(int sample)
{
  char *ret = malloc(50);
  int minutes, seconds, sample_remain = sample;
  
  seconds = sample_remain / SAMPLERATE;
  sample_remain = sample_remain - seconds * SAMPLERATE; /* rimainder */
  minutes = seconds / 60;
  seconds = seconds - minutes * 60;

  sprintf(ret, "sample %d, time %d:%02d", sample, minutes, seconds);

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


int match(struct sa_stream *sa_stream, struct match *what)
{
  int size = SAMPLESIZE;

  if (memcmp(sa_stream->buf, what->string, size) == 0) {
    what->matchpoint += size;
    if (what->matchpoint >= what->matchlen) {
      what->matchsample = sa_stream->samplecount - what->matchlen / SAMPLESIZE;
      return 1; /* match */
    }
  } else /* doesn't match */
    what->matchpoint = 0; /* start from the beginning of match string */

  return 0; /* no match */
}



int main(int argc, char **argv)
{
  int argcount = 0;
  int searchpos = -1;
  int start_on_sync = 0;
  
  while (argcount < argc) {
    if (argv[argcount][0] == '-') {
      switch (argv[argcount][1]) {
        case 's': searchpos = atoi(argv[++argcount]); break;
        case 'm': start_on_sync = 1; break;
        case 'h': 
	default: printf("Usage: d8bup [options]\nfilter stdin to stdout\n");
		 return 0;
      }
    }
    ++argcount;
  }

  struct stream *input_low, *output_low;

  input_low = malloc(sizeof(struct stream));
  memset(input_low, sizeof(struct stream), 0);
  input_low->fd = 0; /* stdin */
  input_low->buf = malloc(CHUNKSIZE);

  output_low = malloc(sizeof(struct stream));
  memset(output_low, sizeof(struct stream), 0);
  output_low->fd = 1; /* stdout */
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

  int done = 0;
  int copying = 0;
  int get_started = 0;
  while (!done)
  {
    int res = read_sample(input);

    if (res < 0)
      return 1;

    if (input->eof)
      break;
    
    if (input->samplecount == searchpos)
      get_started = 1;

    if (match(input, syncblip)) {
      fprintf(stderr, "Found syncblip at %s\n", sampletime(syncblip->matchsample));
      if (start_on_sync)
        get_started = 1;
    }

    if (get_started && !copying) {
      fprintf(stderr, "Copying to output from %s\n", sampletime(input->samplecount));
      copying = 1;
      get_started = 0;
    }

    if (copying)
      copy_sample(output);

  }

  flush(output->stream); /* write final bytes */


  fprintf(stderr, "Read %d bytes, wrote %d bytes\n", input->bytecount, output->bytecount);
  fprintf(stderr, "Read %d samples, wrote %d samples\n", input->samplecount, output->samplecount);

  


  return 0;
}
