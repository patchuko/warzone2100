#include <stdio.h>

#include "frame.h"

#define BUFFER_SIZE 2048
static char buffer[BUFFER_SIZE];

#define NB_TRACKS 3

typedef struct {
	char**		songs;
	unsigned int	nb_songs;
	unsigned int	list_size;
} WZ_TRACK;

static unsigned int current_track = 0;
static unsigned int current_song = 0;

static WZ_TRACK playlist[NB_TRACKS];

#define CURRENT_TRACK playlist[current_track]

void PlayList_Init() {
	unsigned int i;

	for (i = 0; i < NB_TRACKS; ++i) {
		playlist[i].songs = malloc(2*sizeof(char*));
		playlist[i].list_size = 2;
		playlist[i].nb_songs = 0;
	}
}

char PlayList_Read(const char* path) {
	FILE* f;
	unsigned int nb_songs = 0;
	char* path_to_music = NULL;

	sprintf(buffer, "%s/music.wpl", path);

	f = fopen(buffer, "r");

	if (f == NULL) {
		return 1;
	}

	while (!feof(f)) {
		unsigned int new_track;
		char* filename;

		fgets(buffer, BUFFER_SIZE, f);

		if (sscanf(buffer, "[track%i]", &new_track) == 1) {
			if (new_track >= 0 && new_track < NB_TRACKS) {
				printf("Track %i\n", new_track);
				current_track = new_track;
				free(path_to_music);
				path_to_music = NULL;
			}
		} else if (strncmp(buffer, "path=", 5) == 0) {
			free(path_to_music);
			path_to_music = strtok(buffer+5, "\n");
			if (strcmp(path_to_music, ".") == 0) {
				path_to_music = strdup(path);
			} else {
				path_to_music = strdup(path_to_music);
			}
			printf("  path = %s\n", path_to_music);
		} else if (strncmp(buffer, "shuffle=", 8) == 0) {
			if (strcmp(strtok(buffer+8, "\n"), "yes") == 0) {
			}
			printf("  shuffle = yes\n");
		} else if (   buffer[0] != '\0'
			   && (filename = strtok(buffer, "\n")) != NULL
			   && strlen(filename) != 0) {
			char* filepath;

			if (path_to_music == NULL) {
				filepath = malloc(strlen(filename)+1);
				sprintf(filepath, "%s", filename);
			} else {
				filepath = malloc(  strlen(filename)
						  + strlen(path_to_music)+2);
				sprintf(filepath, "%s/%s", path_to_music, filename);
			}
			printf("  adding song %s\n", filepath);

			if (CURRENT_TRACK.nb_songs == CURRENT_TRACK.list_size) {
				CURRENT_TRACK.list_size <<= 1;
				CURRENT_TRACK.songs = realloc(CURRENT_TRACK.songs,
							      CURRENT_TRACK.list_size*sizeof(char*));
			}

			CURRENT_TRACK.songs[CURRENT_TRACK.nb_songs++] = filepath;
		}
	}

	free(path_to_music);

	return 0;
}

void PlayList_SetTrack(unsigned int t) {
	if (t >= 0 && t < NB_TRACKS) {
		current_track = t;
	} else {
		current_track = 0;
	}
	current_song = 0;
}

char* PlayList_CurrentSong() {
	if (current_song >= CURRENT_TRACK.nb_songs) {
		return NULL;
	} else {
		return CURRENT_TRACK.songs[current_song];
	}
}

char* PlayList_NextSong() {
	if (++current_song >= CURRENT_TRACK.nb_songs) {
		current_song = 0;
	}

	if (CURRENT_TRACK.nb_songs == 0) {
		return NULL;
	} else {
		return CURRENT_TRACK.songs[current_song];
	}
}

void PlayList_DeleteCurrentSong() {
}

