#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  int samplecount;
  int eof;
};

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


int read_sample(struct stream *stream)
{
  int res;
  int size = 0;

  while (size < SAMPLESIZE) {
    res = read(stream->fd, stream->buf, SAMPLESIZE);
    if (res < 0)
      return res;
    if (res == 0) {
      stream->eof = 1;
      break;
    }
    if (res > 0) {
      stream->bytecount += res;
      size += res;
    }
  }
  if (size == SAMPLESIZE)
    stream->samplecount++;

  return res;
}


int copy_sample(struct stream *stream)
{
  int size = SAMPLESIZE;
  int res;

  while (size) {
    res = write(stream->fd, stream->buf, size);
    if (res < 0)
      return res;
    stream->bytecount += res;
    size -= res;
  }
  stream->samplecount++;
  return 0;
}

int discard_sample(struct stream *stream)
{
  stream->bytecount += SAMPLESIZE;
  stream->samplecount++;

  return 0;
}


int match(struct stream *stream, struct match *what)
{
  int size = SAMPLESIZE;

  if (memcmp(stream->buf, what->string, size) == 0) {
    what->matchpoint += size;
    if (what->matchpoint >= what->matchlen) {
      what->matchsample = stream->samplecount - what->matchlen / SAMPLESIZE;
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

  struct stream *input, *output;

  input = malloc(sizeof(struct stream));
  memset(input, sizeof(struct stream), 0);

  input->buf = malloc(SAMPLESIZE);
  input->fd = 0; /* stdin */

  output = malloc(sizeof(struct stream));
  memset(output, sizeof(struct stream), 0);

  output->fd = 1; /* stdout */
  output->buf = input->buf;

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


  fprintf(stderr, "Read %d bytes, wrote %d bytes\n", input->bytecount, output->bytecount);
  fprintf(stderr, "Read %d samples, wrote %d samples\n", input->samplecount, output->samplecount);

  


  return 0;
}
