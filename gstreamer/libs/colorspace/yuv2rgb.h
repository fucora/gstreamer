/*
 * Copyright (c) 1995 The Regents of the University of California.
 * All rights reserved.
 * 
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement is
 * hereby granted, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 * 
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF
 * CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

typedef struct _GstColorSpaceYUVTables GstColorSpaceYUVTables;

struct _GstColorSpaceYUVTables {
  int gammaCorrectFlag;
  double gammaCorrect;
  int chromaCorrectFlag;
  double chromaCorrect;

  int *L_tab, *Cr_r_tab, *Cr_g_tab, *Cb_g_tab, *Cb_b_tab;

  /*
   *  We define tables that convert a color value between -256 and 512
   *  into the R, G and B parts of the pixel. The normal range is 0-255.
   **/

  long *r_2_pix;
  long *g_2_pix;
  long *b_2_pix;
};


#define CB_BASE 1
#define CR_BASE (CB_BASE*CB_RANGE)
#define LUM_BASE (CR_BASE*CR_RANGE)

#define Min(x,y) (((x) < (y)) ? (x) : (y))
#define Max(x,y) (((x) > (y)) ? (x) : (y))

#define GAMMA_CORRECTION(x) ((int)(pow((x) / 255.0, 1.0 / gammaCorrect) * 255.0))
#define CHROMA_CORRECTION256(x) ((x) >= 128 \
                        ? 128 + Min(127, (int)(((x) - 128.0) * chromaCorrect)) \
                        : 128 - Min(128, (int)((128.0 - (x)) * chromaCorrect)))
#define CHROMA_CORRECTION128(x) ((x) >= 0 \
                        ? Min(127,  (int)(((x) * chromaCorrect))) \
                        : Max(-128, (int)(((x) * chromaCorrect))))
#define CHROMA_CORRECTION256D(x) ((x) >= 128 \
                        ? 128.0 + Min(127.0, (((x) - 128.0) * chromaCorrect)) \
                        : 128.0 - Min(128.0, (((128.0 - (x)) * chromaCorrect))))
#define CHROMA_CORRECTION128D(x) ((x) >= 0 \
                        ? Min(127.0,  ((x) * chromaCorrect)) \
                        : Max(-128.0, ((x) * chromaCorrect)))


