/* Copyright (c) 2002-2012 Croteam Ltd. 
This program is free software; you can redistribute it and/or modify
it under the terms of version 2 of the GNU General Public License as published by
the Free Software Foundation


This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA. */

#include "StdAfx.h"

// console variables
static FLOAT fConsoleFadeValue = 0.0f; // faded value of console (0..1)
static CTimerValue tvConsoleLast;

static CTString strConsole;
static CTString strInputHistory = "\n";
static CTString strEditingLine;
static CTString strExpandStart;
static CTString strLastExpanded;
static CTString strCurrentLine; 
static BOOL  bLastExpandedFound;
static INDEX iSymbolOffset;
static INDEX iHistoryLine=0;
static INDEX iCursorPos=0;
static INDEX ctConsoleLinesOnScreen;

static INDEX con_iFirstLine = 1;
extern FLOAT con_fHeightFactor;
extern FLOAT con_tmLastLines;

// [Cecil] Customizable fade time
FLOAT con_tmConsoleFade = 0.5f;

// [Cecil] Use bigger font in console (0 - never; 1 - only last lines; 2 - always)
INDEX con_iBigFont = 0;
FLOAT con_fBigFontScale = 1.0f;

// [Cecil] Screen height
static PIX _pixSizeJ = 0;

// [Cecil] Current console font
static CFontData *_pfdCurrentFont = _pfdConsoleFont;

// [Cecil] Check if should use big font
static inline BOOL UseBigFont(BOOL bLastLines) {
  // Always on or only for last lines
  return con_iBigFont > 1 || (con_iBigFont == 1 && bLastLines);
};

// [Cecil] Set console font
static void SetConsoleFont(CDrawPort *pdp, BOOL bLastLines) {
  const BOOL bBig = UseBigFont(bLastLines);

  _pfdCurrentFont = (bBig ? _pfdDisplayFont : _pfdConsoleFont);
  pdp->SetFont(_pfdCurrentFont);

  if (bBig) {
    FLOAT fScaling = ClampDn((FLOAT)_pixSizeJ / 900.0f, 0.8f) * Clamp(con_fBigFontScale, 0.1f, 10.0f);

    if (bLastLines) {
      _pfdDisplayFont->SetVariableWidth();
      pdp->SetTextCharSpacing(2.0f * fScaling);

    } else {
      _pfdDisplayFont->SetFixedWidth();
      pdp->SetTextCharSpacing(-4.0f * fScaling);
    }

    pdp->SetTextScaling(fScaling);
  }
};

// [Cecil] Reset console font
static void ResetConsoleFont(void) {
  _pfdDisplayFont->SetVariableWidth();
};

// find a line with given number in a multi-line string counting from end of string
BOOL GetLineCountBackward(const char *pchrStringStart, const char *pchrStringEnd, 
                          INDEX iBackwardLine, CTString &strResult)
{
  // start at the end of string
  const char *pchrCurrent = pchrStringEnd;
  INDEX ctLinesDone=0;

  // while line of given number is not found
  while(ctLinesDone!=iBackwardLine) {
    // go one character back
    pchrCurrent--;
    // if got before start of string
    if (pchrCurrent < pchrStringStart) {
      // line is not found
      return FALSE;
    }
    // if at line break
    if(*pchrCurrent == '\n') {
      // count lines
      ctLinesDone++;
    }
  }
  // check that pointer is not outside range
  ASSERT(pchrCurrent>=pchrStringStart && pchrCurrent<pchrStringEnd);

  // if exactly on line break, skip it
  if (*pchrCurrent=='\n') {
    pchrCurrent++;
  }

  // find end of that line
  const char *pchrLineEnd=strchr(pchrCurrent, '\r');
  if (pchrLineEnd==NULL) {
    pchrLineEnd = pchrStringEnd;
  }

  // copy the found line
  char achrLine[ 1024];
  strncpy( achrLine, pchrCurrent, pchrLineEnd-pchrCurrent);
  achrLine[ pchrLineEnd-pchrCurrent]=0;
  strResult = achrLine;
  return TRUE;
}

