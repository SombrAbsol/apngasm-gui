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
#include "apngasm_gui.h"
#include "resource.h"

HINSTANCE   hInst;
HICON       hIconMain;
HWND        hMainDlg;

FILE_INFO * pInputFiles;
UINT        nNumInputFiles;
wchar_t     szOutput[MAX_PATH+1];

int         deflate_method;
int         iter1;
int         iter2;
int         keep_palette;
int         keep_coltype;
int         first;
int         loops;
int         delay_num, delay_den;

PFUNC_PARAMS   pParams;
HANDLE         hConvertThread;
DWORD          dwThreadId;
DWORD WINAPI   CreateAPNG(FILE_INFO * pInputFiles, UINT frames, wchar_t * szOut, int deflate_method, int iter, int keep_palette, int keep_coltype, int first, int loops);

void EnableInterface(BOOL bEnable)
{
  EnableWindow(GetDlgItem(hMainDlg, IDC_LIST1), bEnable);
  EnableWindow(GetDlgItem(hMainDlg, IDC_PLAYBACK), bEnable);
  EnableWindow(GetDlgItem(hMainDlg, IDC_COMPRESSION), bEnable);
  EnableWindow(GetDlgItem(hMainDlg, IDC_DELAYS_ALL), bEnable && nNumInputFiles != 0);
  EnableWindow(GetDlgItem(hMainDlg, IDC_DELAYS_SELECT), bEnable && nNumInputFiles != 0);
  EnableWindow(GetDlgItem(hMainDlg, IDC_EDIT_OUTPUT), bEnable);
  EnableWindow(GetDlgItem(hMainDlg, IDC_BROWSE_OUTPUT), bEnable);
  EnableWindow(GetDlgItem(hMainDlg, IDC_BUTTON_MAKE), bEnable && nNumInputFiles != 0 && szOutput[0] != 0);
}

DWORD WINAPI ConvertFunction(LPVOID lpParam)
{
  wchar_t szOutput[MAX_PATH+1];
  FILE_INFO * pInput = ((PFUNC_PARAMS)lpParam)->pInputFiles;
  UINT nNum = ((PFUNC_PARAMS)lpParam)->nNumInputFiles;
  wchar_t * szOut = ((PFUNC_PARAMS)lpParam)->szOutput;
  int deflate_method = ((PFUNC_PARAMS)lpParam)->deflate_method;
  int iter = ((PFUNC_PARAMS)lpParam)->iter;
  int keep_palette = ((PFUNC_PARAMS)lpParam)->keep_palette;
  int keep_coltype = ((PFUNC_PARAMS)lpParam)->keep_coltype;
  int first = ((PFUNC_PARAMS)lpParam)->first;
  int loops = ((PFUNC_PARAMS)lpParam)->loops;

  if (wcschr(szOut, L'\\') == NULL)
    swprintf(szOutput, MAX_PATH, L"%s%s%s", pInput->szDrive, pInput->szDir, szOut);
  else
    wcscpy(szOutput, szOut);

  SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETRANGE, 0, MAKELPARAM(0, 48));
  SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, 0, 0);

  EnableInterface(FALSE);
  DWORD dwRes = CreateAPNG(pInput, nNum, szOutput, deflate_method, iter, keep_palette, keep_coltype, first, loops);
  EnableInterface(TRUE);

  SetDlgItemText(hMainDlg, IDC_PERCENT, (dwRes == 0) ? L"Done" : L"Error");
  return dwRes;
}

