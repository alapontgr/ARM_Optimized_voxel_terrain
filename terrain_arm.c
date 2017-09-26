#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <SDL/SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "chrono.h"

#define SCR_WIDTH     512
#define SCR_WIDTH_2   256
#define SCR_HEIGHT    512
#define SCR_HEIGHT_2  256
#define MAX_STEPS     1024
#define MAX_STEPS_2   512
#define STATIC_MODE   1


#define PIXEL16

#ifdef PIXEL16
#define PIXEL_BITS 16
typedef unsigned short Pixel;
#else
#define PIXEL_BITS 32
typedef unsigned int Pixel;
#endif


static int cpu_mhz = 0;


typedef struct {
  int width;
  int height;
  unsigned short* pixels;
  unsigned short* pixels_2;
} Heightmap;

typedef struct {
  int x, y, z;
} Point;

static inline int ToFx12(float v) {
  return (int)(v * (float)(1 << 12));
}

static inline int ToFx8(float v) {
  return (int) (v * (float)(1 << 8));
}

static inline int ToFx16(float v) {
  return (int) (v * (float)(1 << 16));
}

static inline int ToFx14(float v) {
  return (int) (v * (float)(1 << 14));
}

static inline int ToFx10(float v) {
  return (int) (v * (float)(1 << 10));
}

static inline int ToFx11(float v) {
  return (int) (v * (float)(1 << 11));
}

static inline int ToFx13(float v) {
  return (int) (v * (float)(1 << 13));
}

static inline int IntegerPartFx12(int fx12) {
  return fx12 >> 12;
}

static float Lerp(float src, float dst, float a) {
  return (1.0f - a) * src + a * dst;
}
// course's helper functions ---------------------------------------
static inline void ChronoShow ( char* name, int computations) {
  float ms = ChronoWatchReset();
  float cycles = ms * (1000000.0f/1000.0f) * (float)cpu_mhz;
  float cyc_per_comp = cycles / (float)computations;
  fprintf ( stdout, "%s: %f ms, %d cycles, %f cycles/pixel\n", name, ms, (int)cycles, cyc_per_comp);
}


static inline void LimitFramerate (int fps) {  
  static unsigned int frame_time = 0;
  unsigned int t = GetMsTime();
  unsigned int elapsed = t - frame_time;
  const unsigned int limit = 1000 / fps;
  if ( elapsed < limit)
    usleep (( limit - elapsed) * 1000);
  frame_time = GetMsTime();
}

static inline unsigned int BlendARGB (
  unsigned int pix0, 
  unsigned int pix1, 
  int alphafx8) {
  unsigned int ag0 = (pix0 & 0xFF00FF00) >> 8;
  unsigned int rb0 = pix0 & 0x00FF00FF;
  unsigned int ag1 = (pix1 & 0xFF00FF00) >> 8;
  unsigned int rb1 = pix1 & 0x00FF00FF;
  unsigned int ag  = ((ag0 * alphafx8 + ag1 * (0x100 - alphafx8)) >> 8) & 0xFF00FF;
  unsigned int rb  = ((rb0 * alphafx8 + rb1 * (0x100 - alphafx8)) >> 8) & 0xFF00FF;
  return ((ag << 8) | rb);
}
// -----------------------------------------------------------------------


static Heightmap LoadHeightmap(const char* path) {
  int x, y, n;
  unsigned char *data = stbi_load(path, &x, &y, &n, 4);
  assert(data);
  
  int size = x*y;
  unsigned short* pixels = (unsigned short*)malloc(size*2);
  
  int i;
  for(i = 0; i < size; ++i) {
    unsigned int* src = (unsigned int*)(&data[i*4]);
    short alpha = *src >> 24;
    short blue  = *src & 0xFF;
    short dst = (alpha << 8) | blue;
    memcpy(&pixels[i], &dst, 2);
  }
  
  unsigned short* pixels_2 = (unsigned short*) malloc(size);
  
  int yi;
  for(yi = 0; yi < y; ++yi) {
    int xi;
    unsigned short* dst_line = &pixels_2[(yi >> 1) * (x >> 1)];
    for(xi = 0; xi < x; ++xi) {
      unsigned short  src_up    = pixels[xi + (((yi-1) & (y-1)) * x)];
      unsigned short  src_down  = pixels[xi + (((yi+1) & (y-1)) * x)];
      unsigned short  src_left  = pixels[((xi-1) & (x-1)) + (yi * x)];
      unsigned short  src_right = pixels[((xi+1) & (x-1)) + (yi * x)];
      
      unsigned short color  = ((src_up & 0xFF) + 
                              (src_down & 0xFF) + 
                              (src_left & 0xFF) + 
                              (src_right & 0xFF)) >> 2;
      unsigned short height = ((src_up >> 8) + 
                              (src_down >> 8) + 
                              (src_left >> 8) + 
                              (src_right >> 8)) >> 2;

      unsigned short final  = (height << 8) | color;   
      dst_line[xi >> 1] = final;    
    }
  }

  Heightmap h;
  h.pixels = pixels;
  h.pixels_2 = pixels_2;
  h.width = x;
  h.height = y;

  stbi_image_free(data);
  return h;
}

