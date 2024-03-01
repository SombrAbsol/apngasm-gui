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
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include "apngasm_gui.h"
#include "resource.h"
#include "image.h"
#include "png.h"     /* original (unpatched) libpng is ok */
#include "zlib.h"
#include "7z.h"
#include "zopfli.h"

typedef struct { Image * image; unsigned int size; int x, y, w, h, valid, filters; } OP;
OP   op[6];

unsigned char * op_zbuf1;
unsigned char * op_zbuf2;
z_stream        op_zstream1;
z_stream        op_zstream2;

unsigned int    next_seq_num;
unsigned char * row_buf;
unsigned char * sub_row;
unsigned char * up_row;
unsigned char * avg_row;
unsigned char * paeth_row;
unsigned char   png_sign[8] = {137,  80,  78,  71,  13,  10,  26,  10};
unsigned char   png_Software[28] = { 83, 111, 102, 116, 119, 97, 114, 101, '\0',
                                     65,  80,  78,  71,  32, 65, 115, 115, 101,
                                    109,  98, 108, 101, 114, 32,  50,  46,  57,  49};

extern HWND hMainDlg;

void write_chunk(FILE * f, const char * name, unsigned char * data, unsigned int length)
{
  unsigned char buf[4];
  unsigned int crc = (unsigned int)crc32(0, Z_NULL, 0);

  png_save_uint_32(buf, length);
  fwrite(buf, 1, 4, f);
  fwrite(name, 1, 4, f);
  crc = (unsigned int)crc32(crc, (const Bytef *)name, 4);

  if (memcmp(name, "fdAT", 4) == 0)
  {
    png_save_uint_32(buf, next_seq_num++);
    fwrite(buf, 1, 4, f);
    crc = (unsigned int)crc32(crc, buf, 4);
    length -= 4;
  }

  if (data != NULL && length > 0)
  {
    fwrite(data, 1, length, f);
    crc = (unsigned int)crc32(crc, data, length);
  }

  png_save_uint_32(buf, crc);
  fwrite(buf, 1, 4, f);
}

void write_IDATs(FILE * f, unsigned int frame, unsigned char * data, unsigned int length, unsigned int idat_size)
{
  unsigned int z_cmf = data[0];
  if ((z_cmf & 0x0f) == 8 && (z_cmf & 0xf0) <= 0x70)
  {
    if (length >= 2)
    {
      unsigned int z_cinfo = z_cmf >> 4;
      unsigned int half_z_window_size = 1 << (z_cinfo + 7);
      while (idat_size <= half_z_window_size && half_z_window_size >= 256)
      {
        z_cinfo--;
        half_z_window_size >>= 1;
      }
      z_cmf = (z_cmf & 0x0f) | (z_cinfo << 4);
      if (data[0] != (unsigned char)z_cmf)
      {
        data[0] = (unsigned char)z_cmf;
        data[1] &= 0xe0;
        data[1] += (unsigned char)(0x1f - ((z_cmf << 8) + data[1]) % 0x1f);
      }
    }
  }

  while (length > 0)
  {
    unsigned int ds = length;
    if (ds > 32768)
      ds = 32768;

    if (frame == 0)
      write_chunk(f, "IDAT", data, ds);
    else
      write_chunk(f, "fdAT", data, ds+4);

    data += ds;
    length -= ds;
  }
}