LRESULT CALLBACK PlaybackDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
    case WM_INITDIALOG:
      CheckDlgButton(hWnd, IDC_PLAY, loops==0 ? 1 : 0);
      SetDlgItemInt(hWnd, IDC_EDIT_LOOPS, loops, TRUE);
      EnableWindow(GetDlgItem(hWnd, IDC_EDIT_LOOPS), loops != 0);
      CheckDlgButton(hWnd, IDC_SKIP, first);
      return TRUE;
    case WM_CLOSE:
      EndDialog(hWnd, IDCANCEL);
      return TRUE;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDOK:
          if (!IsDlgButtonChecked(hWnd, IDC_PLAY))
          {
            loops = GetDlgItemInt(hWnd, IDC_EDIT_LOOPS, NULL, TRUE);
            if (loops < 0) loops = 0;
          }
          else
            loops = 0;
          first = (IsDlgButtonChecked(hWnd, IDC_SKIP)==BST_CHECKED) ? 1 : 0;
          SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, 0, 0);
          SetDlgItemText(hMainDlg, IDC_PERCENT, L"");
          EndDialog(hWnd, IDOK);
          return TRUE;
        case IDC_PLAY:
          EnableWindow(GetDlgItem(hWnd, IDC_EDIT_LOOPS), !IsDlgButtonChecked(hWnd, IDC_PLAY));
          return TRUE;
        case IDCANCEL:
          EndDialog(hWnd, IDCANCEL);
          return TRUE;
        default:
          return FALSE;
      }
    default:
      return FALSE;
  }
}

LRESULT CALLBACK CompressionDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
    case WM_INITDIALOG:
      CheckDlgButton(hWnd, IDC_RADIO1, deflate_method==0 ? 1 : 0);
      CheckDlgButton(hWnd, IDC_RADIO2, deflate_method==1 ? 1 : 0);
      CheckDlgButton(hWnd, IDC_RADIO3, deflate_method==2 ? 1 : 0);
      SetDlgItemInt(hWnd, IDC_EDIT_I1, iter1, TRUE);
      SetDlgItemInt(hWnd, IDC_EDIT_I2, iter2, TRUE);
      EnableWindow(GetDlgItem(hWnd, IDC_EDIT_I1), deflate_method==1);
      EnableWindow(GetDlgItem(hWnd, IDC_EDIT_I2), deflate_method==2);
      CheckDlgButton(hWnd, IDC_OPT_PAL, 1-keep_palette);
      CheckDlgButton(hWnd, IDC_OPT_COLTYPE, 1-keep_coltype);
      return TRUE;
    case WM_CLOSE:
      EndDialog(hWnd, IDCANCEL);
      return TRUE;
    case WM_COMMAND:
      switch (HIWORD(wParam))
      {
        case BN_CLICKED:
          switch (LOWORD(wParam))
          {
            case IDC_RADIO1:
              EnableWindow(GetDlgItem(hWnd, IDC_EDIT_I1), FALSE);
              EnableWindow(GetDlgItem(hWnd, IDC_EDIT_I2), FALSE);
              return TRUE;
            case IDC_RADIO2:
              EnableWindow(GetDlgItem(hWnd, IDC_EDIT_I1), TRUE);
              EnableWindow(GetDlgItem(hWnd, IDC_EDIT_I2), FALSE);
              return TRUE;
            case IDC_RADIO3:
              EnableWindow(GetDlgItem(hWnd, IDC_EDIT_I1), FALSE);
              EnableWindow(GetDlgItem(hWnd, IDC_EDIT_I2), TRUE);
              return TRUE;
            case IDOK:
              if (IsDlgButtonChecked(hWnd, IDC_RADIO1))
                deflate_method = 0;
              if (IsDlgButtonChecked(hWnd, IDC_RADIO2))
                deflate_method = 1;
              if (IsDlgButtonChecked(hWnd, IDC_RADIO3))
                deflate_method = 2;
              iter1 = GetDlgItemInt(hWnd, IDC_EDIT_I1, NULL, TRUE);
              if (iter1 < 1) iter1 = 1;
              iter2 = GetDlgItemInt(hWnd, IDC_EDIT_I2, NULL, TRUE);
              if (iter2 < 1) iter2 = 1;
              keep_palette = 1-IsDlgButtonChecked(hWnd, IDC_OPT_PAL);
              keep_coltype = 1-IsDlgButtonChecked(hWnd, IDC_OPT_COLTYPE);
              SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, 0, 0);
              SetDlgItemText(hMainDlg, IDC_PERCENT, L"");
              EndDialog(hWnd, IDOK);
              return TRUE;
            case IDCANCEL:
              EndDialog(hWnd, IDCANCEL);
              return TRUE;
            default:
              return FALSE;
          }
        default:
          return FALSE;
      }
    default:
      return FALSE;
  }
}

