
gcc -c -g -Wall -O2 -o buildobjs/terrain.o terrain.c
gcc -c -g -O3 -o buildobjs/chrono.o chrono.c

cd buildobjs
gcc -o ../terrain.elf terrain.o chrono.o -lSDL -lm