void CGame::ConsoleRender(CDrawPort *pdp)
{
  // [Cecil] Abort console rendering if it's fully closed
  if (_pGame->gm_csConsoleState == CS_OFF && fConsoleFadeValue < 0.001f) {
    con_iFirstLine = 1;
    tvConsoleLast  = _pTimer->GetHighPrecisionTimer();
    return;
  }

  // get console height
  con_fHeightFactor = Clamp( con_fHeightFactor, 0.1f, 1.0f);
  FLOAT fHeightFactor = con_fHeightFactor;
  if( !gm_bGameOn) fHeightFactor = 0.9f;

  // calculate up-down speed to be independent of refresh speed
  CTimerValue tvNow   = _pTimer->GetHighPrecisionTimer();
  CTimerValue tvDelta = tvNow - tvConsoleLast;
  tvConsoleLast       = tvNow;
  FLOAT fFadeSpeed    = (FLOAT)(tvDelta.GetSeconds() / con_tmConsoleFade);

  // [Cecil] Force console to be gradually turned on and off
  if (_pGame->gm_csConsoleState == CS_ON && fConsoleFadeValue <= 0.999f) {
    _pGame->gm_csConsoleState = CS_TURNINGON;
  }

  if (_pGame->gm_csConsoleState == CS_OFF && fConsoleFadeValue >= 0.001f) {
    _pGame->gm_csConsoleState = CS_TURNINGOFF;
  }

  // if console is dropping down
  if( _pGame->gm_csConsoleState==CS_TURNINGON) {
    // move it down
    fConsoleFadeValue += fFadeSpeed;
    // if finished moving
    if( fConsoleFadeValue>1.0f) {
      // stop
      fConsoleFadeValue = 1.0f;
      _pGame->gm_csConsoleState = CS_ON;
    }
  }
  // if console is pulling up
  if( _pGame->gm_csConsoleState==CS_TURNINGOFF) {
    // move it up
    fConsoleFadeValue -= fFadeSpeed;
    // if finished moving
    if( fConsoleFadeValue<0.0f) {
      // stop
      fConsoleFadeValue = 0.0f;
      _pGame->gm_csConsoleState = CS_OFF;
      // if not in network
      if (!_pNetwork->IsNetworkEnabled()) {
        // don't show last lines on screen after exiting console
        CON_DiscardLastLineTimes();
      }
      return;
    }
  }

  if (_pGame->gm_csConsoleState==CS_TALK) {
    fHeightFactor = 0.1f;
    fConsoleFadeValue = 1.0f;
  }

  // [Cecil] Get screen height
  _pixSizeJ = pdp->GetHeight();

  // calculate size of console box so that it covers upper half of the screen
  FLOAT fHeight = ClampUp( fHeightFactor*fConsoleFadeValue*2, fHeightFactor);
  CDrawPort dpConsole( pdp, 0.0f, 0.0f, 1.0f, fHeight);
  // lock drawport
  if( !dpConsole.Lock()) return;

  LCDPrepare(fConsoleFadeValue);
  LCDSetDrawport(&dpConsole);
  dpConsole.Fill(LCDFadedColor(C_BLACK|225));

  PIX pixSizeI = dpConsole.GetWidth();
  PIX pixSizeJ = dpConsole.GetHeight();
  COLOR colLight = LCDFadedColor(C_WHITE|255);
  COLOR colDark  = LCDFadedColor(CECIL_COL_CONTEXT|255); // [Cecil] New separate color
  INDEX iBackwardLine = con_iFirstLine;
  if( iBackwardLine>1) Swap( colLight, colDark);

  // [Cecil] Set console font
  SetConsoleFont(&dpConsole, FALSE);

  // [Cecil] Multiply by text scaling
  const PIX pixLineSpacing = (_pfdCurrentFont->GetHeight() + _pfdCurrentFont->GetLineSpacing()) * dpConsole.dp_fTextScaling;

  // [Cecil] Enable clouds in console
  if (_gmtTheme.bConClouds) {
    LCDRenderCloudsForComp();
  }

  LCDRenderGrid();

  // [Cecil] Enable clouds in console
  if (_gmtTheme.bConClouds) {
    LCDRenderClouds2();
  }

  // [Cecil] New separate color
  dpConsole.DrawLine( 0, pixSizeJ-1, pixSizeI, pixSizeJ-1, LCDFadedColor(CECIL_COL_CONBORDER|255));

  // [Cecil] Static 7 pixels instead of a 1.6 multiplier
  const PIX pixEditHeight = (pixLineSpacing + 7);

  const COLOR colFill = (colDark & ~CT_AMASK) | 0x2F;
  dpConsole.Fill(0, pixSizeJ - pixEditHeight, pixSizeI, pixEditHeight, colFill);

  // setup font
  PIX pixTextX = (PIX)(dpConsole.GetWidth()*0.01f);

  // [Cecil] Static 14 pixels are replaced with line spacing + 3 scaled pixels
  PIX pixYLine = dpConsole.GetHeight() - (pixLineSpacing + 3.0f / dpConsole.dp_fTextScaling);

  // print editing line of text
  dpConsole.SetTextMode(-1);
  CTString strPrompt;
  if (_pGame->gm_csConsoleState == CS_TALK) {
    strPrompt = LOCALIZE("say: ");
  } else {
    strPrompt = "=> ";
  }
  CTString strLineOnScreen = strPrompt + strEditingLine;
  dpConsole.PutText( strLineOnScreen, pixTextX, pixYLine, colLight);
  dpConsole.SetTextMode(+1);

  // add blinking cursor
  if( ((ULONG)(_pTimer->GetRealTimeTick()*2)) & 1) {
    // [Cecil] Different cursor for big font
    const BOOL bBigFont = UseBigFont(FALSE);

    CTString strCursor = (bBigFont ? "|" : "_");
    const PIX pixCursorShift = (bBigFont ? dpConsole.dp_fTextScaling * -2 : 2);

    FLOAT fTextScalingX = dpConsole.dp_fTextScaling * dpConsole.dp_fTextAspect;
    PIX pixCellSize = _pfdCurrentFont->fd_pixCharWidth * fTextScalingX + dpConsole.dp_pixTextCharSpacing;
    PIX pixCursorX  = pixTextX + (iCursorPos+strlen(strPrompt))*pixCellSize;

    // [Cecil] Fix '|' cursor being in the middle of a letter instead of to the left of it
    if (bBigFont) pixCursorX -= pixCellSize / 2;

    dpConsole.PutText(strCursor, pixCursorX, pixYLine + pixCursorShift, colDark);
  }

  // render previous outputs
  con_iFirstLine = ClampDn( con_iFirstLine, 1L);

  // [Cecil] Static 3 pixels instead of a 1.333 multiplier
  pixYLine -= (pixLineSpacing + 3);
  ctConsoleLinesOnScreen = pixYLine/pixLineSpacing;

  while (pixYLine >= 0) {
    CTString strLineOnScreen = CON_GetLastLine(iBackwardLine);
    dpConsole.PutText( strLineOnScreen, pixTextX, pixYLine, colDark);
    iBackwardLine++;
    pixYLine -= pixLineSpacing;
  }

  // [Cecil] Reset console font
  ResetConsoleFont();

  // all done
  dpConsole.Unlock();
}


