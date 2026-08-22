///////////////////////////////////////////////////////////////////////////
//  File:       abl.cpp
//  Version:    1.0.0.0
//  Created:    24-Aug-2026
//
//  Copyright:  LibreMerge contributors
//
//  Progress OpenEdge ABL (4GL) syntax highlighting definition
//
//  You are free to use or modify this code to the following restrictions:
//  - Acknowledge me somewhere in your about box, simple "Parts of code by.."
//  will be enough. If you can't (or don't want to), contact me personally.
//  - LEAVE THIS HEADER INTACT
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "crystallineparser.h"
#include "../SyntaxColors.h"
#include "../utils/string_util.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//  Progress ABL keywords (language is case-insensitive; common
//  abbreviations such as DEF/VAR/PARAM are listed explicitly)
static const tchar_t * s_apszAblKeywordList[] =
  {
    _T ("absolute"),
    _T ("accum"),
    _T ("accumulate"),
    _T ("alert-box"),
    _T ("ambiguous"),
    _T ("and"),
    _T ("apply"),
    _T ("as"),
    _T ("assign"),
    _T ("at"),
    _T ("avail"),
    _T ("available"),
    _T ("begins"),
    _T ("blob"),
    _T ("break"),
    _T ("browse"),
    _T ("buffer"),
    _T ("buffer-compare"),
    _T ("buffer-copy"),
    _T ("button"),
    _T ("buttons"),
    _T ("by"),
    _T ("can-do"),
    _T ("can-find"),
    _T ("case"),
    _T ("catch"),
    _T ("char"),
    _T ("character"),
    _T ("class"),
    _T ("clob"),
    _T ("close"),
    _T ("colon"),
    _T ("column"),
    _T ("column-label"),
    _T ("com-handle"),
    _T ("combo-box"),
    _T ("connect"),
    _T ("connected"),
    _T ("constructor"),
    _T ("contains"),
    _T ("create"),
    _T ("data-source"),
    _T ("database"),
    _T ("dataset"),
    _T ("date"),
    _T ("datetime"),
    _T ("datetime-tz"),
    _T ("day"),
    _T ("dec"),
    _T ("decimal"),
    _T ("def"),
    _T ("define"),
    _T ("delete"),
    _T ("delimiter"),
    _T ("destructor"),
    _T ("disable"),
    _T ("disconnect"),
    _T ("disp"),
    _T ("display"),
    _T ("do"),
    _T ("down"),
    _T ("drop-down-list"),
    _T ("dynamic-function"),
    _T ("each"),
    _T ("editing"),
    _T ("editor"),
    _T ("else"),
    _T ("empty"),
    _T ("enable"),
    _T ("end"),
    _T ("entry"),
    _T ("error"),
    _T ("error-status"),
    _T ("etime"),
    _T ("exclusive-lock"),
    _T ("export"),
    _T ("extent"),
    _T ("field"),
    _T ("fields"),
    _T ("fill-in"),
    _T ("finally"),
    _T ("find"),
    _T ("first"),
    _T ("first-of"),
    _T ("font"),
    _T ("for"),
    _T ("format"),
    _T ("forward"),
    _T ("frame"),
    _T ("function"),
    _T ("get"),
    _T ("global"),
    _T ("group"),
    _T ("handle"),
    _T ("help"),
    _T ("hidden"),
    _T ("hide"),
    _T ("horizontal"),
    _T ("if"),
    _T ("implements"),
    _T ("import"),
    _T ("in"),
    _T ("index"),
    _T ("inherits"),
    _T ("init"),
    _T ("initial"),
    _T ("inner-lines"),
    _T ("input"),
    _T ("input-output"),
    _T ("insert"),
    _T ("int"),
    _T ("int64"),
    _T ("integer"),
    _T ("interface"),
    _T ("is"),
    _T ("label"),
    _T ("last"),
    _T ("last-of"),
    _T ("leave"),
    _T ("length"),
    _T ("like"),
    _T ("list-items"),
    _T ("log"),
    _T ("logical"),
    _T ("longchar"),
    _T ("matches"),
    _T ("memptr"),
    _T ("menu"),
    _T ("menu-item"),
    _T ("message"),
    _T ("method"),
    _T ("month"),
    _T ("multiple"),
    _T ("new"),
    _T ("next"),
    _T ("next-prompt"),
    _T ("no"),
    _T ("no-box"),
    _T ("no-error"),
    _T ("no-labels"),
    _T ("no-lock"),
    _T ("no-pause"),
    _T ("no-row-markers"),
    _T ("no-undo"),
    _T ("no-wait"),
    _T ("not"),
    _T ("now"),
    _T ("num-entries"),
    _T ("of"),
    _T ("ok"),
    _T ("ok-cancel"),
    _T ("on"),
    _T ("open"),
    _T ("or"),
    _T ("os-append"),
    _T ("os-command"),
    _T ("os-copy"),
    _T ("os-create-dir"),
    _T ("os-delete"),
    _T ("os-dir"),
    _T ("os-error"),
    _T ("os-rename"),
    _T ("otherwise"),
    _T ("output"),
    _T ("overlay"),
    _T ("override"),
    _T ("param"),
    _T ("parameter"),
    _T ("pause"),
    _T ("persistent"),
    _T ("preselect"),
    _T ("private"),
    _T ("procedure"),
    _T ("prompt-for"),
    _T ("protected"),
    _T ("prototype"),
    _T ("public"),
    _T ("put"),
    _T ("query"),
    _T ("question"),
    _T ("quit"),
    _T ("radio-buttons"),
    _T ("radio-set"),
    _T ("raw"),
    _T ("recid"),
    _T ("release"),
    _T ("repeat"),
    _T ("replace"),
    _T ("retain"),
    _T ("retry"),
    _T ("return"),
    _T ("return-value"),
    _T ("returns"),
    _T ("row"),
    _T ("rowid"),
    _T ("run"),
    _T ("scrollable"),
    _T ("sensitive"),
    _T ("session"),
    _T ("set"),
    _T ("share-lock"),
    _T ("shared"),
    _T ("side-labels"),
    _T ("single"),
    _T ("size"),
    _T ("skip"),
    _T ("space"),
    _T ("static"),
    _T ("stop"),
    _T ("stream"),
    _T ("string"),
    _T ("sub-menu"),
    _T ("subscribe"),
    _T ("substr"),
    _T ("substring"),
    _T ("super"),
    _T ("temp-table"),
    _T ("then"),
    _T ("this-object"),
    _T ("this-procedure"),
    _T ("throw"),
    _T ("time"),
    _T ("title"),
    _T ("to"),
    _T ("today"),
    _T ("toggle-box"),
    _T ("tooltip"),
    _T ("transaction"),
    _T ("trim"),
    _T ("undo"),
    _T ("unformatted"),
    _T ("unique"),
    _T ("unsubscribe"),
    _T ("update"),
    _T ("valid-handle"),
    _T ("valid-object"),
    _T ("validate"),
    _T ("value"),
    _T ("var"),
    _T ("variable"),
    _T ("vertical"),
    _T ("view"),
    _T ("view-as"),
    _T ("visible"),
    _T ("void"),
    _T ("wait-for"),
    _T ("weekday"),
    _T ("when"),
    _T ("where"),
    _T ("while"),
    _T ("widget-handle"),
    _T ("width"),
    _T ("with"),
    _T ("work-table"),
    _T ("workfile"),
    _T ("year"),
    _T ("yes"),
    _T ("yes-no"),
    _T ("yes-no-cancel"),
  };