LRESULT CALLBACK DelaysAllDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  wchar_t szStr[MAX_PATH+1];
  int d1, d2;

  switch (uMsg)
  {
    case WM_INITDIALOG:
      SetDlgItemInt(hWnd, IDC_EDIT_D1, delay_num, TRUE);
      SetDlgItemInt(hWnd, IDC_EDIT_D2, delay_den, TRUE);
      return TRUE;
    case WM_CLOSE:
      EndDialog(hWnd, IDCANCEL);
      SetFocus(GetDlgItem(hMainDlg, IDC_LIST1));
      return TRUE;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDOK:
          d1 = GetDlgItemInt(hWnd, IDC_EDIT_D1, NULL, TRUE);
          d2 = GetDlgItemInt(hWnd, IDC_EDIT_D2, NULL, TRUE);
          if (d1>=0 && d2>=0)
          {
            UINT i;
            FILE_INFO * pFileInfo = pInputFiles;
            HWND hwndLV = GetDlgItem(hMainDlg, IDC_LIST1);
            delay_num = d1;
            delay_den = d2;
            swprintf(szStr, MAX_PATH, L"%d/%d", d1, d2);
            for (i=0; i<nNumInputFiles; ++i, ++pFileInfo)
            {
              LVITEM lvi = {0};
              lvi.iItem = i;
              pFileInfo->delay_num = d1;
              pFileInfo->delay_den = d2;
              ListView_SetItemText(hwndLV, i, 2, szStr);
            }
          }
          SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, 0, 0);
          SetDlgItemText(hMainDlg, IDC_PERCENT, L"");
          EndDialog(hWnd, IDOK);
          SetFocus(GetDlgItem(hMainDlg, IDC_LIST1));
          return TRUE;
        case IDCANCEL:
          EndDialog(hWnd, IDCANCEL);
          SetFocus(GetDlgItem(hMainDlg, IDC_LIST1));
          return TRUE;
        default:
          return FALSE;
      }
    default:
      return FALSE;
  }
}

LRESULT CALLBACK DelaysSelectDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  wchar_t szStr[MAX_PATH+1];
  int d1, d2;

  switch (uMsg)
  {
    case WM_INITDIALOG:
      SetDlgItemInt(hWnd, IDC_EDIT_D1, delay_num, TRUE);
      SetDlgItemInt(hWnd, IDC_EDIT_D2, delay_den, TRUE);
      return TRUE;
    case WM_CLOSE:
      EndDialog(hWnd, IDCANCEL);
      SetFocus(GetDlgItem(hMainDlg, IDC_LIST1));
      return TRUE;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDOK:
          d1 = GetDlgItemInt(hWnd, IDC_EDIT_D1, NULL, TRUE);
          d2 = GetDlgItemInt(hWnd, IDC_EDIT_D2, NULL, TRUE);
          if (d1>=0 && d2>=0)
          {
            UINT i;
            FILE_INFO * pFileInfo = pInputFiles;
            HWND hwndLV = GetDlgItem(hMainDlg, IDC_LIST1);
            delay_num = d1;
            delay_den = d2;
            swprintf(szStr, MAX_PATH, L"%d/%d", d1, d2);
            for (i=0; i<nNumInputFiles; ++i, ++pFileInfo)
            if (ListView_GetItemState(hwndLV, i, LVIS_SELECTED) == LVIS_SELECTED)
            {
              LVITEM lvi = {0};
              lvi.iItem = i;
              pFileInfo->delay_num = d1;
              pFileInfo->delay_den = d2;
              ListView_SetItemText(hwndLV, i, 2, szStr);
            }
          }
          SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, 0, 0);
          SetDlgItemText(hMainDlg, IDC_PERCENT, L"");
          EndDialog(hWnd, IDOK);
          SetFocus(GetDlgItem(hMainDlg, IDC_LIST1));
          return TRUE;
        case IDCANCEL:
          EndDialog(hWnd, IDCANCEL);
          SetFocus(GetDlgItem(hMainDlg, IDC_LIST1));
          return TRUE;
        default:
          return FALSE;
      }
    default:
      return FALSE;
  }
}

