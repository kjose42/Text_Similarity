project2: project2.c
	gcc -g -std=c99 -Wvla -Wall -fsanitize=address,undefined project2.c -o project2 -lm

clean:
	rm -f *.o project2