static bool
IsAblKeyword (const tchar_t *pszChars, int nLength)
{
  return ISXKEYWORDI (s_apszAblKeywordList, pszChars, nLength);
}

//  ABL identifiers may contain '-' (NO-UNDO, VIEW-AS, ...) and '_'
static inline bool
IsAblIdentChar (tchar_t c)
{
  return xisalnum (c) || c == '-' || c == '_';
}

static inline void
DefineIdentiferBlock(const tchar_t *pszChars, int nLength, std::vector<CrystalLineParser::TEXTBLOCK>* pBuf, int nIdentBegin, int I)
{
  if (pszChars[nIdentBegin] == '&')
    {
      //  Preprocessor directive: &GLOBAL-DEFINE, &IF, &THEN, ...
      DEFINE_BLOCK (nIdentBegin, COLORINDEX_PREPROCESSOR);
    }
  else if (IsAblKeyword (pszChars + nIdentBegin, I - nIdentBegin))
    {
      DEFINE_BLOCK (nIdentBegin, COLORINDEX_KEYWORD);
    }
  else if (CrystalLineParser::IsXNumber (pszChars + nIdentBegin, I - nIdentBegin))
    {
      DEFINE_BLOCK (nIdentBegin, COLORINDEX_NUMBER);
    }
  else
    {
      bool bFunction = false;

      for (int j = I; j < nLength; j++)
        {
          if (!xisspace (pszChars[j]))
            {
              if (pszChars[j] == '(')
                {
                  bFunction = true;
                }
              break;
            }
        }
      if (bFunction)
        {
          DEFINE_BLOCK (nIdentBegin, COLORINDEX_FUNCNAME);
        }
    }
}