// print last few lines from console to top of screen
void CGame::ConsolePrintLastLines(CDrawPort *pdp)
{
  // get number of lines to print
  con_tmLastLines = Clamp( con_tmLastLines, 1.0f, 10.0f);
  INDEX ctLines   = CON_NumberOfLinesAfter( _pTimer->GetRealTimeTick() - con_tmLastLines);
  // if no lines left to print, just skip it
  if( ctLines==0) return;

  // [Cecil] Get screen height
  _pixSizeJ = pdp->GetHeight();

  // [Cecil] Set console font
  SetConsoleFont(pdp, TRUE);

  // [Cecil] Multiply by text scaling
  const FLOAT fScaling = pdp->dp_fTextScaling;
  const PIX pixCharHeight = (_pfdCurrentFont->GetHeight() - 1) * fScaling;

  // put some filter underneath for easier reading
  pdp->Fill( 0, 0, pdp->GetWidth(), pixCharHeight*ctLines, C_BLACK|128);
  // for each line
  for( INDEX iLine=0; iLine<ctLines; iLine++) {
    CTString strLine = CON_GetLastLine(iLine+1);

    // [Cecil] Adjust line position for big font
    PIX pixLineX = (UseBigFont(TRUE) ? fScaling * 4 : 0);
    PIX pixLineY = (UseBigFont(TRUE) ? fScaling * 1 : 0) + pixCharHeight * (ctLines - iLine - 1);
    pdp->PutText(strLine, pixLineX, pixLineY, CECIL_COL_CONTEXT|255); // [Cecil] New separate color
  }

  // [Cecil] Reset console font
  ResetConsoleFont();
}


static void Key_Backspace( BOOL bShift, BOOL bRight)
{
  // do nothing if string is empty
  INDEX ctChars = strlen(strEditingLine);
  if( ctChars==0) return;

  if( bRight && iCursorPos<ctChars) {   // DELETE key
    if( bShift) {  // delete to end of line
      strEditingLine.TrimRight(iCursorPos);
    } else {  // delete only one char
      strEditingLine.DeleteChar(iCursorPos);
    }
  }
  if( !bRight && iCursorPos>0) {  // BACKSPACE key
    if( bShift) {  // delete to start of line
      strEditingLine.TrimLeft(ctChars-iCursorPos);
      iCursorPos=0;
    } else {  // delete only one char
      strEditingLine.DeleteChar(iCursorPos-1);
      iCursorPos--;
    }
  }
}


