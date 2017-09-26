
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

// Timer de alta precision, standar POSIX. Devuelve microsegundos en 
// formato "long long" (entero 64 bits). ATENCION: Es un tipo de GCC, no es del standard C

static inline unsigned long long GetHighPrecisionTime()
{
  struct timeval t;
  gettimeofday(&t, NULL);
  // El sufijo LL indica que es un numero "long long"
  return 1000000LL * t.tv_sec + t.tv_usec;
}

// ---------------------------------------------------------------------------
// Consulta en milisegundos, reseteado a 0 en 1er uso

unsigned int GetMsTime()
{
  unsigned int ms = 0;
  unsigned long long t;
  static int init = 0;
  static unsigned long long first = 0;
  if ( init == 0) {
    first = GetHighPrecisionTime();
    init = 1;
  }

  t = GetHighPrecisionTime();
  ms = ((t - first) / 1000);  // microseconds to miliseconds
  return ms;
}

// ---------------------------------------------------------------------------
// Cronometro para medir costes del programa

static unsigned long long g_last_time = 0;

static inline float NanoToMili ( unsigned long long time)
{
  // Dividimos por 1000 para pasar de nanosegundos a milisegundos
  double res = (1.0/1000.0) * (double)time;
  return (float) res;
}

// Watch and reset
float ChronoWatchReset()
{
    unsigned long long now = GetHighPrecisionTime ();
    float elapsed = NanoToMili (now - g_last_time);
    g_last_time = now;
    return elapsed;
}