static void FreeHeightmap(Heightmap* height_map) {
  assert(height_map && height_map->pixels);
  free(height_map->pixels);
  free(height_map->pixels_2);
  height_map->pixels = NULL;
  height_map->width = 0;
  height_map->height = 0;
}

typedef struct {
  Heightmap* tex;
  float fov;
  int c;
  int level_2;
  int* inv_z;
  Point* cam_pos;
  Point* cam_dir;
  unsigned int* world;
  unsigned int* world2;
} SampleTerrainParams;


typedef struct {
  unsigned int* world;
  unsigned int* world2;
  Pixel* scr;
  Pixel* palette;
  unsigned int* inv_diff_y;
  int level_2;
} DrawTerrainParams;

static inline int GetMin(int a, int b) {
  /*int c = a-b;
  int sign = ((c >> 31) &  0x1);
  return  (sign * a) + ((1 - sign) * b);*/
  return a < b ? a : b;
}

static inline int Clamp(int min, int max, int val) {
  return (val < min ? min : val > max ? max : val);
}

static inline int GetMax(int a, int b) {
  /*int c = a-b;
  int sign = ((c >> 31) &  0x1);
  return  (sign * b) + ((1 - sign) * a);*/
  return a > b ? a : b;
}


typedef struct {
  Pixel palette[256];
  Pixel* line;
  unsigned int* w_line;
  unsigned int* inv_diff_y;
  int* last_color;
  int* last_y;
  int level_2;
  int h;
} InnerParams;

static InnerParams inner_params;

extern void DrawL0(InnerParams* params) ;

void DrawTerrain(DrawTerrainParams* params) {
  Pixel* scr = params->scr;
  int level_2 = params->level_2;
  Pixel* palette = inner_params.palette;
  unsigned int* inv_diff_y = params->inv_diff_y;
  int x;
  for(x = 0; x < SCR_WIDTH; ++x) {  
    int first = params->world[x * MAX_STEPS];
    int last_y = 0;
    int last_color = (first & 0xFF)<<12;
    int y; 
    //LOD 0
    unsigned int* world_line = &params->world[x * MAX_STEPS];
    Pixel* line = &scr[x * SCR_WIDTH];
   
    inner_params.level_2 = level_2;
    inner_params.h = SCR_HEIGHT;
    inner_params.line = line;
    inner_params.inv_diff_y = inv_diff_y;
    inner_params.w_line = world_line;
    inner_params.last_color = &last_color;
    inner_params.last_y = &last_y;
    
    DrawL0(&inner_params);  

    //LOD 1
    world_line = &params->world2[(x>>1) * MAX_STEPS_2];
    for(y = level_2; y < MAX_STEPS && last_y < SCR_HEIGHT; y+=2) {
      unsigned int texel = world_line[((y>>1) & (MAX_STEPS_2-1))];
      int current_y = texel >> 8;
      int current_color = (texel & 0xFF)<<12;  
      if(current_y > last_y) {
        unsigned int diff_y = inv_diff_y[current_y - last_y];
        unsigned int color = last_color;
        unsigned int delta_color = ((current_color - last_color)>>12) * diff_y;   
        current_y = GetMin(current_y, SCR_HEIGHT - 1);
        int y0;
        for(y0 = current_y-1; y0>=last_y; y0--) {
          unsigned int f = color>>12;
          //unsigned int f_2 = f >> 1;
          line[y0] = palette[f];
          //line[y0] = (255 << 24) | (f << 16) | (f_2 << 8) | (current_y >> 2);
          color += delta_color;     
        }
        last_y = current_y;
        last_color = current_color;
      }
    }
  }
}