static void Key_ArrowUp(void)
{
  CTString strSlash = "/";
  CTString strHistoryLine;
  if( iHistoryLine==0) strCurrentLine = strEditingLine;
  INDEX iCurrentHistoryLine = iHistoryLine;
  do {
    // determine previous line in history
    iCurrentHistoryLine++;
    const char *pchrHistoryStart = (const char*)strInputHistory;
    const char *pchrHistoryEnd   = pchrHistoryStart +strlen( strInputHistory) -1;
    // we reach top of history, if line doesn't exist in history
    if( !GetLineCountBackward( pchrHistoryStart, pchrHistoryEnd, iCurrentHistoryLine, strHistoryLine)) return;
  } while( strCurrentLine!="" &&
           strnicmp( strHistoryLine,          strCurrentLine, Min(strlen(strHistoryLine), strlen(strCurrentLine)))!=0 &&
           strnicmp( strHistoryLine, strSlash+strCurrentLine, Min(strlen(strHistoryLine), strlen(strCurrentLine)+1))!=0);
  // set new editing line
  iHistoryLine   = iCurrentHistoryLine;
  strEditingLine = strHistoryLine;
  iCursorPos = strlen(strEditingLine);
}


static void Key_ArrowDown(void)
{
  CTString strSlash = "/";
  CTString strHistoryLine;
  if( iHistoryLine==0) strCurrentLine = strEditingLine;
  INDEX iCurrentHistoryLine = iHistoryLine;
  while( iCurrentHistoryLine>1) {
    iCurrentHistoryLine--;
    const char *pchrHistoryStart = (const char *) strInputHistory;
    const char *pchrHistoryEnd   = pchrHistoryStart +strlen(strInputHistory) -1;
    // line must exist in history
    BOOL bExists = GetLineCountBackward( pchrHistoryStart, pchrHistoryEnd, iCurrentHistoryLine, strHistoryLine);
    ASSERT( bExists);
    // set new editing line
    if( strCurrentLine=="" ||
        strnicmp( strHistoryLine,          strCurrentLine, Min(strlen(strHistoryLine), strlen(strCurrentLine)))  ==0 ||
        strnicmp( strHistoryLine, strSlash+strCurrentLine, Min(strlen(strHistoryLine), strlen(strCurrentLine)+1))==0) {
      iHistoryLine   = iCurrentHistoryLine;
      strEditingLine = strHistoryLine;
      iCursorPos = strlen(strEditingLine);
      return;
    }
  }
}

void DoCheat(const CTString &strCommand, const CTString &strVar)
{
  _pShell->SetINDEX(strVar, !_pShell->GetINDEX(strVar));
  BOOL bNew = _pShell->GetINDEX(strVar);
  CPrintF("%s: %s\n", strCommand, bNew?"ON":"OFF");
}

