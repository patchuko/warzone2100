#ifdef WZ_CDA

#include <SDL/SDL.h>

#else

#include <AL/al.h>

#ifndef WZ_NOMP3
#include <mad.h>
#endif

#ifndef WZ_NOOGG
#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
#include <vorbis/vorbisfile.h>
#endif

#endif

#include "frame.h"
#include "audio.h"
#include "cdaudio.h"

#ifdef WZ_CDA

SDL_CD	*cdAudio_dev;

#else

extern BOOL		openal_initialized;

#define NB_BUFFERS  6
#define BUFFER_SIZE (16384)

static BOOL		music_initialized;

static FILE*		music_file = NULL;

enum {	WZ_NONE,
	WZ_MP3,
	WZ_OGG }	music_file_format;

#ifndef WZ_NOMP3
#define MP3_BUFFER_SIZE (16384)

static struct mad_stream mp3_stream;
static struct mad_frame mp3_frame;
static struct mad_synth mp3_synth;

static char		mp3_buffer[MP3_BUFFER_SIZE + MAD_BUFFER_GUARD];
static unsigned int	mp3_buffer_length;
static unsigned int	mp3_size;
static unsigned int	mp3_pos_in_frame;
#endif

#ifndef WZ_NOOGG
static OggVorbis_File	ogg_stream;
static vorbis_info*	ogg_info;
#endif

static ALfloat		music_volume = 0.5;

static char		music_data[BUFFER_SIZE];

static unsigned int	music_track = 0;
static ALuint 		music_buffers[NB_BUFFERS];
static ALuint		music_source;
static ALenum		music_format;
static unsigned int	music_rate;

#endif

void PlayList_Init();
char PlayList_Read(const char* path);
void PlayList_SetTrack(unsigned int t);
char* PlayList_CurrentSong();
char* PlayList_NextSong();
void PlayList_DeleteCurrentSong();

//*
//
// cdAudio Subclass procedure

//*
// ======================================================================
// ======================================================================
//
BOOL cdAudio_Open( char* user_musicdir )
{
#ifdef WZ_CDA
	if ( !SDL_CDNumDrives() )
	{
		printf( "No CDROM devices available\n" );
		return FALSE;
	}

	cdAudio_dev = SDL_CDOpen( 0 );
	if ( !cdAudio_dev )
	{
		printf( "Couldn't open drive: %s\n", SDL_GetError() );
		return FALSE;
	}

	SDL_CDStatus( cdAudio_dev );

	return TRUE;
#else
	if (!openal_initialized) {
		return FALSE;
	}

	alGenBuffers(NB_BUFFERS, music_buffers);
	alGenSources(1, &music_source);
	alSourcef (music_source, AL_GAIN, music_volume);
	alSource3f(music_source, AL_POSITION, 0.0, 0.0, 0.0);
	alSource3f(music_source, AL_VELOCITY, 0.0, 0.0, 0.0);
	alSourcef (music_source, AL_ROLLOFF_FACTOR, 0.0);
	alSourcei (music_source, AL_SOURCE_RELATIVE, AL_TRUE);

	PlayList_Init();
	if (   (   user_musicdir == NULL
		|| PlayList_Read(user_musicdir))
	    && PlayList_Read("music")) {
		return FALSE;
	}
	
	music_initialized = TRUE;

	return TRUE;
#endif
}

//*
// ======================================================================
// ======================================================================
//
BOOL cdAudio_Close( void )
{
#ifdef WZ_CDA
	if ( cdAudio_dev != NULL )
	{
		SDL_CDClose( cdAudio_dev );
	}

	return TRUE;
#else
	return TRUE;
#endif
}

#ifndef WZ_NOMP3

void mp3_refill() {
	while (mad_frame_decode(&mp3_frame, &mp3_stream)) {
		int size;

		if (mp3_stream.error == MAD_ERROR_BUFLEN) {
			int offset;

			if (   music_file == NULL
			    || feof(music_file)) {
				return;
			}

			if (!mp3_stream.next_frame) {
				offset = 0;
				memset(mp3_buffer, 0, MP3_BUFFER_SIZE + MAD_BUFFER_GUARD);
			} else {
				offset = mp3_stream.bufend - mp3_stream.next_frame;
				memcpy(mp3_buffer, mp3_stream.next_frame, offset);
			}

			size = fread(mp3_buffer + offset, 1,
				     MP3_BUFFER_SIZE - offset, music_file);

			if (size <= 0) {
				return;
			}
			mp3_stream.error = (enum mad_error)0;

			// Feed the data we just read into the stream decoder
			mad_stream_buffer(&mp3_stream, mp3_buffer, size + offset);
		} else if (!MAD_RECOVERABLE(mp3_stream.error)) {
			return;
		}
	}

	// Synthesise the frame into PCM samples and reset the buffer position
	mad_synth_frame(&mp3_synth, &mp3_frame);
	mp3_pos_in_frame = 0;
}

