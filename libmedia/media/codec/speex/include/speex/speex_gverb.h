

#ifndef SPEEX_GVERB_H
#define SPEEX_GVERB_H

#include "../../config.h"

#include "arch.h"

#ifdef _MSC_VER
#define isnan(x) ((x) != (x)) 
#endif

#ifdef __cplusplus
extern "C" {
#endif
#define FDNORDER 4

    /* 32 bit "pointer cast" union */
typedef union {float f1;
     int i;    
} speex_ls_pcast32;

typedef struct {
  int size;
  int idx;
  int *buf;
} speex_ty_fixeddelay;

typedef struct {
  int size;
  int coeff;
  int idx;
  spx_word32_t *buf;
} speex_ty_diffuser;

typedef struct {
  int damping;
  int delay;
} speex_ty_damper;

typedef struct {
  int rate;
  float inputbandwidth;
  short taillevel;
  short earlylevel;
  speex_ty_damper *inputdamper;
  float maxroomsize;
  float roomsize;
  float revtime;
  float maxdelay;
  float largestdelay;
  speex_ty_fixeddelay **fdndels;
  int *fdngains;
  int *fdnlens;
  speex_ty_damper **fdndamps; 
  float fdndamping;
  speex_ty_diffuser **ldifs;
  speex_ty_diffuser **rdifs;
  speex_ty_fixeddelay *tapdelay;
  short *taps;
  short *tapgains;
  int *d;
  int *u;
  int *f1;
  double alpha;
} speex_ty_gverb;


speex_ty_gverb *speex_gverb_new(int, float, float, float, float, float, float, float, float);
void speex_gverb_free(speex_ty_gverb *);
void speex_gverb_flush(speex_ty_gverb *);
static void speex_gverb_do(speex_ty_gverb *, short, short *, short *);
static void speex_gverb_set_roomsize(speex_ty_gverb *, float);
static void speex_gverb_set_revtime(speex_ty_gverb *, float);
static void speex_gverb_set_damping(speex_ty_gverb *, float);
static void speex_gverb_set_inputbandwidth(speex_ty_gverb *, float);
static void speex_gverb_set_earlylevel(speex_ty_gverb *, float);
static void speex_gverb_set_taillevel(speex_ty_gverb *, float);

#ifdef __cplusplus
}
#endif



// Round float to int using IEEE int* hack
static __inline int speex_f_round(float f1)
{
	speex_ls_pcast32 p;

	p.f1 = f1;
	p.f1 += (3<<22);

	return p.i - 0x4b400000;
}

static __inline spx_word32_t speex_diffuser_do(speex_ty_diffuser *p, spx_word32_t x)
{
  spx_word32_t y,w;

  w = x - p->buf[p->idx]*p->coeff;
  y = p->buf[p->idx] + w*p->coeff;
  p->buf[p->idx] = w;
  p->idx = (p->idx + 1) % p->size;
  return(y);
}

static __inline spx_word32_t speex_fixeddelay_read(speex_ty_fixeddelay *p, spx_word32_t n)
{
  int i;

  i = (p->idx - n + p->size) % p->size;
  return(p->buf[i]);
}

static __inline void speex_fixeddelay_write(speex_ty_fixeddelay *p, spx_word32_t x)
{
  p->buf[p->idx] = x;
  p->idx = (p->idx + 1) % p->size;
}

static __inline void speex_damper_set(speex_ty_damper *p, float damping)
{ 
  p->damping = QCONST16(damping, 15);
} 
  
static __inline spx_word32_t speex_damper_do(speex_ty_damper *p, spx_word32_t x)
{ 
  spx_word32_t y;
    
  y = MULT16_16(x, (QCONST32(1, 15) -p->damping)) + MULT16_16(p->delay, p->damping);
  p->delay = y;
  return(y);
}

static __inline void speex_gverb_fdnmatrix(int *a, int *b)
{
  const int dl0 = a[0], dl1 = a[1], dl2 = a[2], dl3 = a[3];

 
  b[0] = SHR((+dl0 + dl1 - dl2 - dl3), 1);
  b[1] = SHR((+dl0 - dl1 - dl2 + dl3), 1);
  b[2] = SHR((-dl0 + dl1 - dl2 + dl3), 1);
  b[3] = SHR((+dl0 + dl1 + dl2 + dl3), 1);
}