int cmp_info( const void *arg1, const void *arg2 )
{
  int res;

  if (res = wcsncmp(((FILE_INFO*)arg1)->szDrive, ((FILE_INFO*)arg2)->szDrive, _MAX_DRIVE))
    return res;

  if (res = wcsncmp(((FILE_INFO*)arg1)->szDir, ((FILE_INFO*)arg2)->szDir, _MAX_DIR))
    return res;

  if (res = wcsncmp(((FILE_INFO*)arg1)->szPrefix, ((FILE_INFO*)arg2)->szPrefix, _MAX_FNAME))
    return res;

  if (res = ((FILE_INFO*)arg1)->num - ((FILE_INFO*)arg2)->num)
    return res;

  return wcsncmp(((FILE_INFO*)arg1)->szExt, ((FILE_INFO*)arg2)->szExt, _MAX_EXT);
}

void DropFiles(HDROP hDrop)
{
  UINT i;
  UINT nCount = 0;
  FILE * f;
  wchar_t  szBuf[MAX_PATH+1];

  if (UINT nNum = DragQueryFile(hDrop, -1, 0, 0))
  {
    pInputFiles = (FILE_INFO *)realloc(pInputFiles, (nNumInputFiles + nNum)*sizeof(FILE_INFO));
    FILE_INFO * pFileInfo = pInputFiles + nNumInputFiles;
    for (i=0; i<nNum; ++i)
    {
      if (DragQueryFile(hDrop, i, 0, 0) <= MAX_PATH)
      {
        if (DragQueryFile(hDrop, i, pFileInfo->szPath, MAX_PATH+1) != 0)
        {
          _wsplitpath(pFileInfo->szPath, pFileInfo->szDrive, pFileInfo->szDir, pFileInfo->szName, pFileInfo->szExt);
          if (!_wcsnicmp(pFileInfo->szExt, L".txt", 4))
            continue;

          int is_png_tga = 0;
          if ((f = _wfopen(pFileInfo->szPath, L"rb")) != 0)
          {
            unsigned char header[24];
            unsigned char png_head[8] = {137, 80, 78, 71, 13, 10, 26, 10};
            if (fread(header, 1, 24, f) == 24)
            {
              if (!memcmp(header, png_head, 8))
                is_png_tga = 1;
              else
              if ((header[2] & 7) == 1 && header[16] == 8 && header[1] == 1 && header[7] == 24)
                is_png_tga = 2;
              else
              if ((header[2] & 7) == 3 && header[16] == 8)
                is_png_tga = 2;
              else
              if ((header[2] & 7) == 2 && header[16] == 24)
                is_png_tga = 2;
              else
              if ((header[2] & 7) == 2 && header[16] == 32)
                is_png_tga = 2;

              if (is_png_tga == 1)
              {
                pFileInfo->w = (((header[16]*256 + header[17])*256) + header[18])*256 + header[19];
                pFileInfo->h = (((header[20]*256 + header[21])*256) + header[22])*256 + header[23];
              }
              else
              if (is_png_tga == 2)
              {
                pFileInfo->w = header[12] + header[13]*256;
                pFileInfo->h = header[14] + header[15]*256;
              }
            }
            fclose(f);
          }
          if (is_png_tga == 0)
            continue;

          pFileInfo->delay_num = delay_num;
          pFileInfo->delay_den = delay_den;
          swprintf(szBuf, MAX_PATH, L"%s%s%s.txt", pFileInfo->szDrive, pFileInfo->szDir, pFileInfo->szName);
          if ((f = _wfopen(szBuf, L"rt")) != 0)
          {
            char szStr[MAX_PATH+1];
            if (fgets(szStr, MAX_PATH, f) != NULL)
            {
              int d1, d2;
              if (sscanf(szStr, "delay=%d/%d", &d1, &d2) == 2)
              {
                if (d1 != 0) pFileInfo->delay_num = d1;
                if (d2 != 0) pFileInfo->delay_den = d2;
              }
            }
            fclose(f);
          }

          size_t end = wcslen(pFileInfo->szName);
          while (end > 0 && pFileInfo->szName[end-1]>=L'0' && pFileInfo->szName[end-1]<=L'9')
            end--;
          pFileInfo->num = _wtoi(pFileInfo->szName + end);
          wcsncpy(pFileInfo->szPrefix, pFileInfo->szName, end);
          pFileInfo->szPrefix[end] = 0;
          pFileInfo++;
          nCount++;
        }
      }
    }

    if (nCount == 0)
    {
      MessageBox(hMainDlg, L"Error: PNG or TGA formats only    ", 0, MB_ICONINFORMATION);
      return;
    }

    qsort(&pInputFiles[nNumInputFiles], nCount, sizeof(FILE_INFO), cmp_info);

    pFileInfo = pInputFiles + nNumInputFiles;
    HWND hwndLV = GetDlgItem(hMainDlg, IDC_LIST1);
    for (i=0; i<nCount; ++i, ++pFileInfo)
    {
      LVITEM lvi = {0};
      lvi.iItem = nNumInputFiles + i;
      ListView_InsertItem(hwndLV, &lvi);
      swprintf(szBuf, MAX_PATH, L"%s%s", pFileInfo->szName, pFileInfo->szExt);
      ListView_SetItemText(hwndLV, nNumInputFiles+i, 0, szBuf);
      swprintf(szBuf, MAX_PATH, L"%dx%d", pFileInfo->w, pFileInfo->h);
      ListView_SetItemText(hwndLV, nNumInputFiles+i, 1, szBuf);
      swprintf(szBuf, MAX_PATH, L"%d/%d", pFileInfo->delay_num, pFileInfo->delay_den);
      ListView_SetItemText(hwndLV, nNumInputFiles+i, 2, szBuf);
    }
    nNumInputFiles += nCount;

    if (szOutput[0] == 0)
    {
      wcscpy(szOutput, L"animated.png");
      SetDlgItemText(hMainDlg, IDC_EDIT_OUTPUT, szOutput);
    }

    SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, 0, 0);
    SetDlgItemText(hMainDlg, IDC_PERCENT, L"");
    EnableWindow(GetDlgItem(hMainDlg, IDC_DELAYS_ALL), TRUE);
    EnableWindow(GetDlgItem(hMainDlg, IDC_DELAYS_SELECT), TRUE);
    EnableWindow(GetDlgItem(hMainDlg, IDC_BUTTON_MAKE), TRUE);
    SetFocus(GetDlgItem(hMainDlg, IDC_BUTTON_MAKE));
  }
}

