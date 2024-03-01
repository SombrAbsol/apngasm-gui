/* APNG Assembler 2.91
 *
 * This program creates APNG animation from PNG/TGA image sequence.
 *
 * http://apngasm.sourceforge.net/
 *
 * Copyright (c) 2009-2016 Max Stepin
 * maxst at users.sourceforge.net
 *
 * zlib license
 * ------------
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 */

typedef struct { wchar_t szPath[MAX_PATH+1];
                 wchar_t szDrive[_MAX_DRIVE+1];
                 wchar_t szDir[_MAX_DIR+1];
                 wchar_t szName[_MAX_FNAME+1];
                 wchar_t szExt[_MAX_EXT+1];
                 wchar_t szPrefix[_MAX_FNAME+1];
                 int num, delay_num, delay_den;
                 unsigned int w, h;
               } FILE_INFO;

typedef struct { FILE_INFO * pInputFiles;
                 UINT nNumInputFiles;
                 wchar_t * szOutput;
                 int first;
                 int loops;
                 int deflate_method;
                 int iter;
                 int keep_palette;
                 int keep_coltype;
               } FUNC_PARAMS, *PFUNC_PARAMS;