static __inline void speex_gverb_do(speex_ty_gverb *p, short x, short *yl, short *yr)
{
  int z;
  unsigned int i;
  int lsum,rsum,sum,sign;

  z = speex_damper_do(p->inputdamper, x);

  z = speex_diffuser_do(p->ldifs[0],z);

  for(i = 0; i < FDNORDER; i++) {
    p->u[i] = p->tapgains[i]*speex_fixeddelay_read(p->tapdelay,p->taps[i]);
  }

  speex_fixeddelay_write(p->tapdelay,z);

  for(i = 0; i < FDNORDER; i++) {
    p->d[i] = speex_damper_do(p->fdndamps[i],
			p->fdngains[i]*speex_fixeddelay_read(p->fdndels[i],  p->fdnlens[i]));
  }

  sum = 0;
  sign = 1;
  for(i = 0; i < FDNORDER; i++) {
    sum += sign*(p->taillevel*p->d[i] + p->earlylevel*p->u[i]);
    sign = -sign;
  }
  sum += x*p->earlylevel;
  lsum = sum;
 

  speex_gverb_fdnmatrix(p->d,p->f1);

  for(i = 0; i < FDNORDER; i++) {
    speex_fixeddelay_write(p->fdndels[i],p->u[i]+p->f1[i]);
  }

  lsum = speex_diffuser_do(p->ldifs[1],lsum);
  lsum = speex_diffuser_do(p->ldifs[2],lsum);
  lsum = speex_diffuser_do(p->ldifs[3],lsum);

  *yl = SHR(lsum, 15);

  if(yr)
  { 
      rsum = sum;
      rsum = speex_diffuser_do(p->rdifs[1],rsum);
      rsum = speex_diffuser_do(p->rdifs[2],rsum);
      rsum = speex_diffuser_do(p->rdifs[3],rsum);
      *yr = SHR(rsum, 15);
  }
}

static __inline void speex_gverb_set_roomsize(speex_ty_gverb *p, float a)
{
  unsigned int i;
  if (a <= 1.0 || isnan(a)) {
    p->roomsize = 1.0;
  } else {
    p->roomsize = a;
  }
  p->largestdelay = p->rate * p->roomsize * 0.00294f;

  p->fdnlens[0] = speex_f_round(1.000000f*p->largestdelay);
  p->fdnlens[1] = speex_f_round(0.816490f*p->largestdelay);
  p->fdnlens[2] = speex_f_round(0.707100f*p->largestdelay);
  p->fdnlens[3] = speex_f_round(0.632450f*p->largestdelay);
  for(i = 0; i < FDNORDER; i++) {
    p->fdngains[i] = QCONST16(-powf((float)p->alpha, p->fdnlens[i]), 15);
  }

  p->taps[0] =  QCONST16((5+speex_f_round(0.410f*p->largestdelay)),15);
  p->taps[1] =  QCONST16((5+speex_f_round(0.300f*p->largestdelay)),15);
  p->taps[2] =  QCONST16((5+speex_f_round(0.155f*p->largestdelay)),15);
  p->taps[3] =  QCONST16((5+speex_f_round(0.000f*p->largestdelay)),15);

  for(i = 0; i < FDNORDER; i++) {
    p->tapgains[i] = QCONST16(powf((float)p->alpha, p->taps[i]), 15);
  }

}

static __inline void speex_gverb_set_revtime(speex_ty_gverb *p,float a)
{
  float ga,gt;
  double n;
  unsigned int i;

  p->revtime = a;

  ga = 60.0;
  gt = p->revtime;
  ga = powf(10.0f,-ga/20.0f);
  n = p->rate*gt;
  p->alpha = (double)powf(ga,1.0f/n);

  for(i = 0; i < FDNORDER; i++) {
    p->fdngains[i] = QCONST16(-powf((float)p->alpha, p->fdnlens[i]), 15);
  }

}

static __inline void speex_gverb_set_damping(speex_ty_gverb *p,float a)
{
  unsigned int i;

  p->fdndamping = a;
  for(i = 0; i < FDNORDER; i++) {
    speex_damper_set(p->fdndamps[i],p->fdndamping);
  }
}

static __inline void speex_gverb_set_inputbandwidth(speex_ty_gverb *p,float a)
{
  p->inputbandwidth = a;
  speex_damper_set(p->inputdamper,(float)(1.0 - p->inputbandwidth));
}

static __inline void speex_gverb_set_earlylevel(speex_ty_gverb *p,float a)
{
  p->earlylevel = QCONST16(a, 15);
}

static __inline void speex_gverb_set_taillevel(speex_ty_gverb *p,float a)
{
  p->taillevel = QCONST16(a, 15);
}

/** @}*/
#endif