void BrowseOutput()
{
  OPENFILENAME  ofn;
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(OPENFILENAME);
  ofn.hwndOwner = hMainDlg;
  ofn.hInstance = hInst;
  ofn.lpstrFilter = L"Animated PNG Files (*.png)\0*.png\0";
  ofn.nFilterIndex = 1;
  ofn.lpstrFile = szOutput;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = L"Animated PNG Output";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  ofn.lpstrDefExt = L"png";

  if (GetSaveFileName(&ofn))
  {
    SetDlgItemText(hMainDlg, IDC_EDIT_OUTPUT, szOutput);
    SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, 0, 0);
    SetDlgItemText(hMainDlg, IDC_PERCENT, L"");
    if (nNumInputFiles != 0)
    {
      EnableWindow(GetDlgItem(hMainDlg, IDC_BUTTON_MAKE), TRUE);
      SetFocus(GetDlgItem(hMainDlg, IDC_BUTTON_MAKE));
    }
    else
      SetFocus(GetDlgItem(hMainDlg, IDC_LIST1));
  }
}

void RemoveSelected()
{
  HWND hwndLV = GetDlgItem(hMainDlg, IDC_LIST1);
  int i;
  if (int nCount = ListView_GetItemCount(hwndLV))
  {
    for (i = nCount - 1; i >= 0; --i)
    {
      if (ListView_GetItemState(hwndLV, i, LVIS_SELECTED) == LVIS_SELECTED)
      {
        ListView_DeleteItem(hwndLV, i);
        if (i < (int)(nNumInputFiles - 1))
        {
          FILE_INFO * pDst = pInputFiles + i;
          FILE_INFO * pSrc = pDst + 1;
          for (int j = i; j < (int)(nNumInputFiles - 1); ++j)
            memcpy(pDst++, pSrc++, sizeof(FILE_INFO));
        }
        nNumInputFiles--;
        SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, 0, 0);
        SetDlgItemText(hMainDlg, IDC_PERCENT, L"");
      }
    }
    for (i = 0; i < ListView_GetItemCount(hwndLV); ++i)
    {
      if (ListView_GetItemState(hwndLV, i, LVIS_FOCUSED))
      {
        ListView_SetItemState(hwndLV, i, LVIS_SELECTED, LVIS_SELECTED);
        break;
      }
    }
    if (!nNumInputFiles)
    {
      EnableWindow(GetDlgItem(hMainDlg, IDC_DELAYS_ALL), FALSE);
      EnableWindow(GetDlgItem(hMainDlg, IDC_DELAYS_SELECT), FALSE);
      EnableWindow(GetDlgItem(hMainDlg, IDC_BUTTON_MAKE), FALSE);
    }
  }
}

