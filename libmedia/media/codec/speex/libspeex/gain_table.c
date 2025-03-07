/* Copyright (C) 2002 Jean-Marc Valin 
   File: gain_table.c
   Codebook for 3-tap pitch prediction gain (128 entries)
  
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

   1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.  

   2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
   IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
   OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
   HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
   STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.
*/
#ifdef __cplusplus
extern "C" const signed char gain_cdbk_nb[512] = {
#else
const signed char gain_cdbk_nb[512] = {
#endif
-32, -32, -32, 0,
-28, -67, -5, 33,
-42, -6, -32, 18,
-57, -10, -54, 35,
-16, 27, -41, 42,
19, -19, -40, 36,
-45, 24, -21, 40,
-8, -14, -18, 28,
1, 14, -58, 53,
-18, -88, -39, 39,
-38, 21, -18, 37,
-19, 20, -43, 38,
10, 17, -48, 54,
-52, -58, -13, 33,
-44, -1, -11, 32,
-12, -11, -34, 22,
14, 0, -46, 46,
-37, -35, -34, 5,
-25, 44, -30, 43,
6, -4, -63, 49,
-31, 43, -41, 43,
-23, 30, -43, 41,
-43, 26, -14, 44,
-33, 1, -13, 27,
-13, 18, -37, 37,
-46, -73, -45, 34,
-36, 24, -25, 34,
-36, -11, -20, 19,
-25, 12, -18, 33,
-36, -69, -59, 34,
-45, 6, 8, 46,
-22, -14, -24, 18,
-1, 13, -44, 44,
-39, -48, -26, 15,
-32, 31, -37, 34,
-33, 15, -46, 31,
-24, 30, -36, 37,
-41, 31, -23, 41,
-50, 22, -4, 50,
-22, 2, -21, 28,
-17, 30, -34, 40,
-7, -60, -28, 29,
-38, 42, -28, 42,
-44, -11, 21, 43,
-16, 8, -44, 34,
-39, -55, -43, 21,
-11, -35, 26, 41,
-9, 0, -34, 29,
-8, 121, -81, 113,
7, -16, -22, 33,
-37, 33, -31, 36,
-27, -7, -36, 17,
-34, 70, -57, 65,
-37, -11, -48, 21,
-40, 17, -1, 44,
-33, 6, -6, 33,
-9, 0, -20, 34,
-21, 69, -33, 57,
-29, 33, -31, 35,
-55, 12, -1, 49,
-33, 27, -22, 35,
-50, -33, -47, 17,
-50, 54, 51, 94,
-1, -5, -44, 35,
-4, 22, -40, 45,
-39, -66, -25, 24,
-33, 1, -26, 20,
-24, -23, -25, 12,
-11, 21, -45, 44,
-25, -45, -19, 17,
-43, 105, -16, 82,
5, -21, 1, 41,
-16, 11, -33, 30,
-13, -99, -4, 57,
-37, 33, -15, 44,
-25, 37, -63, 54,
-36, 24, -31, 31,
-53, -56, -38, 26,
-41, -4, 4, 37,
-33, 13, -30, 24,
49, 52, -94, 114,
-5, -30, -15, 23,
1, 38, -40, 56,
-23, 12, -36, 29,
-17, 40, -47, 51,
-37, -41, -39, 11,
-49, 34, 0, 58,
-18, -7, -4, 34,
-16, 17, -27, 35,
30, 5, -62, 65,
4, 48, -68, 76,
-43, 11, -11, 38,
-18, 19, -15, 41,
-23, -62, -39, 23,
-42, 10, -2, 41,
-21, -13, -13, 25,
-9, 13, -47, 42,
-23, -62, -24, 24,
-44, 60, -21, 58,
-18, -3, -52, 32,
-22, 22, -36, 34,
-75, 57, 16, 90,
-19, 3, 10, 45,
-29, 23, -38, 32,
-5, -62, -51, 38,
-51, 40, -18, 53,
-42, 13, -24, 32,
-34, 14, -20, 30,
-56, -75, -26, 37,
-26, 32, 15, 59,
-26, 17, -29, 29,
-7, 28, -52, 53,
-12, -30, 5, 30,
-5, -48, -5, 35,
2, 2, -43, 40,
21, 16, 16, 75,
-25, -45, -32, 10,
-43, 18, -10, 42,
9, 0, -1, 52,
-1, 7, -30, 36,
19, -48, -4, 48,
-28, 25, -29, 32,
-22, 0, -31, 22,
-32, 17, -10, 36,
-64, -41, -62, 36,
-52, 15, 16, 58,
-30, -22, -32, 6,
-7, 9, -38, 36};
