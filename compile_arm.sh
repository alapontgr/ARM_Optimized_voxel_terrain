
gcc -c -g -Wall -O2 -o buildobjs/terrain_arm.o terrain_arm.c 
gcc -c -g -Wall -O2 -o buildobjs/terrain_s.o terrain_arm.s 
gcc -c -g -O3 -o buildobjs/chrono.o chrono.c 

cd buildobjs
gcc -o ../terrain_arm.elf terrain_arm.o terrain_s.o chrono.o -lSDL -lm 
