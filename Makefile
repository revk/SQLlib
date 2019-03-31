sqllib.o: sqllib.c
	cc -O -c -o $@ $< -D_GNU_SOURCE -DLIB