static void SampleTerrain(SampleTerrainParams* params)
{
  float fov_h = params->fov * 0.5f;
  float left_f = sinf(-fov_h) / cosf(-fov_h);
  float right_f = sinf(fov_h) / cosf(fov_h);
  Point cam_pos = *(params->cam_pos);
  int w = params->tex->width;
  int h = params->tex->height;
  int w_mask = w-1;
  int h_mask = h-1;
  int w_2 = w>>1;
  int h_2 = h>>1;
  int w2_mask = w_2-1;
  int h2_mask = h_2-1;
  unsigned short* pixels = params->tex->pixels;
  unsigned short* pixels_2 = params->tex->pixels_2;
  int left_x = ToFx11(left_f);
  int right_x = ToFx11(right_f);

  int level_2 = params->level_2;
  int delta = ((right_x - left_x)>>11) * ToFx11(1.0f / (float)SCR_WIDTH);
  //int xf = 0;
  int x;
  int lerped_x = left_x;
  for(x=0; x<SCR_WIDTH; x++) {
    lerped_x += delta;
    //xf += delta;
    Point pos = { cam_pos.x, cam_pos.y, cam_pos.z };
    unsigned int depth;
    int z = cam_pos.z >> 11;
    int cam_y = cam_pos.y >> 11;
    unsigned int* line = &params->world[x * MAX_STEPS];
    //Mipmap level 0
    for(depth = 0; depth<level_2; depth++) {
      int world_x = (pos.x >> 11) & w_mask;
      int world_z = (z + depth) & h_mask;
      int idx = (world_z + world_x * w);
      
      unsigned short texel = pixels[idx];
      int inv_z = params->inv_z[depth];
      int size_y = texel >> 8;
      int yc = ((size_y - cam_y) * inv_z) >> 12;
      int proj_y = Clamp(0, SCR_HEIGHT, yc + SCR_HEIGHT_2);    
      line[depth]= (proj_y << 8) | (texel & 0xFF);
      pos.x += lerped_x;
    }
     
    //Mipmap level 1
    line = &params->world2[(x>>1) * MAX_STEPS_2];
    for(depth = level_2; depth<MAX_STEPS; depth+=2) {
      int world_x = (pos.x >> 12) & w2_mask;
      int world_z = ((z + depth)>>1) & h2_mask;
      int idx = (world_z + (world_x * w_2));
      
      unsigned short texel = pixels_2[idx];
      int inv_z = params->inv_z[depth];
      int size_y = texel >> 8;
      int yc = ((size_y - cam_y) * inv_z) >> 12;
      int proj_y = Clamp(0, SCR_HEIGHT, yc + SCR_HEIGHT_2);  
      line[(depth>>1) & (MAX_STEPS_2-1)] = (proj_y << 8) | (texel & 0xFF);
      pos.x += lerped_x;
    }
  }
}


static void RotateScreen(Pixel* world_final, Pixel* screen) {
  int y;
  int num_tiles = (SCR_HEIGHT>>3);
  int tile_size = 1<<3;
  for(y = 0; y < num_tiles; ++y) {
    int x;
    for(x = 0; x < num_tiles; ++x) {
      int tile_y;
      int y0 = y<<3;      
      int y_offset = y0 * SCR_WIDTH;
      for(tile_y = 0; tile_y<tile_size; tile_y++) {
        int tile_x;
        int x0 = x<<3;
        for(tile_x = 0; tile_x<tile_size; tile_x++) {
          screen[y0 + ((SCR_HEIGHT - x0 -1) * SCR_WIDTH)] = world_final[y_offset + x0];
          x0++;
        }
        y0++;
      }
    }
  }
}


