CC=gcc
LIBS=-lpulse -lmpdclient

mpauserd: simple_mpd_checker.c
	$(CC) -o simple_mpd_checker simple_mpd_checker.c $(LIBS) 