unsigned
CrystalLineParser::ParseLineAbl (unsigned dwCookie, const tchar_t *pszChars, int nLength, std::vector<TEXTBLOCK>* pBuf)
{
  if (nLength == 0)
    return dwCookie & (COOKIE_EXT_COMMENT | COOKIE_STRING | COOKIE_CHAR | 0xFF000000);

  const tchar_t *pszCommentBegin = nullptr;
  const tchar_t *pszCommentEnd = nullptr;
  bool bRedefineBlock = true;
  bool bDecIndex = false;
  int nIdentBegin = -1;
  int nPrevI = -1;
  int I=0;
  for (I = 0;; nPrevI = I, I = static_cast<int>(tc::tcharnext(pszChars+I) - pszChars))
    {
      if (I == nPrevI)
        {
          // CharNext did not advance, so we're at the end of the string
          // and we already handled this character, so stop
          break;
        }

      if (bRedefineBlock)
        {
          int nPos = I;
          if (bDecIndex)
            nPos = nPrevI;
          if (dwCookie & (COOKIE_COMMENT | COOKIE_EXT_COMMENT))
            {
              DEFINE_BLOCK (nPos, COLORINDEX_COMMENT);
            }
          else if (dwCookie & (COOKIE_CHAR | COOKIE_STRING))
            {
              DEFINE_BLOCK (nPos, COLORINDEX_STRING);
            }
          else
            {
              if (xisalnum (pszChars[nPos]) || pszChars[nPos] == '.' && nPos > 0 && (!xisalpha (*tc::tcharprev(pszChars, pszChars + nPos)) && !xisalpha (*tc::tcharnext(pszChars + nPos))))
                {
                  DEFINE_BLOCK (nPos, COLORINDEX_NORMALTEXT);
                }
              else
                {
                  DEFINE_BLOCK (nPos, COLORINDEX_OPERATOR);
                  bRedefineBlock = true;
                  bDecIndex = true;
                  goto out;
                }
            }
          bRedefineBlock = false;
          bDecIndex = false;
        }
out:

      // Can be bigger than length if there is binary data
      // See bug #1474782 Crash when comparing SQL with with binary data
      if (I >= nLength || pszChars[I] == 0)
        break;

      if (dwCookie & COOKIE_COMMENT)
        {
          DEFINE_BLOCK (I, COLORINDEX_COMMENT);
          dwCookie |= COOKIE_COMMENT;
          break;
        }

      //  String constant "...." (escape character is '~')
      if (dwCookie & COOKIE_STRING)
        {
          if (pszChars[I] == '"' && (I == 0 || pszChars[nPrevI] != '~'))
            {
              dwCookie &= ~COOKIE_STRING;
              bRedefineBlock = true;
            }
          continue;
        }

      //  String constant '....' (escape character is '~')
      if (dwCookie & COOKIE_CHAR)
        {
          if (pszChars[I] == '\'' && (I == 0 || pszChars[nPrevI] != '~'))
            {
              dwCookie &= ~COOKIE_CHAR;
              bRedefineBlock = true;
            }
          continue;
        }

      //  Extended comment /*....*/ — ABL block comments nest
      if (dwCookie & COOKIE_EXT_COMMENT)
        {
          unsigned depth = COOKIE_GET_EXT_COMMENT_DEPTH(dwCookie);
          if ((pszCommentEnd < pszChars + I) && (I > 0 && pszChars[I] == '*' && pszChars[nPrevI] == '/'))
            {
              COOKIE_SET_EXT_COMMENT_DEPTH(dwCookie, depth + 1);
              pszCommentBegin = pszChars + I + 1;
            }
          else if ((pszCommentBegin < pszChars + I) && (I > 0 && pszChars[I] == '/' && pszChars[nPrevI] == '*'))
            {
              COOKIE_SET_EXT_COMMENT_DEPTH(dwCookie, depth - 1);
              if (depth <= 1)
                {
                  dwCookie &= ~COOKIE_EXT_COMMENT;
                  bRedefineBlock = true;
                }
              pszCommentEnd = pszChars + I + 1;
            }
          continue;
        }

      //  Single-line comment // (OpenEdge 11.6+)
      if ((pszCommentEnd < pszChars + I) && (I > 0 && pszChars[I] == '/' && pszChars[nPrevI] == '/'))
        {
          DEFINE_BLOCK (nPrevI, COLORINDEX_COMMENT);
          dwCookie |= COOKIE_COMMENT;
          break;
        }

      //  Normal text
      if (pszChars[I] == '"')
        {
          DEFINE_BLOCK (I, COLORINDEX_STRING);
          dwCookie |= COOKIE_STRING;
          continue;
        }
      if (pszChars[I] == '\'')
        {
          DEFINE_BLOCK (I, COLORINDEX_STRING);
          dwCookie |= COOKIE_CHAR;
          continue;
        }
      if ((pszCommentEnd < pszChars + I) && (I > 0 && pszChars[I] == '*' && pszChars[nPrevI] == '/'))
        {
          DEFINE_BLOCK (nPrevI, COLORINDEX_COMMENT);
          dwCookie |= COOKIE_EXT_COMMENT;
          pszCommentBegin = pszChars + I + 1;
          COOKIE_SET_EXT_COMMENT_DEPTH(dwCookie, 1);
          continue;
        }

      //  Include or preprocessor reference: {include/file.i ...} / {&NAME}
      if (pszChars[I] == '{')
        {
          DEFINE_BLOCK (I, COLORINDEX_PREPROCESSOR);
          int j = I;
          while (j < nLength && pszChars[j] != '}')
            j++;
          if (j >= nLength)
            break;              //  unterminated: color to end of line
          I = j;
          bRedefineBlock = true;
          bDecIndex = false;
          nIdentBegin = -1;
          continue;
        }

      if (pBuf == nullptr)
        continue;               //  We don't need to extract keywords,
      //  for faster parsing skip the rest of loop

      if (IsAblIdentChar (pszChars[I]) || pszChars[I] == '&' && IsAblIdentChar (*tc::tcharnext(pszChars + I)))
        {
          if (nIdentBegin == -1)
            nIdentBegin = I;
        }
      else
        {
          if (nIdentBegin >= 0)
            {
              DefineIdentiferBlock(pszChars, nLength, pBuf, nIdentBegin, I);
              bRedefineBlock = true;
              bDecIndex = true;
              nIdentBegin = -1;
            }
        }
    }

  if (nIdentBegin >= 0)
    {
      DefineIdentiferBlock(pszChars, nLength, pBuf, nIdentBegin, I);
    }

  dwCookie &= COOKIE_EXT_COMMENT | COOKIE_STRING | COOKIE_CHAR | 0xFF000000;
  return dwCookie;
}