void process_rect(Image * image, int xbytes, int rowbytes, int y, int h, int bpp, unsigned char * dest)
{
  int i, j, v;
  int a, b, c, pa, pb, pc, p;
  unsigned char * prev = NULL;
  unsigned char * dp  = dest;
  unsigned char * out;

  for (j=y; j<y+h; j++)
  {
    unsigned char * row = image->rows[j] + xbytes;
    unsigned int    sum = 0;
    unsigned char * best_row = row_buf;
    unsigned int    mins = ((unsigned int)(-1)) >> 1;

    out = row_buf+1;
    for (i=0; i<rowbytes; i++)
    {
      v = out[i] = row[i];
      sum += (v < 128) ? v : 256 - v;
    }
    mins = sum;

    sum = 0;
    out = sub_row+1;
    for (i=0; i<bpp; i++)
    {
      v = out[i] = row[i];
      sum += (v < 128) ? v : 256 - v;
    }
    for (i=bpp; i<rowbytes; i++)
    {
      v = out[i] = row[i] - row[i-bpp];
      sum += (v < 128) ? v : 256 - v;
      if (sum > mins) break;
    }
    if (sum < mins)
    {
      mins = sum;
      best_row = sub_row;
    }

    if (prev)
    {
      sum = 0;
      out = up_row+1;
      for (i=0; i<rowbytes; i++)
      {
        v = out[i] = row[i] - prev[i];
        sum += (v < 128) ? v : 256 - v;
        if (sum > mins) break;
      }
      if (sum < mins)
      {
        mins = sum;
        best_row = up_row;
      }

      sum = 0;
      out = avg_row+1;
      for (i=0; i<bpp; i++)
      {
        v = out[i] = row[i] - prev[i]/2;
        sum += (v < 128) ? v : 256 - v;
      }
      for (i=bpp; i<rowbytes; i++)
      {
        v = out[i] = row[i] - (prev[i] + row[i-bpp])/2;
        sum += (v < 128) ? v : 256 - v;
        if (sum > mins) break;
      }
      if (sum < mins)
      {
        mins = sum;
        best_row = avg_row;
      }

      sum = 0;
      out = paeth_row+1;
      for (i=0; i<bpp; i++)
      {
        v = out[i] = row[i] - prev[i];
        sum += (v < 128) ? v : 256 - v;
      }
      for (i=bpp; i<rowbytes; i++)
      {
        a = row[i-bpp];
        b = prev[i];
        c = prev[i-bpp];
        p = b - c;
        pc = a - c;
        pa = abs(p);
        pb = abs(pc);
        pc = abs(p + pc);
        p = (pa <= pb && pa <=pc) ? a : (pb <= pc) ? b : c;
        v = out[i] = row[i] - p;
        sum += (v < 128) ? v : 256 - v;
        if (sum > mins) break;
      }
      if (sum < mins)
      {
        best_row = paeth_row;
      }
    }

    if (dest == NULL)
    {
      // deflate_rect_op()
      op_zstream1.next_in = row_buf;
      op_zstream1.avail_in = rowbytes + 1;
      deflate(&op_zstream1, Z_NO_FLUSH);

      op_zstream2.next_in = best_row;
      op_zstream2.avail_in = rowbytes + 1;
      deflate(&op_zstream2, Z_NO_FLUSH);
    }
    else
    {
      // deflate_rect_fin()
      memcpy(dp, best_row, rowbytes+1);
      dp += rowbytes+1;
    }

    prev = row;
  }
}

