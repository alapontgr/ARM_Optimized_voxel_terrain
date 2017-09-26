
gcc -c -g -Wall -O2 -o buildobjs/terrain_rasp.o terrain_rasp.c 
gcc -c -g -O3 -o buildobjs/chrono.o chrono.c 

cd buildobjs
gcc -o ../terrain_rasp.elf terrain_rasp.o chrono.o -lSDL -lm 