LRESULT CALLBACK MainDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
    case WM_INITDIALOG:
      hMainDlg = hWnd;
      SendMessage(hMainDlg, WM_SETICON, ICON_BIG, (LPARAM)hIconMain);
      nNumInputFiles = 0;
      pInputFiles = NULL;
      first = 0;
      loops = 0;
      deflate_method = 1;
      iter1 = iter2 = 15;
      keep_palette = 0;
      keep_coltype = 0;
      delay_num = 1;
      delay_den = 10;
      DragAcceptFiles(hMainDlg, TRUE);
      {
        HMODULE hModule = GetModuleHandle(TEXT("user32.dll"));
        if (hModule != NULL)
        {
          typedef BOOL (WINAPI *PFN)(HWND, UINT, DWORD, PCHANGEFILTERSTRUCT);
          PFN pfnChangeWindowMessageFilterEx = (PFN)GetProcAddress(hModule, "ChangeWindowMessageFilterEx");
          if (pfnChangeWindowMessageFilterEx != NULL)
          {
            (*pfnChangeWindowMessageFilterEx)(hMainDlg, WM_DROPFILES, MSGFLT_ALLOW, NULL);
            (*pfnChangeWindowMessageFilterEx)(hMainDlg, WM_COPYDATA,  MSGFLT_ALLOW, NULL);
            (*pfnChangeWindowMessageFilterEx)(hMainDlg, 0x0049,       MSGFLT_ALLOW, NULL);
          }
        }
        HWND hwndLV = GetDlgItem(hMainDlg, IDC_LIST1);
        ListView_SetExtendedListViewStyle(hwndLV, LVS_EX_FULLROWSELECT);
        LVCOLUMN lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_SUBITEM | LVCF_WIDTH | LVCF_FMT;
        lvc.fmt  = LVCFMT_LEFT;
        lvc.iSubItem = 0;
        lvc.cx       = 248;
        lvc.pszText  = TEXT("Frame");
        ListView_InsertColumn(hwndLV, 0, &lvc);
        lvc.iSubItem = 1;
        lvc.cx       = 78;
        lvc.pszText  = TEXT("Size");
        ListView_InsertColumn(hwndLV, 1, &lvc);
        lvc.iSubItem = 1;
        lvc.cx       = 78;
        lvc.pszText  = TEXT("Delay, sec");
        ListView_InsertColumn(hwndLV, 2, &lvc);
      }
      SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETRANGE, 0, MAKELPARAM(0, 48));
      SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, 0, 0);
      SetDlgItemText(hMainDlg, IDC_PERCENT, L"");
      if (szOutput[0] != 0)
        SetDlgItemText(hMainDlg, IDC_EDIT_OUTPUT, szOutput);
      EnableWindow(GetDlgItem(hMainDlg, IDC_DELAYS_ALL), FALSE);
      EnableWindow(GetDlgItem(hMainDlg, IDC_DELAYS_SELECT), FALSE);
      EnableWindow(GetDlgItem(hMainDlg, IDC_BUTTON_MAKE), FALSE);
      SetFocus(GetDlgItem(hMainDlg, IDC_LIST1));
      return TRUE;
    case WM_CLOSE:
      DragAcceptFiles(hMainDlg, FALSE);
      EndDialog(hMainDlg, IDCANCEL);
      PostQuitMessage(0);
      return TRUE;
    case WM_COMMAND:
      switch (HIWORD(wParam))
      {
        case BN_CLICKED:
          switch (LOWORD(wParam))
          {
            case IDC_BROWSE_OUTPUT:
              BrowseOutput();
              return TRUE;
            case IDC_BUTTON_MAKE:
              if (nNumInputFiles != 0 && szOutput[0] != 0)
              {
                pParams->pInputFiles = pInputFiles;
                pParams->nNumInputFiles = nNumInputFiles;
                pParams->szOutput = szOutput;
                pParams->deflate_method = deflate_method;
                pParams->iter = deflate_method==1 ? iter1 : iter2;
                pParams->keep_palette = keep_palette;
                pParams->keep_coltype = keep_coltype;
                pParams->first = first;
                pParams->loops = loops;
                hConvertThread = CreateThread(NULL, 0, ConvertFunction, pParams, 0, &dwThreadId);
              }
              return TRUE;
            case IDC_PLAYBACK:
              EnableInterface(FALSE);
              DialogBox(hInst, MAKEINTRESOURCE(IDD_PLAYBACK), NULL, (DLGPROC)PlaybackDlgProc);
              EnableInterface(TRUE);
              return TRUE;
            case IDC_COMPRESSION:
              EnableInterface(FALSE);
              DialogBox(hInst, MAKEINTRESOURCE(IDD_COMPRESSION), NULL, (DLGPROC)CompressionDlgProc);
              EnableInterface(TRUE);
              return TRUE;
            case IDC_DELAYS_ALL:
              EnableInterface(FALSE);
              DialogBox(hInst, MAKEINTRESOURCE(IDD_DELAYS_ALL), NULL, (DLGPROC)DelaysAllDlgProc);
              EnableInterface(TRUE);
              SetFocus(GetDlgItem(hMainDlg, IDC_LIST1));
              return TRUE;
            case IDC_DELAYS_SELECT:
              EnableInterface(FALSE);
              DialogBox(hInst, MAKEINTRESOURCE(IDD_DELAYS_SELECT), NULL, (DLGPROC)DelaysSelectDlgProc);
              EnableInterface(TRUE);
              SetFocus(GetDlgItem(hMainDlg, IDC_LIST1));
              return TRUE;
            case IDCANCEL:
              DragAcceptFiles(hMainDlg, FALSE);
              EndDialog(hMainDlg, IDCANCEL);
              PostQuitMessage(0);
              return TRUE;
            default:
              return FALSE;
          }
        case EN_CHANGE:
          switch (LOWORD(wParam))
          {
            case IDC_EDIT_OUTPUT:
              GetDlgItemText(hMainDlg, IDC_EDIT_OUTPUT, szOutput, MAX_PATH);
              EnableWindow(GetDlgItem(hMainDlg, IDC_BUTTON_MAKE), nNumInputFiles != 0 && szOutput[0] != 0);
              SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, 0, 0);
              SetDlgItemText(hMainDlg, IDC_PERCENT, L"");
              return TRUE;
            default:
              return FALSE;
          }
        default:
          return FALSE;
      }
    case WM_DROPFILES:
      DropFiles((HDROP)wParam);
      return TRUE;
    case WM_NOTIFY:
      {
        NMHDR* pnmh = (NMHDR*)lParam;
        if (pnmh->code == LVN_KEYDOWN)
        {
          NMLVKEYDOWN* pnm = (NMLVKEYDOWN*)pnmh;
          if (pnm->wVKey == VK_DELETE)
            RemoveSelected();
          else
          if (pnm->wVKey == 0x41 && GetKeyState(VK_CONTROL)<0 && GetKeyState(VK_MENU)>=0 && GetKeyState(VK_SHIFT)>=0)
          {
            HWND hwndLV = GetDlgItem(hMainDlg, IDC_LIST1);
            for (int i = 0; i < ListView_GetItemCount(hwndLV); ++i)
              ListView_SetItemState(hwndLV, i, LVIS_SELECTED, LVIS_SELECTED);
          }
          return TRUE;
        }
        else
        if (pnmh->code == NM_RCLICK && pnmh->hwndFrom == GetDlgItem(hMainDlg, IDC_LIST1))
        {
          int nCount = 0;
          HWND hwndLV = GetDlgItem(hMainDlg, IDC_LIST1);
          for (int i = 0; i < ListView_GetItemCount(hwndLV); ++i)
          {
            if (ListView_GetItemState(hwndLV, i, LVIS_SELECTED) == LVIS_SELECTED)
              nCount++;
          }
          if (nCount > 0)
          {
            if (HMENU hMenu = CreatePopupMenu())
            {
              POINT pt;
              AppendMenu(hMenu, MF_STRING,    1, (nCount > 1) ? L"Delete Frames" : L"Delete Frame");
              AppendMenu(hMenu, MF_SEPARATOR, 2, L"");
              AppendMenu(hMenu, MF_STRING,    3, L"Set Delay");
              GetCursorPos(&pt);
              int sel = TrackPopupMenuEx(hMenu, TPM_RETURNCMD, pt.x, pt.y, pnmh->hwndFrom, NULL);
              if (sel == 1)
                RemoveSelected();
              else
              if (sel == 3)
              {
                EnableInterface(FALSE);
                DialogBox(hInst, MAKEINTRESOURCE(IDD_DELAYS_SELECT), NULL, (DLGPROC)DelaysSelectDlgProc);
                EnableInterface(TRUE);
                SetFocus(hwndLV);
              }
            }
          }
          return TRUE;
        }
        else
        if (pnmh->code == NM_DBLCLK && pnmh->hwndFrom == GetDlgItem(hMainDlg, IDC_LIST1))
        {
          int nCount = 0;
          HWND hwndLV = GetDlgItem(hMainDlg, IDC_LIST1);
          for (int i = 0; i < ListView_GetItemCount(hwndLV); ++i)
          {
            if (ListView_GetItemState(hwndLV, i, LVIS_SELECTED) == LVIS_SELECTED)
              nCount++;
          }
          if (nCount > 0)
          {
            EnableInterface(FALSE);
            DialogBox(hInst, MAKEINTRESOURCE(IDD_DELAYS_SELECT), NULL, (DLGPROC)DelaysSelectDlgProc);
            EnableInterface(TRUE);
            SetFocus(hwndLV);
          }
          return TRUE;
        }
        return FALSE;
      }
    default:
      return FALSE;
  }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
  INITCOMMONCONTROLSEX icce;
  icce.dwSize = sizeof(icce);
  icce.dwICC = ICC_PROGRESS_CLASS;
  InitCommonControlsEx(&icce);
  hInst = hInstance;
  hIconMain = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN));

  LPWSTR * szArglist;
  int nArgs;

  szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
  if (szArglist != NULL)
  {
    wcscpy(szOutput, (nArgs > 1) ? szArglist[1] : L"");
    LocalFree(szArglist);
  }

  pParams = (PFUNC_PARAMS)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FUNC_PARAMS));
  if (pParams != NULL)
  {
    DialogBox(hInstance, MAKEINTRESOURCE(IDD_MAIN), NULL, (DLGPROC)MainDlgProc);
    CloseHandle(hConvertThread);
    HeapFree(GetProcessHeap(), 0, pParams);
    if (pInputFiles)
      free(pInputFiles);
  }

  return FALSE;
}
