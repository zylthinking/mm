#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <math.h>
#include "../include/speex/speex_gverb.h"
#include "../include/speex/speex_echo.h"
#include "arch.h"
#include "fftwrap.h"
#include "filterbank.h"
#include "math_approx.h"
#include "os_support.h"

//gvervdsp
speex_ty_diffuser *speex_diffuser_make(spx_word32_t size, float coeff)
{
  speex_ty_diffuser *p;

  p = (speex_ty_diffuser *)malloc(sizeof(speex_ty_diffuser));
  p->size = size;
  p->coeff = QCONST16(coeff, 15);
  p->idx = 0;
  p->buf = (int *)malloc(size*sizeof(spx_word32_t));
  memset(p->buf, 0, p->size * sizeof(spx_word32_t));

  return(p);
}

void speex_diffuser_free(speex_ty_diffuser *p)
{
  free(p->buf);
  free(p);
}

void speex_diffuser_flush(speex_ty_diffuser *p)
{
  memset(p->buf, 0, p->size * sizeof(spx_word32_t));
}

speex_ty_damper *speex_damper_make(int damping)
{
  speex_ty_damper *p;

  p = (speex_ty_damper *)malloc(sizeof(speex_ty_damper));
  p->damping = damping;
  p->delay = 0;
  return(p);
}

void speex_damper_free(speex_ty_damper *p)
{
  free(p);
}

void speex_damper_flush(speex_ty_damper *p)
{
  p->delay = 0;
}

speex_ty_fixeddelay *fixeddelay_make(spx_word32_t size)
{
  speex_ty_fixeddelay *p;

  p = (speex_ty_fixeddelay *)malloc(sizeof(speex_ty_fixeddelay));
  p->size = size;
  p->idx = 0;
  p->buf = (int *)malloc(size*sizeof(spx_word32_t));
  memset(p->buf, 0 , size*sizeof(spx_word32_t));
  return(p);
}

void speex_fixeddelay_free(speex_ty_fixeddelay *p)
{
  free(p->buf);
  free(p);
}

void speex_fixeddelay_flush(speex_ty_fixeddelay *p)
{
  memset(p->buf, 0, p->size * sizeof(spx_word32_t));
}


//gverb