void deflate_rect_fin(int deflate_method, int iter, unsigned char * zbuf, unsigned int * zsize, int bpp, unsigned char * dest, int zbuf_size, int n)
{
  Image * image = op[n].image;
  int xbytes = op[n].x*bpp;
  int rowbytes = op[n].w*bpp;
  int y = op[n].y;
  int h = op[n].h;

  if (op[n].filters == 0)
  {
    unsigned char * dp = dest;
    for (int j=y; j<y+h; j++)
    {
      *dp++ = 0;
      memcpy(dp, image->rows[j] + xbytes, rowbytes);
      dp += rowbytes;
    }
  }
  else
    process_rect(image, xbytes, rowbytes, y, h, bpp, dest);

  if (deflate_method == 2)
  {
    ZopfliOptions opt_zopfli;
    unsigned char* data = 0;
    size_t size = 0;
    ZopfliInitOptions(&opt_zopfli);
    opt_zopfli.numiterations = iter;
    ZopfliCompress(&opt_zopfli, ZOPFLI_FORMAT_ZLIB, dest, h*(rowbytes + 1), &data, &size);
    if (size < (size_t)zbuf_size)
    {
      memcpy(zbuf, data, size);
      *zsize = (unsigned int)size;
    }
    free(data);
  }
  else
  if (deflate_method == 1)
  {
    unsigned size = zbuf_size;
    compress_rfc1950_7z(dest, h*(rowbytes + 1), zbuf, size, iter<100 ? iter : 100, 255);
    *zsize = size;
  }
  else
  {
    z_stream fin_zstream;

    fin_zstream.data_type = Z_BINARY;
    fin_zstream.zalloc = Z_NULL;
    fin_zstream.zfree = Z_NULL;
    fin_zstream.opaque = Z_NULL;
    deflateInit2(&fin_zstream, Z_BEST_COMPRESSION, 8, 15, 8, op[n].filters ? Z_FILTERED : Z_DEFAULT_STRATEGY);

    fin_zstream.next_out = zbuf;
    fin_zstream.avail_out = zbuf_size;
    fin_zstream.next_in = dest;
    fin_zstream.avail_in = h*(rowbytes + 1);
    deflate(&fin_zstream, Z_FINISH);
    *zsize = (unsigned int)fin_zstream.total_out;
    deflateEnd(&fin_zstream);
  }
}

void deflate_rect_op(Image * image, int x, int y, int w, int h, int bpp, int zbuf_size, int n)
{
  op_zstream1.data_type = Z_BINARY;
  op_zstream1.next_out = op_zbuf1;
  op_zstream1.avail_out = zbuf_size;

  op_zstream2.data_type = Z_BINARY;
  op_zstream2.next_out = op_zbuf2;
  op_zstream2.avail_out = zbuf_size;

  process_rect(image, x * bpp, w * bpp, y, h, bpp, NULL);

  deflate(&op_zstream1, Z_FINISH);
  deflate(&op_zstream2, Z_FINISH);
  op[n].image = image;

  if (op_zstream1.total_out < op_zstream2.total_out)
  {
    op[n].size = (unsigned int)op_zstream1.total_out;
    op[n].filters = 0;
  }
  else
  {
    op[n].size = (unsigned int)op_zstream2.total_out;
    op[n].filters = 1;
  }
  op[n].x = x;
  op[n].y = y;
  op[n].w = w;
  op[n].h = h;
  op[n].valid = 1;
  deflateReset(&op_zstream1);
  deflateReset(&op_zstream2);
}

