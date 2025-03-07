/* Copyright (C) 2002 Jean-Marc Valin 
   File: exc_8_128_table.c
   Codebook for excitation in narrowband CELP mode (7000 bps)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:
   
   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
   
   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
   
   - Neither the name of the Xiph.org Foundation nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.
   
   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef __cplusplus
extern "C" const signed char exc_8_128_table[1024] = {
#else
const signed char exc_8_128_table[1024] = {
#endif
-14,9,13,-32,2,-10,31,-10,
-8,-8,6,-4,-1,10,-64,23,
6,20,13,6,8,-22,16,34,
7,42,-49,-28,5,26,4,-15,
41,34,41,32,33,24,23,14,
8,40,34,4,-24,-41,-19,-15,
13,-13,33,-54,24,27,-44,33,
27,-15,-15,24,-19,14,-36,14,
-9,24,-12,-4,37,-5,16,-34,
5,10,33,-15,-54,-16,12,25,
12,1,2,0,3,-1,-4,-4,
11,2,-56,54,27,-20,13,-6,
-46,-41,-33,-11,-5,7,12,14,
-14,-5,8,20,6,3,4,-8,
-5,-42,11,8,-14,25,-2,2,
13,11,-22,39,-9,9,5,-45,
-9,7,-9,12,-7,34,-17,-102,
7,2,-42,18,35,-9,-34,11,
-5,-2,3,22,46,-52,-25,-9,
-94,8,11,-5,-5,-5,4,-7,
-35,-7,54,5,-32,3,24,-9,
-22,8,65,37,-1,-12,-23,-6,
-9,-28,55,-33,14,-3,2,18,
-60,41,-17,8,-16,17,-11,0,
-11,29,-28,37,9,-53,33,-14,
-9,7,-25,-7,-11,26,-32,-8,
24,-21,22,-19,19,-10,29,-14,
0,0,0,0,0,0,0,0,
-5,-52,10,41,6,-30,-4,16,
32,22,-27,-22,32,-3,-28,-3,
3,-35,6,17,23,21,8,2,
4,-45,-17,14,23,-4,-31,-11,
-3,14,1,19,-11,2,61,-8,
9,-12,7,-10,12,-3,-24,99,
-48,23,50,-37,-5,-23,0,8,
-14,35,-64,-5,46,-25,13,-1,
-49,-19,-15,9,34,50,25,11,
-6,-9,-16,-20,-32,-33,-32,-27,
10,-8,12,-15,56,-14,-32,33,
3,-9,1,65,-9,-9,-10,-2,
-6,-23,9,17,3,-28,13,-32,
4,-2,-10,4,-16,76,12,-52,
6,13,33,-6,4,-14,-9,-3,
1,-15,-16,28,1,-15,11,16,
9,4,-21,-37,-40,-6,22,12,
-15,-23,-14,-17,-16,-9,-10,-9,
13,-39,41,5,-9,16,-38,25,
46,-47,4,49,-14,17,-2,6,
18,5,-6,-33,-22,44,50,-2,
1,3,-6,7,7,-3,-21,38,
-18,34,-14,-41,60,-13,6,16,
-24,35,19,-13,-36,24,3,-17,
-14,-10,36,44,-44,-29,-3,3,
-54,-8,12,55,26,4,-2,-5,
2,-11,22,-23,2,22,1,-25,
-39,66,-49,21,-8,-2,10,-14,
-60,25,6,10,27,-25,16,5,
-2,-9,26,-13,-20,58,-2,7,
52,-9,2,5,-4,-15,23,-1,
-38,23,8,27,-6,0,-27,-7,
39,-10,-14,26,11,-45,-12,9,
-5,34,4,-35,10,43,-22,-11,
56,-7,20,1,10,1,-26,9,
94,11,-27,-14,-13,1,-11,0,
14,-5,-6,-10,-4,-15,-8,-41,
21,-5,1,-28,-8,22,-9,33,
-23,-4,-4,-12,39,4,-7,3,
-60,80,8,-17,2,-6,12,-5,
1,9,15,27,31,30,27,23,
61,47,26,10,-5,-8,-12,-13,
5,-18,25,-15,-4,-15,-11,12,
-2,-2,-16,-2,-6,24,12,11,
-4,9,1,-9,14,-45,57,12,
20,-35,26,11,-64,32,-10,-10,
42,-4,-9,-16,32,24,7,10,
52,-11,-57,29,0,8,0,-6,
17,-17,-56,-40,7,20,18,12,
-6,16,5,7,-1,9,1,10,
29,12,16,13,-2,23,7,9,
-3,-4,-5,18,-64,13,55,-25,
9,-9,24,14,-25,15,-11,-40,
-30,37,1,-19,22,-5,-31,13,
-2,0,7,-4,16,-67,12,66,
-36,24,-8,18,-15,-23,19,0,
-45,-7,4,3,-13,13,35,5,
13,33,10,27,23,0,-7,-11,
43,-74,36,-12,2,5,-8,6,
-33,11,-16,-14,-5,-7,-3,17,
-34,27,-16,11,-9,15,33,-31,
8,-16,7,-6,-7,63,-55,-17,
11,-1,20,-46,34,-30,6,9,
19,28,-9,5,-24,-8,-23,-2,
31,-19,-16,-5,-15,-18,0,26,
18,37,-5,-15,-2,17,5,-27,
21,-33,44,12,-27,-9,17,11,
25,-21,-31,-7,13,33,-8,-25,
-7,7,-10,4,-6,-9,48,-82,
-23,-8,6,11,-23,3,-3,49,
-29,25,31,4,14,16,9,-4,
-18,10,-26,3,5,-44,-9,9,
-47,-55,15,9,28,1,4,-3,
46,6,-6,-38,-29,-31,-15,-6,
3,0,14,-6,8,-54,-50,33,
-5,1,-14,33,-48,26,-4,-5,
-3,-5,-3,-5,-28,-22,77,55,
-1,2,10,10,-9,-14,-66,-49,
11,-36,-6,-20,10,-10,16,12,
4,-1,-16,45,-44,-50,31,-2,
25,42,23,-32,-22,0,11,20,
-40,-35,-40,-36,-32,-26,-21,-13,
52,-22,6,-24,-20,17,-5,-8,
36,-25,-11,21,-26,6,34,-8,
7,20,-3,5,-25,-8,18,-5,
-9,-4,1,-9,20,20,39,48,
-24,9,5,-65,22,29,4,3,
-43,-11,32,-6,9,19,-27,-10,
-47,-14,24,10,-7,-36,-7,-1,
-4,-5,-5,16,53,25,-26,-29,
-4,-12,45,-58,-34,33,-5,2,
-1,27,-48,31,-15,22,-5,4,
7,7,-25,-3,11,-22,16,-12,
8,-3,7,-11,45,14,-73,-19,
56,-46,24,-20,28,-12,-2,-1,
-36,-3,-33,19,-6,7,2,-15,
5,-31,-45,8,35,13,20,0,
-9,48,-13,-43,-3,-13,2,-5,
72,-68,-27,2,1,-2,-7,5,
36,33,-40,-12,-4,-5,23,19};