speex_ty_gverb *speex_gverb_new(int srate, 
                    float maxroomsize, 
                    float roomsize,
                    float revtime,
                    float damping, 
                    float spread,
                    float inputbandwidth, 
                    float earlylevel,
                    float taillevel)
{
  speex_ty_gverb *p;
  float ga,gb,gt;
  int i,n;
  float r;
  float diffscale;
  int a,b,c,cc,d,dd,e;
  float spread1,spread2;

  p = (speex_ty_gverb *)malloc(sizeof(speex_ty_gverb));
  p->rate = srate;
  p->fdndamping = damping;
  p->maxroomsize = maxroomsize;
  p->roomsize = roomsize;
  p->revtime = revtime;
  p->earlylevel = QCONST16(earlylevel, 15);
  p->taillevel = QCONST16(taillevel, 15);

  p->maxdelay = p->rate*p->maxroomsize/340.0;
  p->largestdelay = p->rate*p->roomsize/340.0;

  /* Input damper */

  p->inputbandwidth = inputbandwidth;
  p->inputdamper = speex_damper_make(1.0 - p->inputbandwidth);


  /* FDN section */


  p->fdndels = (speex_ty_fixeddelay **)calloc(FDNORDER, sizeof(speex_ty_fixeddelay *));
  for(i = 0; i < FDNORDER; i++) {
    p->fdndels[i] = fixeddelay_make((int)p->maxdelay+1000);
  }
  p->fdngains = (int *)calloc(FDNORDER, sizeof(int));
  p->fdnlens = (int *)calloc(FDNORDER, sizeof(int));

  p->fdndamps = (speex_ty_damper **)calloc(FDNORDER, sizeof(speex_ty_damper *));
  for(i = 0; i < FDNORDER; i++) {
    p->fdndamps[i] = speex_damper_make(p->fdndamping);
  }

  ga = 60.0;
  gt = p->revtime;
  ga = powf(10.0f,-ga/20.0f);
  n = p->rate*gt;
  p->alpha = pow((double)ga, 1.0/(double)n);

  gb = 0.0;
  for(i = 0; i < FDNORDER; i++) {
    if (i == 0) gb = 1.000000*p->largestdelay;
    if (i == 1) gb = 0.816490*p->largestdelay;
    if (i == 2) gb = 0.707100*p->largestdelay;
    if (i == 3) gb = 0.632450*p->largestdelay;

#if 0
    p->fdnlens[i] = nearest_prime((int)gb, 0.5);
#else
    p->fdnlens[i] = speex_f_round(gb);
#endif
    p->fdngains[i] = -powf((float)p->alpha,p->fdnlens[i]);
  }

  p->d = (int *)calloc(FDNORDER, sizeof(int));
  p->u = (int *)calloc(FDNORDER, sizeof(int));
  p->f1= (int *)calloc(FDNORDER, sizeof(int));

  /* Diffuser section */

  diffscale = (float)p->fdnlens[3]/(210+159+562+410);
  spread1 = spread;
  spread2 = 3.0*spread;

  b = 210;
  r = 0.125541;
  a = spread1*r;
  c = 210+159+a;
  cc = c-b;
  r = 0.854046;
  a = spread2*r;
  d = 210+159+562+a;
  dd = d-c;
  e = 1341-d;

  p->ldifs = (speex_ty_diffuser **)calloc(4, sizeof(speex_ty_diffuser *));
  p->ldifs[0] = speex_diffuser_make((int)(diffscale*b),0.75);
  p->ldifs[1] = speex_diffuser_make((int)(diffscale*cc),0.75);
  p->ldifs[2] = speex_diffuser_make((int)(diffscale*dd),0.625);
  p->ldifs[3] = speex_diffuser_make((int)(diffscale*e),0.625);

  b = 210;
  r = -0.568366;
  a = spread1*r;
  c = 210+159+a;
  cc = c-b;
  r = -0.126815;
  a = spread2*r;
  d = 210+159+562+a;
  dd = d-c;
  e = 1341-d;

  p->rdifs = (speex_ty_diffuser **)calloc(4, sizeof(speex_ty_diffuser *));
  p->rdifs[0] = speex_diffuser_make((int)(diffscale*b),0.75);
  p->rdifs[1] = speex_diffuser_make((int)(diffscale*cc),0.75);
  p->rdifs[2] = speex_diffuser_make((int)(diffscale*dd),0.625);
  p->rdifs[3] = speex_diffuser_make((int)(diffscale*e),0.625);



  /* Tapped delay section */

  p->tapdelay = fixeddelay_make(44000);
  p->taps = (short *)calloc(FDNORDER, sizeof(short));
  p->tapgains = (short *)calloc(FDNORDER, sizeof(short));

  p->taps[0] =  QCONST16((5+speex_f_round(0.410f*p->largestdelay)), 15);
  p->taps[1] =  QCONST16((5+speex_f_round(0.300f*p->largestdelay)), 15);
  p->taps[2] =  QCONST16((5+speex_f_round(0.155f*p->largestdelay)), 15);
  p->taps[3] =  QCONST16((5+speex_f_round(0.000f*p->largestdelay)), 15);

  for(i = 0; i < FDNORDER; i++) {
    p->tapgains[i] = QCONST16(pow(p->alpha,(double)p->taps[i]), 15);
  }

  return(p);
}

void speex_gverb_free(speex_ty_gverb *p)
{
  int i;

  speex_damper_free(p->inputdamper);
  for(i = 0; i < FDNORDER; i++) {
    speex_fixeddelay_free(p->fdndels[i]);
    speex_damper_free(p->fdndamps[i]);
    speex_diffuser_free(p->ldifs[i]);
    speex_diffuser_free(p->rdifs[i]);
  }
  free(p->fdndels);
  free(p->fdngains);
  free(p->fdnlens);
  free(p->fdndamps);
  free(p->d);
  free(p->u);
  free(p->f1);
  free(p->ldifs);
  free(p->rdifs);
  free(p->taps);
  free(p->tapgains);
  speex_fixeddelay_free(p->tapdelay);
  free(p);
}

void speex_gverb_flush(speex_ty_gverb *p)
{
  int i;

  speex_damper_flush(p->inputdamper);
  for(i = 0; i < FDNORDER; i++) {
    speex_fixeddelay_flush(p->fdndels[i]);
    speex_damper_flush(p->fdndamps[i]);
    speex_diffuser_flush(p->ldifs[i]);
    speex_diffuser_flush(p->rdifs[i]);
  }
  memset(p->d, 0, FDNORDER * sizeof(int));
  memset(p->u, 0, FDNORDER * sizeof(int));
  memset(p->f1, 0, FDNORDER * sizeof(int));
  speex_fixeddelay_flush(p->tapdelay);
}