static void Key_Return(void)
{
  // ignore keydown return (keyup will be handled) - because of key bind to return
  //if( _pGame->gm_csConsoleState==CS_TALK) return;

  // clear editing line from whitespaces
  strEditingLine.TrimSpacesLeft();
  strEditingLine.TrimSpacesRight();
  // reset display position
  con_iFirstLine = 1;

  // ignore empty lines
  if( strEditingLine=="" || strEditingLine=="/") {
    strEditingLine = "";
    iCursorPos = 0;
    return;
  }

  // add to history
  strInputHistory += strEditingLine +"\r\n";
  iHistoryLine = 0;

  // [Cecil] TEMP: Run Squirrel script from the console
  if (strEditingLine.RemovePrefix("sq>")) {
    strEditingLine.TrimSpacesLeft();
    CPrintF("sq> %s\n", strEditingLine);

    static HPatchPlugin hScripting = ClassicsExtensions_GetExtensionByName("PATCH_EXT_scripting");
    bool bExecuted;

    // Resume the previous script on an empty line
    if (strEditingLine == "") {
      bExecuted = ClassicsExtensions_CallSignal(hScripting, "Resume", NULL, NULL);

    // Run a new script
    } else {
      bExecuted = ClassicsExtensions_CallSignal(hScripting, "ExecuteScript", NULL, &strEditingLine);
    }

    if (!bExecuted) CPutString(TRANS("Squirrel scripts are unavailable!\n"));

  // check for cheats
  #define CHEAT_PREFIX "please"
  } else if (strEditingLine.HasPrefix(CHEAT_PREFIX) || strEditingLine.HasPrefix("/" CHEAT_PREFIX)) {
    strEditingLine.RemovePrefix(CHEAT_PREFIX);
    strEditingLine.RemovePrefix("/ " CHEAT_PREFIX);
    strEditingLine.TrimSpacesLeft();
    if (strEditingLine=="god") {
      DoCheat(strEditingLine, "cht_bGod");
    
    } else if (strEditingLine=="giveall") {
      DoCheat(strEditingLine, "cht_bGiveAll");
    
    } else if (strEditingLine=="killall") {
      DoCheat(strEditingLine, "cht_bKillAll");
    
    } else if (strEditingLine=="open") {
      DoCheat(strEditingLine, "cht_bOpen");
    
    } else if (strEditingLine=="tellall") {
      DoCheat(strEditingLine, "cht_bAllMessages");
    
    } else if (strEditingLine=="fly") {
      DoCheat(strEditingLine, "cht_bFly");

    } else if (strEditingLine=="ghost") {
      DoCheat(strEditingLine, "cht_bGhost");

    } else if (strEditingLine=="invisible") {
      DoCheat(strEditingLine, "cht_bInvisible");

    } else if (strEditingLine=="refresh") {
      DoCheat(strEditingLine, "cht_bRefresh");
    } else {
      CPrintF("sorry?\n");
    }
  // parse editing line
  } else if( strEditingLine[0]=='/') {
    // add to output and execute
    CPrintF( "-> %s\n", strEditingLine);
      strEditingLine+=";";
      _pShell->Execute(strEditingLine+1);
  } 
  else if( !_pGame->gm_bGameOn) {
    // add to output and execute
    CPrintF( "-> %s\n", strEditingLine);
    strEditingLine+=";";
    _pShell->Execute(strEditingLine);
  }
  else {
    // just send chat
    _pNetwork->SendChat(-1, -1, strEditingLine);
  }

  // reset editing line
  strEditingLine = "";
  iCursorPos = 0;
}


// find first character that is not part of a symbol, backwards
char *strrnonsym(const char *strString)
{
  const char *pch = strString+strlen(strString)-1;
  while( pch>=strString) {
    char ch = *pch;
    if( !isalnum(ch) && ch!='_') return (char*)pch;
    pch--;
  }
  return NULL;
}