static inline signed int scale_sample(mad_fixed_t sample) {
  /* round */
  sample += (1L << (MAD_F_FRACBITS - 16));

  /* clip */
  if (sample >= MAD_F_ONE)
    sample = MAD_F_ONE - 1;
  else if (sample < -MAD_F_ONE)
    sample = -MAD_F_ONE;

  /* quantize */
  return sample >> (MAD_F_FRACBITS + 1 - 16);
}

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

int mp3_read_buffer(char *buffer, const int size) {
	int samples = 0;

	while (samples < size) {
		const int len = MIN(size, samples + (int)(mp3_synth.pcm.length - mp3_pos_in_frame) * ((music_format == AL_FORMAT_STEREO16) ? 2 : 1));
		while (samples < len) {
			signed int sample;

			sample = scale_sample(mp3_synth.pcm.samples[0][mp3_pos_in_frame]);
			*buffer++ = (sample >> 0) & 0xff;
			*buffer++ = (sample >> 8) & 0xff;
			samples += 2;
			if (music_format == AL_FORMAT_STEREO16) {
				sample = scale_sample(mp3_synth.pcm.samples[1][mp3_pos_in_frame]);
				*buffer++ = (sample >> 0) & 0xff;
				*buffer++ = (sample >> 8) & 0xff;
				samples += 2;
			}
			mp3_pos_in_frame++;
		}
		if (mp3_pos_in_frame >= mp3_synth.pcm.length) {
			mp3_refill();
			if (mp3_size <= 0) break;
		}
	}
	return samples;
}

#endif

#ifndef WZ_CDA
BOOL cdAudio_OpenTrack(char* filename) {
	if (!music_initialized) {
		return FALSE;
	}

	if (music_file != NULL) {
#ifndef WZ_NOMP3
		if (music_file_format == WZ_MP3) {
			mad_synth_finish(&mp3_synth);
			mad_frame_finish(&mp3_frame);
			mad_stream_finish(&mp3_stream);
		}
#endif
		fclose(music_file);
	}

	music_file_format = WZ_NONE;

#ifndef WZ_NOMP3
	if (strncasecmp(filename+strlen(filename)-4, ".mp3", 4) == 0) {
		music_file = fopen(filename, "rb");

		if (music_file == NULL) {
			return FALSE;
		}

		mad_stream_init(&mp3_stream);
		mad_frame_init(&mp3_frame);
		mad_synth_init(&mp3_synth);

		mp3_buffer_length = fread(mp3_buffer, 1,
					  MP3_BUFFER_SIZE, music_file);

		mad_stream_buffer(&mp3_stream, mp3_buffer, mp3_buffer_length);

		mp3_refill();

		switch(mp3_frame.header.mode) {
			case MAD_MODE_SINGLE_CHANNEL:
			case MAD_MODE_DUAL_CHANNEL:
			case MAD_MODE_JOINT_STEREO:
			case MAD_MODE_STEREO: {
				if (MAD_NCHANNELS(&mp3_frame.header) == 1) {
					music_format = AL_FORMAT_MONO16;
				} else if (MAD_NCHANNELS(&mp3_frame.header) == 2) {
					music_format = AL_FORMAT_STEREO16;
				} else {
					return FALSE;
				}
			}	break;
			default:
				return FALSE;
		}

		music_file_format = WZ_MP3;
	}
#endif

#ifndef WZ_NOOGG
	if (strncasecmp(filename+strlen(filename)-4, ".ogg", 4) == 0) {
		music_file = fopen(filename, "rb");

		if (music_file == NULL) {
			return FALSE;
		}

		if (ov_open(music_file, &ogg_stream, NULL, 0) < 0) {
			fclose(music_file);
			music_file = NULL;
			return FALSE;
		}
	    
		ogg_info = ov_info(&ogg_stream, -1);
	 
		if (ogg_info->channels == 1) {
			music_format = AL_FORMAT_MONO16;
		} else {
			music_format = AL_FORMAT_STEREO16;
		}

		music_file_format = WZ_OGG;
	}
#endif

	return TRUE;
}

BOOL cdAudio_CloseTrack() {
	if (music_track != 0) {
		int queued, processed, all;

		alSourceStop(music_source);
		alGetSourcei(music_source, AL_BUFFERS_QUEUED, &queued);
		alGetSourcei(music_source, AL_BUFFERS_PROCESSED, &processed);
		all = queued + processed;

		while (all > 0) {
			ALuint buffer;

			alSourceUnqueueBuffers(music_source, 1, &buffer);
			all--;
		}

		fclose(music_file);
		music_file = NULL;
		music_track = 0;
	}

	return TRUE;
}