void get_rect(unsigned int w, unsigned int h, Image * image1, Image * image2, Image * temp, unsigned int bpp, int zbuf_size, unsigned int has_tcolor, unsigned int tcolor, int n)
{
  unsigned int   i, j, x0, y0, w0, h0;
  unsigned int   x_min = w-1;
  unsigned int   y_min = h-1;
  unsigned int   x_max = 0;
  unsigned int   y_max = 0;
  unsigned int   diffnum = 0;
  unsigned int   over_is_possible = 1;

  if (!has_tcolor)
    over_is_possible = 0;

  if (bpp == 1)
  {
    for (j=0; j<h; j++)
    {
      unsigned char *pa = image1->rows[j];
      unsigned char *pb = image2->rows[j];
      unsigned char *pc = temp->rows[j];
      for (i=0; i<w; i++)
      {
        unsigned char c = *pb++;
        if (*pa++ != c)
        {
          diffnum++;
          if (has_tcolor && c == tcolor) over_is_possible = 0;
          if (i<x_min) x_min = i;
          if (i>x_max) x_max = i;
          if (j<y_min) y_min = j;
          if (j>y_max) y_max = j;
        }
        else
          c = tcolor;

        *pc++ = c;
      }
    }
  }
  else
  if (bpp == 2)
  {
    for (j=0; j<h; j++)
    {
      unsigned short *pa = (unsigned short *)image1->rows[j];
      unsigned short *pb = (unsigned short *)image2->rows[j];
      unsigned short *pc = (unsigned short *)temp->rows[j];
      for (i=0; i<w; i++)
      {
        unsigned int c1 = *pa++;
        unsigned int c2 = *pb++;
        if ((c1 != c2) && ((c1>>8) || (c2>>8)))
        {
          diffnum++;
          if ((c2 >> 8) != 0xFF) over_is_possible = 0;
          if (i<x_min) x_min = i;
          if (i>x_max) x_max = i;
          if (j<y_min) y_min = j;
          if (j>y_max) y_max = j;
        }
        else
          c2 = 0;

        *pc++ = c2;
      }
    }
  }
  else
  if (bpp == 3)
  {
    for (j=0; j<h; j++)
    {
      unsigned char *pa = image1->rows[j];
      unsigned char *pb = image2->rows[j];
      unsigned char *pc = temp->rows[j];
      for (i=0; i<w; i++)
      {
        unsigned int c1 = (pa[2]<<16) + (pa[1]<<8) + pa[0];
        unsigned int c2 = (pb[2]<<16) + (pb[1]<<8) + pb[0];
        if (c1 != c2)
        {
          diffnum++;
          if (has_tcolor && c2 == tcolor) over_is_possible = 0;
          if (i<x_min) x_min = i;
          if (i>x_max) x_max = i;
          if (j<y_min) y_min = j;
          if (j>y_max) y_max = j;
        }
        else
          c2 = tcolor;

        memcpy(pc, &c2, 3);
        pa += 3;
        pb += 3;
        pc += 3;
      }
    }
  }
  else
  if (bpp == 4)
  {
    for (j=0; j<h; j++)
    {
      unsigned int *pa = (unsigned int *)image1->rows[j];
      unsigned int *pb = (unsigned int *)image2->rows[j];
      unsigned int *pc = (unsigned int *)temp->rows[j];
      for (i=0; i<w; i++)
      {
        unsigned int c1 = *pa++;
        unsigned int c2 = *pb++;
        if ((c1 != c2) && ((c1>>24) || (c2>>24)))
        {
          diffnum++;
          if ((c2 >> 24) != 0xFF) over_is_possible = 0;
          if (i<x_min) x_min = i;
          if (i>x_max) x_max = i;
          if (j<y_min) y_min = j;
          if (j>y_max) y_max = j;
        }
        else
          c2 = 0;

        *pc++ = c2;
      }
    }
  }

  if (diffnum == 0)
  {
    x0 = y0 = 0;
    w0 = h0 = 1;
  }
  else
  {
    x0 = x_min;
    y0 = y_min;
    w0 = x_max-x_min+1;
    h0 = y_max-y_min+1;
  }

  deflate_rect_op(image2, x0, y0, w0, h0, bpp, zbuf_size, n*2);

  if (over_is_possible)
    deflate_rect_op(temp, x0, y0, w0, h0, bpp, zbuf_size, n*2+1);
}

int load_image_sequence(FILE_INFO * pInputFiles, UINT frames, std::vector<Image>& img, unsigned char * coltype)
{
  wchar_t szBuf[MAX_PATH+1];

  img.resize(frames);

  FILE_INFO * pFileInfo = pInputFiles;
  for (unsigned int i=0; i<frames; i++, pFileInfo++)
  {
    if (load_image(pFileInfo->szPath, &img[i]))
    {
      swprintf(szBuf, MAX_PATH, L"load_image(%s) failed    ", pFileInfo->szPath);
      MessageBox(hMainDlg, szBuf, 0, MB_ICONINFORMATION);
      return 1;
    }

    img[i].delay_num = pFileInfo->delay_num;
    img[i].delay_den = pFileInfo->delay_den;

    if (img[0].w != img[i].w || img[0].h != img[i].h)
    {
      swprintf(szBuf, MAX_PATH, L"Error: all frames are expected to be %dx%d    \n\n%s%s is %dx%d    \n ", img[0].w, img[0].h, pFileInfo->szName, pFileInfo->szExt, img[i].w, img[i].h);
      MessageBox(hMainDlg, szBuf, 0, MB_ICONINFORMATION);
      return 1;
    }
  }

  *coltype = find_common_coltype(img);

  return 0;
}