static void Key_Tab( BOOL bShift)
{
  // clear editing line from whitespaces
  strEditingLine.TrimSpacesLeft();
  strEditingLine.TrimSpacesRight();
  // eventualy prepend the command like with '/'
  if (strEditingLine[0]!='/') strEditingLine = CTString("/")+strEditingLine;

  // find symbol letter typed so far
  CTString strSymbol;
  CTString strLastThatCanBeExpanded;
  if( strLastExpanded == "") {
    strExpandStart = strEditingLine; 
    iSymbolOffset = 0;
    char *pcLastSymbol = strrnonsym( strEditingLine);
    // remember symbol text and offset inside editing line
    if( pcLastSymbol!=NULL) {
      strExpandStart = pcLastSymbol+1; 
      iSymbolOffset  = (INDEX)(pcLastSymbol+1 - (const char*)strEditingLine);
    }
    // printout all symbols that matches (if not only one, and not TAB only)
    INDEX ctSymbolsFound=0;
    BOOL  bFirstFound = FALSE;
    CTString strLastMatched;
    {FOREACHINDYNAMICARRAY( _pShell->sh_assSymbols, CShellSymbol, itss) {
      // TAB only pressd?
      if( strExpandStart=="") break;
      // get completion name if current symbol is for user
      if( !(itss->ss_ulFlags&SSF_USER)) continue;
      strSymbol = itss->GetCompletionString();
      // if this symbol can be expanded
      if( strnicmp( strSymbol, strExpandStart, Min(strlen(strSymbol),strlen(strExpandStart))) == 0) {
        // can we print last found symbol ?
        if( strLastMatched!="") {
          if( !bFirstFound) CPrintF( "  -\n");
          CPrintF( "  %s\n", strLastMatched);
          bFirstFound = TRUE;
        }
        strLastMatched = strSymbol;
        ctSymbolsFound++;
      }
    }}
    // print last symbol
    if( ctSymbolsFound>1) CPrintF( "  %s\n", strLastMatched);
  }

  // for each of symbols in the shell
  bLastExpandedFound   = FALSE;
  BOOL bTabSymbolFound = FALSE;
  {FOREACHINDYNAMICARRAY( _pShell->sh_assSymbols, CShellSymbol, itss)
  {
    // skip if it is not visible to user
    if( !(itss->ss_ulFlags&SSF_USER)) continue;
    // get completion name for that symbol
    strSymbol = itss->GetCompletionString();

    // if this symbol can be expanded
    if( strnicmp( strSymbol, strExpandStart, Min(strlen(strSymbol),strlen(strExpandStart))) == 0)
    {
      // at least one symbol is found, so tab will work
      bTabSymbolFound = TRUE;
      // if this is first time we are doing this
      if( strLastExpanded == "") {
        // remember symbol as last expanded and set it as current
        strLastExpanded = strSymbol;
        break;
      }
      // if last expanded was already found, set this symbol as result and remember it as last expanded
      if( bLastExpandedFound) {
        strLastExpanded = strSymbol;
        break;
      } 
      // if last expanded was not found yet, check if this one is last expanded
      if( stricmp( strLastExpanded, strSymbol) == 0) {
        // if looping backward (Shift+Tab)
        if( bShift) {
          // if we can loop backwards
          if( strLastThatCanBeExpanded != "") {
            strLastExpanded = strLastThatCanBeExpanded;
            break;
          // act like no symbols are found
          } else {
            bTabSymbolFound = FALSE;
            break;
          }
        }
        // if so, mark it
        bLastExpandedFound = TRUE;
      }
      // remember current as last that can be expanded (for loopbing back)
      strLastThatCanBeExpanded = strSymbol;
    }
  }}
  // if symbol was found
  if( bTabSymbolFound) {
    // set it in current editing line
    *((char*)(const char*)strEditingLine +iSymbolOffset) = '\0';
    strEditingLine += strLastExpanded;
  }
  iCursorPos = strlen(strEditingLine);
}


static void Key_PgUp( BOOL bShift)
{
  if( bShift) con_iFirstLine += ctConsoleLinesOnScreen;
  else con_iFirstLine++;
}


static void Key_PgDn( BOOL bShift)
{
  if( bShift) con_iFirstLine -= ctConsoleLinesOnScreen;
  else con_iFirstLine--;
  con_iFirstLine = ClampDn( con_iFirstLine, 1L);
}


void CGame::ConsoleKeyDown( MSG msg)
{
  // if console is off
  if (_pGame->gm_csConsoleState==CS_OFF || _pGame->gm_csConsoleState==CS_TURNINGOFF) {
    // do nothing
    return;
  }
  BOOL bShift = GetKeyState(VK_SHIFT) & 0x8000;
  switch( msg.wParam) {
  case VK_RETURN:  Key_Return();      break;
  case VK_UP:      Key_ArrowUp();     break;
  case VK_DOWN:    Key_ArrowDown();   break;
  case VK_TAB:     Key_Tab(bShift);   break;
  case VK_PRIOR:   Key_PgUp(bShift);  break;
  case VK_NEXT:    Key_PgDn(bShift);  break;
  case VK_BACK:    Key_Backspace(bShift, FALSE);  break;
  case VK_DELETE:  Key_Backspace(bShift, TRUE);   break;
  case VK_LEFT:    if( iCursorPos > 0)                      iCursorPos--;  break;
  case VK_RIGHT:   if( iCursorPos < strlen(strEditingLine)) iCursorPos++;  break;
  case VK_HOME:    iCursorPos = 0;                       break;
  case VK_END:     iCursorPos = strlen(strEditingLine);  break;
  }
}


void CGame::ConsoleChar( MSG msg)
{
  // if console is off, do nothing
  if (_pGame->gm_csConsoleState==CS_OFF) return;

  // for all keys except tab and shift, discard last found tab browsing symbol
  UBYTE chrKey = msg.wParam; // [Cecil] Unsigned because 'isprint(-1)' is 0 and 'isprint(0xFF)' is not
  if( msg.wParam!=VK_TAB && msg.wParam!=VK_SHIFT) strLastExpanded = "";

  // if key with letter pressed
  if( isprint(chrKey) && chrKey!='`') {
    // insert it to editing line
    strEditingLine.InsertChar( iCursorPos, chrKey);
    iCursorPos++;
    // reset history line
    iHistoryLine = 0;
  }
}