BOOL cdAudio_FillBuffer(ALuint b) {
	int  size = 0;
	int  section;
	int  result;

	while (size < BUFFER_SIZE) {

#ifndef WZ_NOMP3
		if (music_file_format == WZ_MP3) {
			result = mp3_read_buffer(music_data+size, BUFFER_SIZE-size);
			music_rate = 44100;
		}
#endif

#ifndef WZ_NOOGG
		if (music_file_format == WZ_OGG) {
			result = ov_read(&ogg_stream, music_data+size,
					 BUFFER_SIZE-size, 0, 2, 1, &section);
			music_rate = ogg_info->rate;
		}
#endif

		if (result > 0) {
			size += result;
		} else {
			char* filename;

			for (;;) {
				filename = PlayList_NextSong();

				if (filename == NULL) {
					music_track = 0;
					break;
				}
				if (cdAudio_OpenTrack(filename)) {
					printf("Now playing %s\n", filename);
					break;
				}
			}
		}
	}

	if (size == 0) {
		return FALSE;
	}

	alBufferData(b, music_format, music_data, size, music_rate);

	return TRUE;
}
#endif

//*
// ======================================================================
// ======================================================================
//
BOOL cdAudio_PlayTrack( SDWORD iTrack )
{
#ifdef WZ_CDA
	if ( cdAudio_dev != NULL )
	{
		SDL_CDPlayTracks( cdAudio_dev, iTrack - 1, 0, 1, 0 );
	}

	return TRUE;
#else
	unsigned int i;

	cdAudio_CloseTrack();

	PlayList_SetTrack(iTrack);

	{
		char* filename = PlayList_CurrentSong();

		for (;;) {
			if (filename == NULL) {
				music_track = 0;
				return FALSE;
			}
			if (cdAudio_OpenTrack(filename)) {
				music_track = iTrack;
				printf("Now playing %s\n", filename);
				break;
			}

			filename = PlayList_NextSong();
		}
	}

	for (i = 0; i < NB_BUFFERS; ++i) {
		if (!cdAudio_FillBuffer(music_buffers[i])) {
			return FALSE;
		}
	}

	alSourceQueueBuffers(music_source, NB_BUFFERS, music_buffers);
	alSourcePlay(music_source);

	return TRUE;
#endif
}

//*
// ======================================================================
// ======================================================================
//
BOOL cdAudio_Stop( void )
{
#ifdef WZ_CDA
	if ( cdAudio_dev != NULL )
	{
		SDL_CDStop( cdAudio_dev );
	}

	return TRUE;
#else
	cdAudio_CloseTrack();
	return TRUE;
#endif
}

//*
// ======================================================================
// ======================================================================
//
BOOL cdAudio_Pause( void )
{
#ifdef WZ_CDA
	if ( cdAudio_dev != NULL )
	{
		SDL_CDPause( cdAudio_dev );
	}

	return TRUE;
#else
	return TRUE;
#endif
}

//*
// ======================================================================
// ======================================================================
//
BOOL cdAudio_Resume( void )
{
#ifdef WZ_CDA
	if ( cdAudio_dev != NULL )
	{
		SDL_CDResume( cdAudio_dev );
	}

	return TRUE;
#else
	return TRUE;
#endif
}

//*
// ======================================================================
// ======================================================================
//
void cdAudio_SetVolume( SDWORD iVol )
{
#ifdef WZ_CDA
#else
	//alSourcef(music_source, AL_GAIN, 0.01*iVol);
#endif
}

//*
// ======================================================================
// ======================================================================
//
void cdAudio_Update( void )
{
#ifdef WZ_CDA
#else
	if (   music_track != 0
	    && music_volume != 0.0) {
		int processed = 0;

		alGetSourcei(music_source, AL_BUFFERS_PROCESSED, &processed);

		while (processed > 0) {
			ALuint buffer;

			alSourceUnqueueBuffers(music_source, 1, &buffer);
			cdAudio_FillBuffer(buffer);
			alSourceQueueBuffers(music_source, 1, &buffer);
			processed--;
		}

		{
			ALenum state;

			alGetSourcei(music_source, AL_SOURCE_STATE, &state);
			if (state != AL_PLAYING) {
				alSourcePlay(music_source);
			}
		}
	}
#endif
}

//*
// ======================================================================
// ======================================================================
//
SDWORD mixer_GetCDVolume( void )
{
	return 100*music_volume;
}

//*
// ======================================================================
// ======================================================================
//
void mixer_SetCDVolume( SDWORD iVol )
{
	music_volume = 0.01*iVol;

	if (music_volume < 0.0) {
		music_volume = 0.0;
	} else if (music_volume > 1.0) {
		music_volume = 1.0;
	}
	alSourcef (music_source, AL_GAIN, music_volume);
}