int main(int argc, char** argv) {
  if ( argc < 2) { fprintf ( stderr, "I need the cpu speed in Mhz!\n"); exit(0);}
  cpu_mhz = atoi( argv[1]);
  assert(cpu_mhz > 0);
  fprintf ( stdout, "Cycle times for a %d Mhz cpu\n", cpu_mhz);

  SDL_Surface  *g_SDLSrf;

  int req_w = SCR_WIDTH;
  int req_h = SCR_HEIGHT;

  // Init SDL and screen
  if ( SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO) < 0 ) 
  {
    fprintf(stderr, "Can't Initialise SDL: %s\n", SDL_GetError());
    exit(1);
  }
  if (0 == SDL_SetVideoMode( req_w, req_h, PIXEL_BITS,  SDL_HWSURFACE | SDL_DOUBLEBUF))
  {
    printf("Couldn't set %dx%dx32 video mode: %s\n", req_w, req_h, SDL_GetError());
    return 0;
  }

  g_SDLSrf = SDL_GetVideoSurface();

  float hfov = 60.0f * ((3.1416f) / 180.0f); 
  float half_scr_w = (float)(g_SDLSrf->w >> 1);
  float projection = ((1.0f / tan ( hfov * 0.5f)) * half_scr_w);
  int end = 0;
  float time_passed = 0.0f;

  Heightmap hmap = LoadHeightmap("heightmaps/map_1.png");
  
  Point cam_pos = {0, 0, 0};
  Point cam_dir = {0, 0, 1 << 11};

  unsigned int* world = (unsigned int*)malloc(SCR_WIDTH * MAX_STEPS * 4);
  unsigned int* world2 = (unsigned int*)malloc(SCR_WIDTH_2 * MAX_STEPS_2 * 4);
  Pixel* world_final = (Pixel*)malloc(SCR_WIDTH * MAX_STEPS * sizeof(Pixel));
  memset(world, 0, SCR_WIDTH * MAX_STEPS * 4);
  memset(world2, 0, SCR_WIDTH_2 * MAX_STEPS_2 * 4);
  memset(world_final, 0, SCR_WIDTH * MAX_STEPS * sizeof(Pixel));

  int i;
  Pixel* palette = (Pixel*)malloc(256*sizeof(Pixel));
  for(i=0; i<256; i++) {
    unsigned int r = i >> 3;
    unsigned int g = i >> 2;
    unsigned int b = i >> 3;
    palette[i] = (r<<11) | (g <<5) | (b);
  }
  memcpy(inner_params.palette, palette, 256*sizeof(Pixel));
  free(palette);

  unsigned int* inv_diff_y = (unsigned int*)malloc(144<<2);
  for(i=1; i<144; i++) {
    inv_diff_y[i] = (1<<12) / i;
  }

  int* inv_z = (int*)malloc(MAX_STEPS*4);
  for(i = 0; i < MAX_STEPS; ++i) {
    inv_z[i] = ToFx12(projection/(float)(i+1));
    //printf("%d ------ %f\n", inv_z[i], projection/(float)(i+1));
  }
  //exit(0);

  //LOD
  float fov_h = hfov * 0.5f;
  float left_f = sinf(-fov_h) / cosf(-fov_h);
  float right_f = sinf(fov_h) / cosf(fov_h);
  float s = left_f;
  float s_2 = Lerp(left_f, right_f, 1.0f / (float)SCR_WIDTH);
  int level_2 = Clamp(0, MAX_STEPS, (int) abs(1.0f / (s - s_2)));
    
  while ( !end) {
    SDL_Event event;

    // Lock screen to get access to the memory array
    SDL_LockSurface( g_SDLSrf);

    Pixel* screen_pixels = (Pixel*) g_SDLSrf->pixels;

    cam_pos.x = 0;
    cam_pos.y = 130 << 11;
#if !STATIC_MODE
    cam_pos.z = ToFx11(time_passed*256.0f);
#endif    

    int xi = cam_pos.x >> 11;
    int zi = cam_pos.z >> 11;      
    int world_x = (xi) & (hmap.width-1);
    int world_z = (zi) & (hmap.height-1);
    int idx = (world_z + world_x * hmap.width);
    unsigned short texel = hmap.pixels[idx];
    int size_y = texel >> 8;
    cam_pos.y = ToFx11(Lerp(cam_pos.y >> 11, size_y + 125.0f, 0.5f));

    SampleTerrainParams params;
    params.tex = &hmap;
    params.fov = hfov;
    params.c = ToFx8(projection);
    params.cam_pos = &cam_pos;
    params.cam_dir = &cam_dir;
    params.inv_z = inv_z;
    params.world = world;
    params.world2 = world2;
    params.level_2 = level_2;
    

    DrawTerrainParams draw_params;
    draw_params.scr = world_final;
    draw_params.world = world;
    draw_params.world2 = world2;
    draw_params.level_2 = level_2;
    draw_params.palette = palette;
    draw_params.inv_diff_y = inv_diff_y;

    // Clear screen
    memset(screen_pixels, 0, (SCR_WIDTH*SCR_HEIGHT)*2);
    memset(world_final,   0, (SCR_WIDTH*SCR_HEIGHT)*4);
  
    ChronoWatchReset();
    SampleTerrain(&params);
    ChronoShow ("Sample terrain", SCR_WIDTH * MAX_STEPS);

    ChronoWatchReset();
    DrawTerrain(&draw_params);
    ChronoShow ("Draw terrain", SCR_WIDTH * MAX_STEPS);

    ChronoWatchReset();
    RotateScreen(world_final, screen_pixels);
    ChronoShow("Rotate Screen", SCR_WIDTH * SCR_HEIGHT);

    LimitFramerate(60);
    time_passed += 0.016f;
    SDL_UnlockSurface( g_SDLSrf);
    SDL_Flip( g_SDLSrf);
    // Check input events
    while ( SDL_PollEvent(&event) ) {
      switch (event.type) 
      {
        case SDL_MOUSEMOTION:
          break;
        case SDL_MOUSEBUTTONDOWN:
          //printf("Mouse button %d pressed at (%d,%d)\n",
          //       event.button.button, event.button.x, event.button.y);
          break;
        case SDL_QUIT:
          end = 1;
          break;
      }
    }
  } 

  free(inv_z);
  free(inv_diff_y);
  free(world);
  free(world2);
  free(world_final);
  FreeHeightmap(&hmap);
	return 0;
}