int save_apng(wchar_t * szOut, std::vector<Image>& img, unsigned int loops, unsigned int first, int deflate_method, int iter)
{
  wchar_t szBuf[MAX_PATH+1];
  unsigned char coltype = img[0].type;
  unsigned int has_tcolor = 0;
  unsigned int tcolor = 0;

  if (coltype == 0)
  {
    if (img[0].ts)
    {
      has_tcolor = 1;
      tcolor = img[0].tr[1];
    }
  }
  else
  if (coltype == 2)
  {
    if (img[0].ts)
    {
      has_tcolor = 1;
      tcolor = (((img[0].tr[5]<<8)+img[0].tr[3])<<8)+img[0].tr[1];
    }
  }
  else
  if (coltype == 3)
  {
    for (int c=0; c<img[0].ts; c++)
    if (img[0].tr[c] == 0)
    {
      has_tcolor = 1;
      tcolor = c;
      break;
    }
  }
  else
    has_tcolor = 1;

  FILE * f;
  if ((f = _wfopen(szOut, L"wb")) == 0)
  {
    swprintf(szBuf, MAX_PATH, L"Error: can't save to file:    \n\n%s  \n ", szOut);
    MessageBox(hMainDlg, szBuf, 0, MB_ICONINFORMATION);
    SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, 0, 0);
    return 1;
  }

  unsigned char buf_IHDR[13];
  unsigned char buf_acTL[8];
  unsigned char buf_fcTL[26];

  unsigned int width = img[0].w;
  unsigned int height = img[0].h;
  unsigned int bpp = img[0].bpp;
  unsigned int rowbytes  = width * bpp;
  unsigned int visible = (unsigned int)(img.size() - first);

  Image temp, over1, over2, over3, rest;
  temp.init(&img[0]);
  over1.init(&img[0]);
  over2.init(&img[0]);
  over3.init(&img[0]);
  rest.init(&img[0]);
  unsigned char * dest  = new unsigned char[(rowbytes + 1) * height];

  png_save_uint_32(buf_IHDR, width);
  png_save_uint_32(buf_IHDR + 4, height);
  buf_IHDR[8] = 8;
  buf_IHDR[9] = coltype;
  buf_IHDR[10] = 0;
  buf_IHDR[11] = 0;
  buf_IHDR[12] = 0;

  png_save_uint_32(buf_acTL, visible);
  png_save_uint_32(buf_acTL + 4, loops);

  fwrite(png_sign, 1, 8, f);

  write_chunk(f, "IHDR", buf_IHDR, 13);

  if (img.size() > 1)
    write_chunk(f, "acTL", buf_acTL, 8);
  else
    first = 0;

  if (img[0].ps > 0)
    write_chunk(f, "PLTE", (unsigned char *)(&img[0].pl), img[0].ps*3);

  if (img[0].ts > 0)
    write_chunk(f, "tRNS", img[0].tr, img[0].ts);

  op_zstream1.data_type = Z_BINARY;
  op_zstream1.zalloc = Z_NULL;
  op_zstream1.zfree = Z_NULL;
  op_zstream1.opaque = Z_NULL;
  deflateInit2(&op_zstream1, Z_BEST_SPEED+1, 8, 15, 8, Z_DEFAULT_STRATEGY);

  op_zstream2.data_type = Z_BINARY;
  op_zstream2.zalloc = Z_NULL;
  op_zstream2.zfree = Z_NULL;
  op_zstream2.opaque = Z_NULL;
  deflateInit2(&op_zstream2, Z_BEST_SPEED+1, 8, 15, 8, Z_FILTERED);

  unsigned int idat_size = (rowbytes + 1) * height;
  unsigned int zbuf_size = idat_size + ((idat_size + 7) >> 3) + ((idat_size + 63) >> 6) + 11;

  unsigned char * zbuf = new unsigned char[zbuf_size];
  op_zbuf1 = new unsigned char[zbuf_size];
  op_zbuf2 = new unsigned char[zbuf_size];
  row_buf = new unsigned char[rowbytes + 1];
  sub_row = new unsigned char[rowbytes + 1];
  up_row = new unsigned char[rowbytes + 1];
  avg_row = new unsigned char[rowbytes + 1];
  paeth_row = new unsigned char[rowbytes + 1];

  row_buf[0] = 0;
  sub_row[0] = 1;
  up_row[0] = 2;
  avg_row[0] = 3;
  paeth_row[0] = 4;

  unsigned int i, j, k;
  unsigned int zsize = 0;
  unsigned int x0 = 0;
  unsigned int y0 = 0;
  unsigned int w0 = width;
  unsigned int h0 = height;
  unsigned char bop = 0;
  unsigned char dop = 0;
  next_seq_num = 0;

  SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETRANGE, 0, MAKELPARAM(0, 4800));
  SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, 100, 0);
  SetDlgItemText(hMainDlg, IDC_PERCENT, L"2 %");

  for (j=0; j<6; j++)
    op[j].valid = 0;
  deflate_rect_op(&img[0], x0, y0, w0, h0, bpp, zbuf_size, 0);
  deflate_rect_fin(deflate_method, iter, zbuf, &zsize, bpp, dest, zbuf_size, 0);

  if (first)
  {
    write_IDATs(f, 0, zbuf, zsize, idat_size);

    for (j=0; j<6; j++)
      op[j].valid = 0;
    deflate_rect_op(&img[1], x0, y0, w0, h0, bpp, zbuf_size, 0);
    deflate_rect_fin(deflate_method, iter, zbuf, &zsize, bpp, dest, zbuf_size, 0);
  }

  for (i=first; i<img.size()-1; i++)
  {
    unsigned int op_min;
    int          op_best;

    SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, 100 + 4700*(i+1)/img.size(), 0);
    _snwprintf(szBuf, 8, L"%d %%\0", (int)(100 + 4700*(i+1)/img.size())/48);
    SetDlgItemText(hMainDlg, IDC_PERCENT, szBuf);

    for (j=0; j<6; j++)
      op[j].valid = 0;

    /* dispose = none */
    get_rect(width, height, &img[i], &img[i+1], &over1, bpp, zbuf_size, has_tcolor, tcolor, 0);

    /* dispose = background */
    if (has_tcolor)
    {
      for (j=0; j<height; j++)
        memcpy(temp.rows[j], img[i].rows[j], rowbytes);
      if (coltype == 2)
        for (j=0; j<h0; j++)
          for (k=0; k<w0; k++)
            memcpy(temp.rows[j+y0] + (k+x0)*3, &tcolor, 3);
      else
        for (j=0; j<h0; j++)
          memset(temp.rows[j+y0] + x0*bpp, tcolor, w0*bpp);

      get_rect(width, height, &temp, &img[i+1], &over2, bpp, zbuf_size, has_tcolor, tcolor, 1);
    }

    /* dispose = previous */
    if (i > first)
      get_rect(width, height, &rest, &img[i+1], &over3, bpp, zbuf_size, has_tcolor, tcolor, 2);

    op_min = op[0].size;
    op_best = 0;
    for (j=1; j<6; j++)
    if (op[j].valid)
    {
      if (op[j].size < op_min)
      {
        op_min = op[j].size;
        op_best = j;
      }
    }

    dop = op_best >> 1;

    png_save_uint_32(buf_fcTL, next_seq_num++);
    png_save_uint_32(buf_fcTL + 4, w0);
    png_save_uint_32(buf_fcTL + 8, h0);
    png_save_uint_32(buf_fcTL + 12, x0);
    png_save_uint_32(buf_fcTL + 16, y0);
    png_save_uint_16(buf_fcTL + 20, img[i].delay_num);
    png_save_uint_16(buf_fcTL + 22, img[i].delay_den);
    buf_fcTL[24] = dop;
    buf_fcTL[25] = bop;
    write_chunk(f, "fcTL", buf_fcTL, 26);

    write_IDATs(f, i, zbuf, zsize, idat_size);

    /* process apng dispose - begin */
    if (dop != 2)
      for (j=0; j<height; j++)
        memcpy(rest.rows[j], img[i].rows[j], rowbytes);

    if (dop == 1)
    {
      if (coltype == 2)
        for (j=0; j<h0; j++)
          for (k=0; k<w0; k++)
            memcpy(rest.rows[j+y0] + (k+x0)*3, &tcolor, 3);
      else
        for (j=0; j<h0; j++)
          memset(rest.rows[j+y0] + x0*bpp, tcolor, w0*bpp);
    }
    /* process apng dispose - end */

    x0 = op[op_best].x;
    y0 = op[op_best].y;
    w0 = op[op_best].w;
    h0 = op[op_best].h;
    bop = op_best & 1;

    deflate_rect_fin(deflate_method, iter, zbuf, &zsize, bpp, dest, zbuf_size, op_best);
  }

  if (img.size() > 1)
  {
    png_save_uint_32(buf_fcTL, next_seq_num++);
    png_save_uint_32(buf_fcTL + 4, w0);
    png_save_uint_32(buf_fcTL + 8, h0);
    png_save_uint_32(buf_fcTL + 12, x0);
    png_save_uint_32(buf_fcTL + 16, y0);
    png_save_uint_16(buf_fcTL + 20, img.back().delay_num);
    png_save_uint_16(buf_fcTL + 22, img.back().delay_den);
    buf_fcTL[24] = 0;
    buf_fcTL[25] = bop;
    write_chunk(f, "fcTL", buf_fcTL, 26);
  }

  write_IDATs(f, (unsigned int)(img.size()-1), zbuf, zsize, idat_size);

  write_chunk(f, "tEXt", png_Software, 28);
  write_chunk(f, "IEND", 0, 0);
  fclose(f);

  SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, 4800, 0);
  SetDlgItemText(hMainDlg, IDC_PERCENT, L"100 %");

  delete[] zbuf;
  delete[] op_zbuf1;
  delete[] op_zbuf2;
  delete[] row_buf;
  delete[] sub_row;
  delete[] up_row;
  delete[] avg_row;
  delete[] paeth_row;

  deflateEnd(&op_zstream1);
  deflateEnd(&op_zstream2);

  temp.free();
  over1.free();
  over2.free();
  over3.free();
  rest.free();
  delete[] dest;

  return 0;
}

DWORD WINAPI CreateAPNG(FILE_INFO * pInputFiles, UINT frames, wchar_t * szOut, int deflate_method, int iter, int keep_palette, int keep_coltype, int first, int loops)
{
  std::vector<Image> img;
  unsigned int i;
  int res = 0;

  unsigned char coltype = 6;

  if (load_image_sequence(pInputFiles, frames, img, &coltype))
    return 1;

  for (i=0; i<img.size(); i++)
  {
    if (img[i].type != coltype)
      optim_upconvert(&img[i], coltype);
  }

  if (coltype == 6 || coltype == 4)
  {
    for (i=0; i<img.size(); i++)
      optim_dirty_transp(&img[i]);
  }

  optim_duplicates(img, first);

  if (!keep_coltype)
    optim_downconvert(img);

  coltype = img[0].type;

  if (coltype == 3 && !keep_palette)
    optim_palette(img);

  if (coltype == 2 || coltype == 0)
    optim_add_transp(img);

  res = save_apng(szOut, img, loops, first, deflate_method, iter);

  for (i=0; i<img.size(); i++)
    img[i].free();

  if (res)
    return 1;

  return 0;
}
