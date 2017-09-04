/* config.c
 * Read configuration files and manage configuration properties.
 *
 * Copyright (c) 1998-2017 World Wide Web Consortium (Massachusetts
 * Institute of Technology, European Research Consortium for Informatics
 * and Mathematics, Keio University) and HTACG.
 *
 * See tidy.h for the copyright notice.
 */

#include "config.h"
#include "tidy-int.h"
#include "message.h"
#include "tmbstr.h"
#include "tags.h"

#ifdef WINDOWS_OS
#include <io.h>
#else
#ifdef DMALLOC
/*
   macro for valloc() in dmalloc.h may conflict with declaration for valloc()
   in unistd.h - we don't need (debugging for) valloc() here. dmalloc.h should
  come last but it doesn't.
*/
#ifdef valloc
#undef valloc
#endif
#endif
#include <unistd.h>
#endif

#ifdef TIDY_WIN32_MLANG_SUPPORT
#include "win32tc.h"
#endif

void TY_(InitConfig)( TidyDocImpl* doc )
{
    TidyClearMemory( &doc->config, sizeof(TidyConfigImpl) );
    TY_(ResetConfigToDefault)( doc );
}

void TY_(FreeConfig)( TidyDocImpl* doc )
{
    TY_(ResetConfigToDefault)( doc );
    TY_(TakeConfigSnapshot)( doc );
}


/* 
   Arrange so index can be cast to enum. Note that the value field in the 
   following structures is not currently used in code; they're present for 
   documentation purposes currently. The arrays must be populated in enum order.
*/
static PickListItems boolPicks = {
    { "no",  TidyNoState,  { "0", "n", "f", "no",  "false", NULL } },
    { "yes", TidyYesState, { "1", "y", "t", "yes", "true",  NULL } },
    { NULL }
};

static PickListItems autoBoolPicks = {
    { "no",   TidyNoState,  { "0", "n", "f", "no",  "false", NULL } },
    { "yes",  TidyYesState, { "1", "y", "t", "yes", "true",  NULL } },
    { "auto", TidyYesState, { "auto",                        NULL } },
    { NULL }
};

static PickListItems repeatAttrPicks = {
    { "keep-first", TidyNoState,  { "keep-first", NULL } },
    { "keep-last",  TidyYesState, { "keep-last",  NULL } },
    { NULL }
};

static PickListItems accessPicks = {
    { "0 (Tidy Classic)",      0, { "0", "0 (Tidy Classic)",      NULL } },
    { "1 (Priority 1 Checks)", 1, { "1", "1 (Priority 1 Checks)", NULL } },
    { "2 (Priority 2 Checks)", 2, { "2", "2 (Priority 2 Checks)", NULL } },
    { "3 (Priority 3 Checks)", 3, { "3", "3 (Priority 3 Checks)", NULL } },
    { NULL }
};

static PickListItems charEncPicks = {
    { "raw",      TidyEncRaw,      { "raw",      NULL } },
    { "ascii",    TidyEncAscii,    { "ascii",    NULL } },
    { "latin0",   TidyEncLatin0,   { "latin0",   NULL } },
    { "latin1",   TidyEncLatin1,   { "latin1",   NULL } },
    { "utf8",     TidyEncUtf8,     { "utf8",     NULL } },
#ifndef NO_NATIVE_ISO2022_SUPPORT
    { "iso2022",  TidyEncIso2022,  { "iso2022",  NULL } },
#endif
    { "mac",      TidyEncMac,      { "mac",      NULL } },
    { "win1252",  TidyEncWin1252,  { "win1252",  NULL } },
    { "ibm858",   TidyEncIbm858,   { "ibm858",   NULL } },

#if SUPPORT_UTF16_ENCODINGS
    { "utf16le",  TidyEncUtf16le,  { "utf16le",  NULL } },
    { "utf16be",  TidyEncUtf16be,  { "utf16be",  NULL } },
    { "utf16",    TidyEncUtf16,    { "utf16",    NULL } },
#endif

#if SUPPORT_ASIAN_ENCODINGS
    { "big5",     TidyEncBig5,     { "big5",     NULL } },
    { "shiftjis", TidyEncShiftjis, { "shiftjis", NULL } },
#endif

    { NULL }
};

static PickListItems newlinePicks = {
    { "LF",   TidyLF,   { "lf",   NULL } },
    { "CRLF", TidyCRLF, { "crlf", NULL } },
    { "CR",   TidyCR,   { "cr",   NULL } },
    { NULL }
};

static PickListItems doctypePicks = {
    { "html5",        TidyDoctypeHtml5,  { "html5",                 NULL } },
    { "omit",         TidyDoctypeOmit,   { "omit",                  NULL } },
    { "auto",         TidyDoctypeAuto,   { "auto",                  NULL } },
    { "strict",       TidyDoctypeStrict, { "strict",                NULL } },
    { "transitional", TidyDoctypeLoose,  { "loose", "transitional", NULL } },
    { "user",         TidyDoctypeUser,   { "user",                  NULL } },
    { NULL }
};

static PickListItems sorterPicks = {
    { "none",  TidySortAttrNone,  { "none",  NULL } },
    { "alpha", TidySortAttrAlpha, { "alpha", NULL } },
    { NULL }
};

static PickListItems customTagsPicks = {
    {"no",         TidyCustomNo,         { "no", "n",            NULL } },
    {"blocklevel", TidyCustomBlocklevel, { "blocklevel",         NULL } },
    {"empty",      TidyCustomEmpty,      { "empty",              NULL } },
    {"inline",     TidyCustomInline,     { "inline", "y", "yes", NULL } },
    {"pre",        TidyCustomPre,        { "pre",                NULL } },
    { NULL }
};

static PickListItems attributeCasePicks = {
    { "no",       TidyUppercaseNo,       { "0", "n", "f", "no",  "false", NULL } },
    { "yes",      TidyUppercaseYes,      { "1", "y", "t", "yes", "true",  NULL } },
    { "preserve", TidyUppercasePreserve, { "preserve",                    NULL } },
    { NULL }
};



#define MU TidyMarkup
#define DG TidyDiagnostics
#define PP TidyPrettyPrint
#define CE TidyEncoding
#define MS TidyMiscellaneous
#define IR TidyInternalCategory

#define IN TidyInteger
#define BL TidyBoolean
#define ST TidyString

#define XX (TidyConfigCategory)-1
#define XY (TidyOptionType)-1

#define DLF DEFAULT_NL_CONFIG

/* If Accessibility checks not supported, make config setting read-only */
#if SUPPORT_ACCESSIBILITY_CHECKS
#define ParseAcc ParsePickList
#else
#define ParseAcc NULL 
#endif

static void AdjustConfig( TidyDocImpl* doc );

/* parser for integer values */
static ParseProperty ParseInt;

/* a string excluding whitespace */
static ParseProperty ParseName;

/* a CSS1 selector - CSS class naming for -clean option */
static ParseProperty ParseCSS1Selector;

/* a string including whitespace */
static ParseProperty ParseString;

/* a space or comma separated list of tag names */
static ParseProperty ParseTagNames;

/* RAW, ASCII, LATIN0, LATIN1, UTF8, ISO2022, MACROMAN,
   WIN1252, IBM858, UTF16LE, UTF16BE, UTF16, BIG5, SHIFTJIS
*/
static ParseProperty ParseCharEnc;

/* html5 | omit | auto | strict | loose | <fpi> */
static ParseProperty ParseDocType;

/* 20150515 - support using tabs instead of spaces - Issue #108
 */
static ParseProperty ParseTabs;

/* General parser for options having picklists */
static ParseProperty ParsePickList;


/* Ensure struct order is same order as tidyenum.h:TidyOptionId! */
static const TidyOptionImpl option_defs[] =
{
    { TidyUnknownOption,           MS, "unknown!",                    IN, 0,               NULL,              NULL                },
    { TidyAccessibilityCheckLevel, DG, "accessibility-check",         IN, 0,               ParseAcc,          &accessPicks        },
    { TidyAltText,                 MU, "alt-text",                    ST, 0,               ParseString,       NULL                },
    { TidyAnchorAsName,            MU, "anchor-as-name",              BL, yes,             ParsePickList,     &boolPicks          },
    { TidyAsciiChars,              CE, "ascii-chars",                 BL, no,              ParsePickList,     &boolPicks          },
    { TidyBlockTags,               MU, "new-blocklevel-tags",         ST, 0,               ParseTagNames,     NULL                },
    { TidyBodyOnly,                MU, "show-body-only",              IN, no,              ParsePickList,     &autoBoolPicks      },
    { TidyBreakBeforeBR,           PP, "break-before-br",             BL, no,              ParsePickList,     &boolPicks          },
    { TidyCharEncoding,            CE, "char-encoding",               IN, UTF8,            ParseCharEnc,      &charEncPicks       },
    { TidyCoerceEndTags,           MU, "coerce-endtags",              BL, yes,             ParsePickList,     &boolPicks          },
    { TidyCSSPrefix,               MU, "css-prefix",                  ST, 0,               ParseCSS1Selector, NULL                },
    { TidyCustomTags,              IR, "new-custom-tags",             ST, 0,               ParseTagNames,     NULL                }, /* 20170309 - Issue #119 */
    { TidyDecorateInferredUL,      MU, "decorate-inferred-ul",        BL, no,              ParsePickList,     &boolPicks          },
    { TidyDoctype,                 MU, "doctype",                     ST, 0,               ParseDocType,      &doctypePicks       },
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    { TidyDoctypeMode,             IR, "doctype-mode",                IN, TidyDoctypeAuto, NULL,              &doctypePicks       },
#endif
    { TidyDropEmptyElems,          MU, "drop-empty-elements",         BL, yes,             ParsePickList,     &boolPicks          },
    { TidyDropEmptyParas,          MU, "drop-empty-paras",            BL, yes,             ParsePickList,     &boolPicks          },
    { TidyDropPropAttrs,           MU, "drop-proprietary-attributes", BL, no,              ParsePickList,     &boolPicks          },
    { TidyDuplicateAttrs,          MU, "repeated-attributes",         IN, TidyKeepLast,    ParsePickList,     &repeatAttrPicks    },
    { TidyEmacs,                   MS, "gnu-emacs",                   BL, no,              ParsePickList,     &boolPicks          },
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    { TidyEmacsFile,               IR, "gnu-emacs-file",              ST, 0,               ParseString,       NULL                },
#endif
    { TidyEmptyTags,               MU, "new-empty-tags",              ST, 0,               ParseTagNames,     NULL                },
    { TidyEncloseBlockText,        MU, "enclose-block-text",          BL, no,              ParsePickList,     &boolPicks          },
    { TidyEncloseBodyText,         MU, "enclose-text",                BL, no,              ParsePickList,     &boolPicks          },
    { TidyErrFile,                 MS, "error-file",                  ST, 0,               ParseString,       NULL                },
    { TidyEscapeCdata,             MU, "escape-cdata",                BL, no,              ParsePickList,     &boolPicks          },
    { TidyEscapeScripts,           PP, "escape-scripts",              BL, yes,             ParsePickList,     &boolPicks          }, /* 20160227 - Issue #348 */
    { TidyFixBackslash,            MU, "fix-backslash",               BL, yes,             ParsePickList,     &boolPicks          },
    { TidyFixComments,             MU, "fix-bad-comments",            BL, yes,             ParsePickList,     &boolPicks          },
    { TidyFixUri,                  MU, "fix-uri",                     BL, yes,             ParsePickList,     &boolPicks          },
    { TidyForceOutput,             MS, "force-output",                BL, no,              ParsePickList,     &boolPicks          },
    { TidyGDocClean,               MU, "gdoc",                        BL, no,              ParsePickList,     &boolPicks          },
    { TidyHideComments,            MU, "hide-comments",               BL, no,              ParsePickList,     &boolPicks          },
    { TidyHtmlOut,                 MU, "output-html",                 BL, no,              ParsePickList,     &boolPicks          },
    { TidyInCharEncoding,          CE, "input-encoding",              IN, UTF8,            ParseCharEnc,      &charEncPicks       },
    { TidyIndentAttributes,        PP, "indent-attributes",           BL, no,              ParsePickList,     &boolPicks          },
    { TidyIndentCdata,             MU, "indent-cdata",                BL, no,              ParsePickList,     &boolPicks          },
    { TidyIndentContent,           PP, "indent",                      IN, TidyNoState,     ParsePickList,     &autoBoolPicks      },
    { TidyIndentSpaces,            PP, "indent-spaces",               IN, 2,               ParseInt,          NULL                },
    { TidyInlineTags,              MU, "new-inline-tags",             ST, 0,               ParseTagNames,     NULL                },
    { TidyJoinClasses,             MU, "join-classes",                BL, no,              ParsePickList,     &boolPicks          },
    { TidyJoinStyles,              MU, "join-styles",                 BL, yes,             ParsePickList,     &boolPicks          },
    { TidyKeepFileTimes,           MS, "keep-time",                   BL, no,              ParsePickList,     &boolPicks          },
    { TidyLiteralAttribs,          MU, "literal-attributes",          BL, no,              ParsePickList,     &boolPicks          },
    { TidyLogicalEmphasis,         MU, "logical-emphasis",            BL, no,              ParsePickList,     &boolPicks          },
    { TidyLowerLiterals,           MU, "lower-literals",              BL, yes,             ParsePickList,     &boolPicks          },
    { TidyMakeBare,                MU, "bare",                        BL, no,              ParsePickList,     &boolPicks          },
    { TidyMakeClean,               MU, "clean",                       BL, no,              ParsePickList,     &boolPicks          },
    { TidyMark,                    MS, "tidy-mark",                   BL, yes,             ParsePickList,     &boolPicks          },
    { TidyMergeDivs,               MU, "merge-divs",                  IN, TidyAutoState,   ParsePickList,     &autoBoolPicks      },
    { TidyMergeEmphasis,           MU, "merge-emphasis",              BL, yes,             ParsePickList,     &boolPicks          },
    { TidyMergeSpans,              MU, "merge-spans",                 IN, TidyAutoState,   ParsePickList,     &autoBoolPicks      },
    { TidyMetaCharset,             MS, "add-meta-charset",            BL, no,              ParsePickList,     &boolPicks          }, /* 20161004 - Issue #456 */
#if SUPPORT_ASIAN_ENCODINGS
    { TidyNCR,                     MU, "ncr",                         BL, yes,             ParsePickList,     &boolPicks          },
#endif
    { TidyNewline,                 CE, "newline",                     IN, DLF,             ParsePickList,     &newlinePicks       },
    { TidyNumEntities,             MU, "numeric-entities",            BL, no,              ParsePickList,     &boolPicks          },
    { TidyOmitOptionalTags,        MU, "omit-optional-tags",          BL, no,              ParsePickList,     &boolPicks          },
    { TidyOutCharEncoding,         CE, "output-encoding",             IN, UTF8,            ParseCharEnc,      &charEncPicks       },
    { TidyOutFile,                 MS, "output-file",                 ST, 0,               ParseString,       NULL                },
#if SUPPORT_UTF16_ENCODINGS
    { TidyOutputBOM,               CE, "output-bom",                  IN, TidyAutoState,   ParsePickList,     &autoBoolPicks      },
#endif
    { TidyPPrintTabs,              PP, "indent-with-tabs",            BL, no,              ParseTabs,         &boolPicks          }, /* 20150515 - Issue #108 */
    { TidyPreserveEntities,        MU, "preserve-entities",           BL, no,              ParsePickList,     &boolPicks          },
    { TidyPreTags,                 MU, "new-pre-tags",                ST, 0,               ParseTagNames,     NULL                },
#if SUPPORT_ASIAN_ENCODINGS
    { TidyPunctWrap,               PP, "punctuation-wrap",            BL, no,              ParsePickList,     &boolPicks          },
#endif
    { TidyQuiet,                   MS, "quiet",                       BL, no,              ParsePickList,     &boolPicks          },
    { TidyQuoteAmpersand,          MU, "quote-ampersand",             BL, yes,             ParsePickList,     &boolPicks          },
    { TidyQuoteMarks,              MU, "quote-marks",                 BL, no,              ParsePickList,     &boolPicks          },
    { TidyQuoteNbsp,               MU, "quote-nbsp",                  BL, yes,             ParsePickList,     &boolPicks          },
    { TidyReplaceColor,            MU, "replace-color",               BL, no,              ParsePickList,     &boolPicks          },
    { TidyShowErrors,              DG, "show-errors",                 IN, 6,               ParseInt,          NULL                },
    { TidyShowInfo,                DG, "show-info",                   BL, yes,             ParsePickList,     &boolPicks          },
    { TidyShowMarkup,              PP, "markup",                      BL, yes,             ParsePickList,     &boolPicks          },
    { TidyShowMetaChange,          MS, "show-meta-change",            BL, no,              ParsePickList,     &boolPicks          }, /* 20170609 - Issue #456 */
    { TidyShowWarnings,            DG, "show-warnings",               BL, yes,             ParsePickList,     &boolPicks          },
    { TidySkipNested,              MU, "skip-nested",                 BL, yes,             ParsePickList,     &boolPicks          }, /* 1642186 - Issue #65 */
    { TidySortAttributes,          PP, "sort-attributes",             IN, TidySortAttrNone,ParsePickList,     &sorterPicks        },
    { TidyStrictTagsAttr,          MU, "strict-tags-attributes",      BL, no,              ParsePickList,     &boolPicks          }, /* 20160209 - Issue #350 */
    { TidyStyleTags,               MU, "fix-style-tags",              BL, yes,             ParsePickList,     &boolPicks          },
    { TidyTabSize,                 PP, "tab-size",                    IN, 8,               ParseInt,          NULL                },
    { TidyUpperCaseAttrs,          MU, "uppercase-attributes",        IN, TidyUppercaseNo, ParsePickList,     &attributeCasePicks },
    { TidyUpperCaseTags,           MU, "uppercase-tags",              BL, no,              ParsePickList,     &boolPicks          },
    { TidyUseCustomTags,           MU, "custom-tags",                 IN, TidyCustomNo,    ParsePickList,     &customTagsPicks    }, /* 20170309 - Issue #119 */
    { TidyVertSpace,               PP, "vertical-space",              IN, no,              ParsePickList,     &autoBoolPicks      }, /* #228 - tri option */
    { TidyWarnPropAttrs,           MU, "warn-proprietary-attributes", BL, yes,             ParsePickList,     &boolPicks          },
    { TidyWord2000,                MU, "word-2000",                   BL, no,              ParsePickList,     &boolPicks          },
    { TidyWrapAsp,                 PP, "wrap-asp",                    BL, yes,             ParsePickList,     &boolPicks          },
    { TidyWrapAttVals,             PP, "wrap-attributes",             BL, no,              ParsePickList,     &boolPicks          },
    { TidyWrapJste,                PP, "wrap-jste",                   BL, yes,             ParsePickList,     &boolPicks          },
    { TidyWrapLen,                 PP, "wrap",                        IN, 68,              ParseInt,          NULL                },
    { TidyWrapPhp,                 PP, "wrap-php",                    BL, yes,             ParsePickList,     &boolPicks          },
    { TidyWrapScriptlets,          PP, "wrap-script-literals",        BL, no,              ParsePickList,     &boolPicks          },
    { TidyWrapSection,             PP, "wrap-sections",               BL, yes,             ParsePickList,     &boolPicks          },
    { TidyWriteBack,               MS, "write-back",                  BL, no,              ParsePickList,     &boolPicks          },
    { TidyXhtmlOut,                MU, "output-xhtml",                BL, no,              ParsePickList,     &boolPicks          },
    { TidyXmlDecl,                 MU, "add-xml-decl",                BL, no,              ParsePickList,     &boolPicks          },
    { TidyXmlOut,                  MU, "output-xml",                  BL, no,              ParsePickList,     &boolPicks          },
    { TidyXmlPIs,                  MU, "assume-xml-procins",          BL, no,              ParsePickList,     &boolPicks          },
    { TidyXmlSpace,                MU, "add-xml-space",               BL, no,              ParsePickList,     &boolPicks          },
    { TidyXmlTags,                 MU, "input-xml",                   BL, no,              ParsePickList,     &boolPicks          },
    { N_TIDY_OPTIONS,              XX, NULL,                          XY, 0,               NULL,              NULL                }
};


/* Should only be called by options set by name
** thus, it is cheaper to do a few scans than set
** up every option in a hash table.
*/
const TidyOptionImpl* TY_(lookupOption)( ctmbstr s )
{
    const TidyOptionImpl* np = option_defs;
    for ( /**/; np < option_defs + N_TIDY_OPTIONS; ++np )
    {
        if ( TY_(tmbstrcasecmp)(s, np->name) == 0 )
            return np;
    }
    return NULL;
}

const TidyOptionImpl* TY_(getOption)( TidyOptionId optId )
{
  if ( optId < N_TIDY_OPTIONS )
      return option_defs + optId;
  return NULL;
}


static void FreeOptionValue( TidyDocImpl* doc, const TidyOptionImpl* option, TidyOptionValue* value )
{
    if ( option->type == TidyString && value->p && value->p != option->pdflt )
        TidyDocFree( doc, value->p );
}

static void CopyOptionValue( TidyDocImpl* doc, const TidyOptionImpl* option,
                             TidyOptionValue* oldval, const TidyOptionValue* newval )
{
    assert( oldval != NULL );
    FreeOptionValue( doc, option, oldval );

    if ( option->type == TidyString )
    {
        if ( newval->p && newval->p != option->pdflt )
            oldval->p = TY_(tmbstrdup)( doc->allocator, newval->p );
        else
            oldval->p = newval->p;
    }
    else
        oldval->v = newval->v;
}


static Bool SetOptionValue( TidyDocImpl* doc, TidyOptionId optId, ctmbstr val )
{
   const TidyOptionImpl* option = &option_defs[ optId ];
   Bool status = ( optId < N_TIDY_OPTIONS );
   if ( status )
   {
      assert( option->id == optId && option->type == TidyString );
      FreeOptionValue( doc, option, &doc->config.value[ optId ] );
      if ( TY_(tmbstrlen)(val)) /* Issue #218 - ONLY if it has LENGTH! */
          doc->config.value[ optId ].p = TY_(tmbstrdup)( doc->allocator, val );
      else
          doc->config.value[ optId ].p = 0; /* should already be zero, but to be sure... */
   }
   return status;
}

Bool TY_(SetOptionInt)( TidyDocImpl* doc, TidyOptionId optId, ulong val )
{
   Bool status = ( optId < N_TIDY_OPTIONS );
   if ( status )
   {
       assert( option_defs[ optId ].type == TidyInteger );
       doc->config.value[ optId ].v = val;
   }
   return status;
}

Bool TY_(SetOptionBool)( TidyDocImpl* doc, TidyOptionId optId, Bool val )
{
   Bool status = ( optId < N_TIDY_OPTIONS );
   if ( status )
   {
       assert( option_defs[ optId ].type == TidyBoolean );
       doc->config.value[ optId ].v = val;
   }
   return status;
}

static void GetOptionDefault( const TidyOptionImpl* option,
                              TidyOptionValue* dflt )
{
    if ( option->type == TidyString )
        dflt->p = (char*)option->pdflt;
    else
        dflt->v = option->dflt;
}

static Bool OptionValueEqDefault( const TidyOptionImpl* option,
                                  const TidyOptionValue* val )
{
    return ( option->type == TidyString ) ?
        val->p == option->pdflt :
        val->v == option->dflt;
}

Bool TY_(ResetOptionToDefault)( TidyDocImpl* doc, TidyOptionId optId )
{
    Bool status = ( optId > 0 && optId < N_TIDY_OPTIONS );
    if ( status )
    {
        TidyOptionValue dflt;
        const TidyOptionImpl* option = option_defs + optId;
        TidyOptionValue* value = &doc->config.value[ optId ];
        assert( optId == option->id );
        GetOptionDefault( option, &dflt );
        CopyOptionValue( doc, option, value, &dflt );
    }
    return status;
}

static void ReparseTagType( TidyDocImpl* doc, TidyOptionId optId )
{
    ctmbstr tagdecl = cfgStr( doc, optId );
    tmbstr dupdecl = TY_(tmbstrdup)( doc->allocator, tagdecl );
    TY_(ParseConfigValue)( doc, optId, dupdecl );
    TidyDocFree( doc, dupdecl );
}

static Bool OptionValueIdentical( const TidyOptionImpl* option,
                                  const TidyOptionValue* val1,
                                  const TidyOptionValue* val2 )
{
    if ( option->type == TidyString )
    {
        if ( val1->p == val2->p )
            return yes;
        if ( !val1->p || !val2->p )
            return no;
        return TY_(tmbstrcmp)( val1->p, val2->p ) == 0;
    }
    else
        return val1->v == val2->v;
}

static Bool NeedReparseTagDecls( TidyDocImpl* doc,
                                 const TidyOptionValue* current,
                                 const TidyOptionValue* new,
                                 uint *changedUserTags )
{
    Bool ret = no;
    uint ixVal;
    const TidyOptionImpl* option = option_defs;
    *changedUserTags = tagtype_null;

    for ( ixVal=0; ixVal < N_TIDY_OPTIONS; ++option, ++ixVal )
    {
        assert( ixVal == (uint) option->id );
        switch (option->id)
        {
#define TEST_USERTAGS(USERTAGOPTION,USERTAGTYPE) \
        case USERTAGOPTION: \
            if (!OptionValueIdentical(option,&current[ixVal],&new[ixVal])) \
            { \
                *changedUserTags |= USERTAGTYPE; \
                ret = yes; \
            } \
            break
            TEST_USERTAGS(TidyInlineTags,tagtype_inline);
            TEST_USERTAGS(TidyBlockTags,tagtype_block);
            TEST_USERTAGS(TidyEmptyTags,tagtype_empty);
            TEST_USERTAGS(TidyPreTags,tagtype_pre);
        default:
            break;
        }
    }
    return ret;
}

static void ReparseTagDecls( TidyDocImpl* doc, uint changedUserTags  )
{
#define REPARSE_USERTAGS(USERTAGOPTION,USERTAGTYPE) \
    if ( changedUserTags & USERTAGTYPE ) \
    { \
        TY_(FreeDeclaredTags)( doc, USERTAGTYPE ); \
        ReparseTagType( doc, USERTAGOPTION ); \
    }
    REPARSE_USERTAGS(TidyInlineTags,tagtype_inline);
    REPARSE_USERTAGS(TidyBlockTags,tagtype_block);
    REPARSE_USERTAGS(TidyEmptyTags,tagtype_empty);
    REPARSE_USERTAGS(TidyPreTags,tagtype_pre);
}

void TY_(ResetConfigToDefault)( TidyDocImpl* doc )
{
    uint ixVal;
    const TidyOptionImpl* option = option_defs;
    TidyOptionValue* value = &doc->config.value[ 0 ];
    for ( ixVal=0; ixVal < N_TIDY_OPTIONS; ++option, ++ixVal )
    {
        TidyOptionValue dflt;
        assert( ixVal == (uint) option->id );
        GetOptionDefault( option, &dflt );
        CopyOptionValue( doc, option, &value[ixVal], &dflt );
    }
    TY_(FreeDeclaredTags)( doc, tagtype_null );
}

void TY_(TakeConfigSnapshot)( TidyDocImpl* doc )
{
    uint ixVal;
    const TidyOptionImpl* option = option_defs;
    const TidyOptionValue* value = &doc->config.value[ 0 ];
    TidyOptionValue* snap  = &doc->config.snapshot[ 0 ];

    AdjustConfig( doc );  /* Make sure it's consistent */
    for ( ixVal=0; ixVal < N_TIDY_OPTIONS; ++option, ++ixVal )
    {
        assert( ixVal == (uint) option->id );
        CopyOptionValue( doc, option, &snap[ixVal], &value[ixVal] );
    }
}

void TY_(ResetConfigToSnapshot)( TidyDocImpl* doc )
{
    uint ixVal;
    const TidyOptionImpl* option = option_defs;
    TidyOptionValue* value = &doc->config.value[ 0 ];
    const TidyOptionValue* snap  = &doc->config.snapshot[ 0 ];
    uint changedUserTags;
    Bool needReparseTagsDecls = NeedReparseTagDecls( doc, value, snap,
                                                     &changedUserTags );
    
    for ( ixVal=0; ixVal < N_TIDY_OPTIONS; ++option, ++ixVal )
    {
        assert( ixVal == (uint) option->id );
        CopyOptionValue( doc, option, &value[ixVal], &snap[ixVal] );
    }
    if ( needReparseTagsDecls )
        ReparseTagDecls( doc, changedUserTags );
}

void TY_(CopyConfig)( TidyDocImpl* docTo, TidyDocImpl* docFrom )
{
    if ( docTo != docFrom )
    {
        uint ixVal;
        const TidyOptionImpl* option = option_defs;
        const TidyOptionValue* from = &docFrom->config.value[ 0 ];
        TidyOptionValue* to   = &docTo->config.value[ 0 ];
        uint changedUserTags;
        Bool needReparseTagsDecls = NeedReparseTagDecls( docTo, to, from,
                                                         &changedUserTags );

        TY_(TakeConfigSnapshot)( docTo );
        for ( ixVal=0; ixVal < N_TIDY_OPTIONS; ++option, ++ixVal )
        {
            assert( ixVal == (uint) option->id );
            CopyOptionValue( docTo, option, &to[ixVal], &from[ixVal] );
        }
        if ( needReparseTagsDecls )
            ReparseTagDecls( docTo, changedUserTags  );
        AdjustConfig( docTo );  /* Make sure it's consistent */
    }
}


#ifdef _DEBUG

/* Debug accessor functions will be type-safe and assert option type match */
ulong   TY_(_cfgGet)( TidyDocImpl* doc, TidyOptionId optId )
{
  assert( optId < N_TIDY_OPTIONS );
  return doc->config.value[ optId ].v;
}

Bool    TY_(_cfgGetBool)( TidyDocImpl* doc, TidyOptionId optId )
{
  ulong val = TY_(_cfgGet)( doc, optId );
  const TidyOptionImpl* opt = &option_defs[ optId ];
  assert( opt && opt->type == TidyBoolean );
  return (Bool) val;
}

TidyTriState    TY_(_cfgGetAutoBool)( TidyDocImpl* doc, TidyOptionId optId )
{
  ulong val = TY_(_cfgGet)( doc, optId );
  const TidyOptionImpl* opt = &option_defs[ optId ];
  assert( opt && opt->type == TidyInteger
          && opt->parser == ParsePickList );
  return (TidyTriState) val;
}

ctmbstr TY_(_cfgGetString)( TidyDocImpl* doc, TidyOptionId optId )
{
  const TidyOptionImpl* opt;

  assert( optId < N_TIDY_OPTIONS );
  opt = &option_defs[ optId ];
  assert( opt && opt->type == TidyString );
  return doc->config.value[ optId ].p;
}
#endif


static tchar GetC( TidyConfigImpl* config )
{
    if ( config->cfgIn )
        return TY_(ReadChar)( config->cfgIn );
    return EndOfStream;
}

static tchar FirstChar( TidyConfigImpl* config )
{
    config->c = GetC( config );
    return config->c;
}

static tchar AdvanceChar( TidyConfigImpl* config )
{
    if ( config->c != EndOfStream )
        config->c = GetC( config );
    return config->c;
}

static tchar SkipWhite( TidyConfigImpl* config )
{
    while ( TY_(IsWhite)(config->c) && !TY_(IsNewline)(config->c) )
        config->c = GetC( config );
    return config->c;
}

/* skip until end of line
static tchar SkipToEndofLine( TidyConfigImpl* config )
{
    while ( config->c != EndOfStream )
    {
        config->c = GetC( config );
        if ( config->c == '\n' || config->c == '\r' )
            break;
    }
    return config->c;
}
*/

/*
 skip over line continuations
 to start of next property
*/
static uint NextProperty( TidyConfigImpl* config )
{
    do
    {
        /* skip to end of line */
        while ( config->c != '\n' &&  config->c != '\r' &&  config->c != EndOfStream )
             config->c = GetC( config );

        /* treat  \r\n   \r  or  \n as line ends */
        if ( config->c == '\r' )
             config->c = GetC( config );

        if ( config->c == '\n' )
            config->c = GetC( config );
    }
    while ( TY_(IsWhite)(config->c) );  /* line continuation? */

    return config->c;
}

/*
 Todd Lewis contributed this code for expanding
 ~/foo or ~your/foo according to $HOME and your
 user name. This will work partially on any system 
 which defines $HOME.  Support for ~user/foo will
 work on systems that support getpwnam(userid), 
 namely Unix/Linux.
*/
static ctmbstr ExpandTilde( TidyDocImpl* doc, ctmbstr filename )
{
    char *home_dir = NULL;

    if ( !filename )
        return NULL;

    if ( filename[0] != '~' )
        return filename;

    if (filename[1] == '/')
    {
        home_dir = getenv("HOME");
        if ( home_dir )
            ++filename;
    }
#ifdef SUPPORT_GETPWNAM
    else
    {
        struct passwd *passwd = NULL;
        ctmbstr s = filename + 1;
        tmbstr t;

        while ( *s && *s != '/' )
            s++;

        if ( t = TidyDocAlloc(doc, s - filename) )
        {
            memcpy(t, filename+1, s-filename-1);
            t[s-filename-1] = 0;

            passwd = getpwnam(t);

            TidyDocFree(doc, t);
        }

        if ( passwd )
        {
            filename = s;
            home_dir = passwd->pw_dir;
        }
    }
#endif /* SUPPORT_GETPWNAM */

    if ( home_dir )
    {
        uint len = TY_(tmbstrlen)(filename) + TY_(tmbstrlen)(home_dir) + 1;
        tmbstr p = (tmbstr)TidyDocAlloc( doc, len );
        TY_(tmbstrcpy)( p, home_dir );
        TY_(tmbstrcat)( p, filename );
        return (ctmbstr) p;
    }
    return (ctmbstr) filename;
}

Bool TIDY_CALL tidyFileExists( TidyDoc tdoc, ctmbstr filename )
{
  TidyDocImpl* doc = tidyDocToImpl( tdoc );
  ctmbstr fname = (tmbstr) ExpandTilde( doc, filename );
#ifndef NO_ACCESS_SUPPORT
  Bool exists = ( access(fname, 0) == 0 );
#else
  Bool exists;
  /* at present */
  FILE* fin = fopen(fname, "r");
  if (fin != NULL)
      fclose(fin);
  exists = ( fin != NULL );
#endif
  if ( fname != filename )
      TidyDocFree( doc, (tmbstr) fname );
  return exists;
}


#ifndef TIDY_MAX_NAME
#define TIDY_MAX_NAME 64
#endif

int TY_(ParseConfigFile)( TidyDocImpl* doc, ctmbstr file )
{
    return TY_(ParseConfigFileEnc)( doc, file, "ascii" );
}

/* open the file and parse its contents
*/
int TY_(ParseConfigFileEnc)( TidyDocImpl* doc, ctmbstr file, ctmbstr charenc )
{
    uint opterrs = doc->optionErrors;
    tmbstr fname = (tmbstr) ExpandTilde( doc, file );
    TidyConfigImpl* cfg = &doc->config;
    FILE* fin = fopen( fname, "r" );
    int enc = TY_(CharEncodingId)( doc, charenc );

    if ( fin == NULL || enc < 0 )
    {
        TY_(ReportFileError)( doc, fname, FILE_CANT_OPEN_CFG );
        return -1;
    }
    else
    {
        tchar c;
        cfg->cfgIn = TY_(FileInput)( doc, fin, enc );
        c = FirstChar( cfg );
       
        for ( c = SkipWhite(cfg); c != EndOfStream; c = NextProperty(cfg) )
        {
            uint ix = 0;
            tmbchar name[ TIDY_MAX_NAME ] = {0};

            /* // or # start a comment */
            if ( c == '/' || c == '#' )
                continue;

            while ( ix < sizeof(name)-1 && c != '\n' && c != EndOfStream && c != ':' )
            {
                name[ ix++ ] = (tmbchar) c;  /* Option names all ASCII */
                c = AdvanceChar( cfg );
            }

            if ( c == ':' )
            {
                const TidyOptionImpl* option = TY_(lookupOption)( name );
                c = AdvanceChar( cfg );
                if ( option )
                    option->parser( doc, option );
                else
                {
                    if ( (NULL != doc->pOptCallback) || (NULL != doc->pConfigCallback) )
                    {
                        TidyConfigImpl* cfg = &doc->config;
                        tmbchar buf[8192];
                        uint i = 0;
                        tchar delim = 0;
                        Bool waswhite = yes;
                        Bool response = yes;

                        tchar c = SkipWhite( cfg );

                        if ( c == '"' || c == '\'' )
                        {
                            delim = c;
                            c = AdvanceChar( cfg );
                        }

                        while ( i < sizeof(buf)-2 && c != EndOfStream && c != '\r' && c != '\n' )
                        {
                            if ( delim && c == delim )
                                break;

                            if ( TY_(IsWhite)(c) )
                            {
                                if ( waswhite )
                                {
                                    c = AdvanceChar( cfg );
                                    continue;
                                }
                                c = ' ';
                            }
                            else
                                waswhite = no;

                            buf[i++] = (tmbchar) c;
                            c = AdvanceChar( cfg );
                        }
                        buf[i] = '\0';
                        
                        if ( doc->pOptCallback )
                            response = response && (*doc->pOptCallback)( name, buf );

                        if ( doc->pConfigCallback )
                            response = response && (*doc->pConfigCallback)( tidyImplToDoc(doc), name, buf );

                        if (response == no)
                            TY_(ReportUnknownOption)( doc, name );
                    }
                    else
                        TY_(ReportUnknownOption)( doc, name );
                }
            }
        }

        TY_(freeFileSource)(&cfg->cfgIn->source, yes);
        TY_(freeStreamIn)( cfg->cfgIn );
        cfg->cfgIn = NULL;
    }

    if ( fname != (tmbstr) file )
        TidyDocFree( doc, fname );

    AdjustConfig( doc );

    /* any new config errors? If so, return warning status. */
    return (doc->optionErrors > opterrs ? 1 : 0); 
}

/* returns false if unknown option, missing parameter,
** or option doesn't use parameter
*/
Bool TY_(ParseConfigOption)( TidyDocImpl* doc, ctmbstr optnam, ctmbstr optval )
{
    const TidyOptionImpl* option = TY_(lookupOption)( optnam );
    Bool status = ( option != NULL );
    if ( !status )
    {
        /* Not a standard tidy option.  Check to see if the user application 
           recognizes it  */
        if (NULL != doc->pOptCallback)
            status = (*doc->pOptCallback)( optnam, optval );
        if (!status)
            TY_(ReportUnknownOption)( doc, optnam );
    }
    else 
        status = TY_(ParseConfigValue)( doc, option->id, optval );
    return status;
}

/* returns false if unknown option, missing parameter,
** or option doesn't use parameter
*/
Bool TY_(ParseConfigValue)( TidyDocImpl* doc, TidyOptionId optId, ctmbstr optval )
{
    const TidyOptionImpl* option = NULL;
    /* #472: fail status if there is a NULL parser. @ralfjunker */
    Bool status = ( optId < N_TIDY_OPTIONS
                   && (option = option_defs + optId)->parser
                   && optval != NULL );

    if ( !status )
        if ( option )
            TY_(ReportBadArgument)(doc, option->name);
        else
        {
            /* If optId < N_TIDY_OPTIONS then option remains unassigned,
               and we have to fall back to an ugly error message. */
            enum { sizeBuf = 11 }; /* uint_max is 10 characters */
            char buffer[sizeBuf];
            TY_(tmbsnprintf(buffer, sizeBuf, "%u", optId));
            TY_(ReportUnknownOption(doc, buffer));
        }
    else
    {
        TidyBuffer inbuf;            /* Set up input source */
        tidyBufInitWithAllocator( &inbuf, doc->allocator );
        tidyBufAttach( &inbuf, (byte*)optval, TY_(tmbstrlen)(optval)+1 );
        if (optId == TidyOutFile)
            doc->config.cfgIn = TY_(BufferInput)( doc, &inbuf, RAW );
        else
            doc->config.cfgIn = TY_(BufferInput)( doc, &inbuf, RAW ); /* Issue #468 - Was ASCII! */
        doc->config.c = GetC( &doc->config );

        status = option->parser( doc, option );

        TY_(freeStreamIn)(doc->config.cfgIn);  /* Release input source */
        doc->config.cfgIn  = NULL;
        tidyBufDetach( &inbuf );
    }
    return status;
}


/* ensure that char encodings are self consistent */
Bool  TY_(AdjustCharEncoding)( TidyDocImpl* doc, int encoding )
{
    int outenc = -1;
    int inenc = -1;
    
    switch( encoding )
    {
    case MACROMAN:
        inenc = MACROMAN;
        outenc = ASCII;
        break;

    case WIN1252:
        inenc = WIN1252;
        outenc = ASCII;
        break;

    case IBM858:
        inenc = IBM858;
        outenc = ASCII;
        break;

    case ASCII:
        inenc = LATIN1;
        outenc = ASCII;
        break;

    case LATIN0:
        inenc = LATIN0;
        outenc = ASCII;
        break;

    case RAW:
    case LATIN1:
    case UTF8:
#ifndef NO_NATIVE_ISO2022_SUPPORT
    case ISO2022:
#endif

#if SUPPORT_UTF16_ENCODINGS
    case UTF16LE:
    case UTF16BE:
    case UTF16:
#endif
#if SUPPORT_ASIAN_ENCODINGS
    case SHIFTJIS:
    case BIG5:
#endif
        inenc = outenc = encoding;
        break;
    }

    if ( inenc >= 0 )
    {
        TY_(SetOptionInt)( doc, TidyCharEncoding, encoding );
        TY_(SetOptionInt)( doc, TidyInCharEncoding, inenc );
        TY_(SetOptionInt)( doc, TidyOutCharEncoding, outenc );
        return yes;
    }
    return no;
}

/* ensure that config is self consistent */
void AdjustConfig( TidyDocImpl* doc )
{
    if ( cfgBool(doc, TidyEncloseBlockText) )
        TY_(SetOptionBool)( doc, TidyEncloseBodyText, yes );

    if ( cfgAutoBool(doc, TidyIndentContent) == TidyNoState )
        TY_(SetOptionInt)( doc, TidyIndentSpaces, 0 );

    /* disable wrapping */
    if ( cfg(doc, TidyWrapLen) == 0 )
        TY_(SetOptionInt)( doc, TidyWrapLen, 0x7FFFFFFF );

    /* Word 2000 needs o:p to be declared as inline */
    if ( cfgBool(doc, TidyWord2000) )
    {
        doc->config.defined_tags |= tagtype_inline;
        TY_(DefineTag)( doc, tagtype_inline, "o:p" );
    }

    /* #480701 disable XHTML output flag if both output-xhtml and xml input are set */
    if ( cfgBool(doc, TidyXmlTags) )
        TY_(SetOptionBool)( doc, TidyXhtmlOut, no );

    /* XHTML is written in lower case */
    if ( cfgBool(doc, TidyXhtmlOut) )
    {
        TY_(SetOptionBool)( doc, TidyXmlOut, yes );
        TY_(SetOptionBool)( doc, TidyUpperCaseTags, no );
        TY_(SetOptionInt)( doc, TidyUpperCaseAttrs, no );
        /* TY_(SetOptionBool)( doc, TidyXmlPIs, yes ); */
    }

    /* if XML in, then XML out */
    if ( cfgBool(doc, TidyXmlTags) )
    {
        TY_(SetOptionBool)( doc, TidyXmlOut, yes );
        TY_(SetOptionBool)( doc, TidyXmlPIs, yes );
    }

    /* #427837 - fix by Dave Raggett 02 Jun 01
    ** generate <?xml version="1.0" encoding="iso-8859-1"?>
    ** if the output character encoding is Latin-1 etc.
    */
    if ( cfg(doc, TidyOutCharEncoding) != ASCII &&
         cfg(doc, TidyOutCharEncoding) != UTF8 &&
#if SUPPORT_UTF16_ENCODINGS
         cfg(doc, TidyOutCharEncoding) != UTF16 &&
         cfg(doc, TidyOutCharEncoding) != UTF16BE &&
         cfg(doc, TidyOutCharEncoding) != UTF16LE &&
#endif
         cfg(doc, TidyOutCharEncoding) != RAW &&
         cfgBool(doc, TidyXmlOut) )
    {
        TY_(SetOptionBool)( doc, TidyXmlDecl, yes );
    }

    /* XML requires end tags */
    if ( cfgBool(doc, TidyXmlOut) )
    {
#if SUPPORT_UTF16_ENCODINGS
        /* XML requires a BOM on output if using UTF-16 encoding */
        ulong enc = cfg( doc, TidyOutCharEncoding );
        if ( enc == UTF16LE || enc == UTF16BE || enc == UTF16 )
            TY_(SetOptionInt)( doc, TidyOutputBOM, yes );
#endif
        TY_(SetOptionBool)( doc, TidyQuoteAmpersand, yes );
        TY_(SetOptionBool)( doc, TidyOmitOptionalTags, no );
    }
}

/* unsigned integers */
Bool ParseInt( TidyDocImpl* doc, const TidyOptionImpl* entry )
{
    ulong number = 0;
    Bool digits = no;
    TidyConfigImpl* cfg = &doc->config;
    tchar c = SkipWhite( cfg );

    while ( TY_(IsDigit)(c) )
    {
        number = c - '0' + (10 * number);
        digits = yes;
        c = AdvanceChar( cfg );
    }

    if ( !digits )
        TY_(ReportBadArgument)( doc, entry->name );
    else
        TY_(SetOptionInt)( doc, entry->id, number );
    return digits;
}

/* a string excluding whitespace */
Bool FUNC_UNUSED ParseName( TidyDocImpl* doc, const TidyOptionImpl* option )
{
    tmbchar buf[ 1024 ] = {0};
    uint i = 0;
    uint c = SkipWhite( &doc->config );

    while ( i < sizeof(buf)-2 && c != EndOfStream && !TY_(IsWhite)(c) )
    {
        buf[i++] = (tmbchar) c;
        c = AdvanceChar( &doc->config );
    }
    buf[i] = 0;

    if ( i == 0 )
        TY_(ReportBadArgument)( doc, option->name );
    else
        SetOptionValue( doc, option->id, buf );
    return ( i > 0 );
}

/* #508936 - CSS class naming for -clean option */
Bool ParseCSS1Selector( TidyDocImpl* doc, const TidyOptionImpl* option )
{
    char buf[256] = {0};
    uint i = 0;
    uint c = SkipWhite( &doc->config );

    while ( i < sizeof(buf)-2 && c != EndOfStream && !TY_(IsWhite)(c) )
    {
        buf[i++] = (tmbchar) c;
        c = AdvanceChar( &doc->config );
    }
    buf[i] = '\0';

    if ( i == 0 ) {
        return no;
    }
    else if ( !TY_(IsCSS1Selector)(buf) ) {
        TY_(ReportBadArgument)( doc, option->name );
        return no;
    }

    buf[i++] = '-';  /* Make sure any escaped Unicode is terminated */
    buf[i] = 0;      /* so valid class names are generated after */
                     /* Tidy appends last digits. */

    SetOptionValue( doc, option->id, buf );
    return yes;
}

/* A general parser for anything using pick lists. This provides the engine to
   determine the proper option value, and can be used by parsers in addition to
   ParsePickList that require special handling.
 */
Bool GetParsePickListValue( TidyDocImpl* doc, const TidyOptionImpl* entry, uint *result )
{
    TidyConfigImpl* cfg = &doc->config;
    tchar c = SkipWhite( cfg );
    tmbchar work[ 16 ] = {0};
    tmbstr cp = work, end = work + sizeof(work);
    const PickListItem *item = NULL;
    uint ix = 0;
    
    while ( c!=EndOfStream && cp < end && !TY_(IsWhite)(c) && c != '\r' && c != '\n' )
    {
        *cp++ = (tmbchar) c;
        c = AdvanceChar( cfg );
    }
    
    while ( (item = &(*entry->pickList)[ ix ]) && item->label )
    {
        ctmbstr input;
        uint i = 0;
        while ( ( input = &(*item->inputs[i]) ) )
        {
            if (TY_(tmbstrcasecmp)(work, input) == 0 )
            {
                *result = ix;
                return yes;
            }
            ++i;
        }
        ++ix;
    }
    
    TY_(ReportBadArgument)( doc, entry->name );
    return no;
}


/* A general parser for anything using pick lists that don't require special
   handling.
 */
Bool ParsePickList( TidyDocImpl* doc, const TidyOptionImpl* entry )
{
    uint value;
    
    if ( GetParsePickListValue( doc, entry, &value ) )
    {
        if ( entry->type == TidyBoolean )
            TY_(SetOptionBool)( doc, entry->id, value );
        else if ( entry->type == TidyInteger )
            TY_(SetOptionInt)( doc, entry->id, value );
        return yes;
    }
  
    TY_(ReportBadArgument)( doc, entry->name );
    return no;
}


/*\
 * 20150515 - support using tabs instead of spaces - Issue #108
 * Sets the indent character to a tab if on, and set indent space count to 1
 * and sets indent character to a space if off.
 \*/
Bool ParseTabs( TidyDocImpl* doc, const TidyOptionImpl* entry )
{
    uint flag = 0;
    Bool status = GetParsePickListValue( doc, entry, &flag );
    
    if ( status ) {
        Bool tabs = flag != 0 ? yes : no;
        TY_(SetOptionBool)( doc, entry->id, tabs );
        if (tabs) {
            TY_(SetOptionInt)( doc, TidyIndentSpaces, 1 );
        } else {
            /* optional - TY_(ResetOptionToDefault)( doc, TidyIndentSpaces ); */
        }
    }
    return status;
}


/* Coordinates Config update and Tags data */
void TY_(DeclareUserTag)( TidyDocImpl* doc, TidyOptionId optId,
                            UserTagType tagType, ctmbstr name )
{
  ctmbstr prvval = cfgStr( doc, optId );
  tmbstr catval = NULL;
  ctmbstr theval = name;
  if ( prvval )
  {
    uint len = TY_(tmbstrlen)(name) + TY_(tmbstrlen)(prvval) + 3;
    catval = TY_(tmbstrndup)( doc->allocator, prvval, len );
    TY_(tmbstrcat)( catval, ", " );
    TY_(tmbstrcat)( catval, name );
    theval = catval;
  }
  TY_(DefineTag)( doc, tagType, name );
  SetOptionValue( doc, optId, theval );
  if ( catval )
    TidyDocFree( doc, catval );
}

/* a space or comma separated list of tag names */
Bool ParseTagNames( TidyDocImpl* doc, const TidyOptionImpl* option )
{
    TidyConfigImpl* cfg = &doc->config;
    tmbchar buf[1024];
    uint i = 0, nTags = 0;
    uint c = SkipWhite( cfg );
    UserTagType ttyp = tagtype_null;

    switch ( option->id )
    {
        case TidyInlineTags:  ttyp = tagtype_inline;              break;
        case TidyBlockTags:   ttyp = tagtype_block;               break;
        case TidyEmptyTags:   ttyp = tagtype_empty;               break;
        case TidyPreTags:     ttyp = tagtype_pre;                 break;
        case TidyCustomTags:  ttyp = cfg(doc, TidyUseCustomTags); break;
        default:
            TY_(ReportUnknownOption)( doc, option->name );
            return no;
    }

    SetOptionValue( doc, option->id, NULL );
    TY_(FreeDeclaredTags)( doc, ttyp );
    cfg->defined_tags |= ttyp;

    do
    {
        if (c == ' ' || c == '\t' || c == ',')
        {
            c = AdvanceChar( cfg );
            continue;
        }

        if ( c == '\r' || c == '\n' )
        {
            uint c2 = AdvanceChar( cfg );
            if ( c == '\r' && c2 == '\n' )
                c = AdvanceChar( cfg );
            else
                c = c2;

            if ( !TY_(IsWhite)(c) )
            {
                buf[i] = 0;
                TY_(UngetChar)( c, cfg->cfgIn );
                TY_(UngetChar)( '\n', cfg->cfgIn );
                break;
            }
        }

        /*
        if ( c == '\n' )
        {
            c = AdvanceChar( cfg );
            if ( !TY_(IsWhite)(c) )
            {
                buf[i] = 0;
                TY_(UngetChar)( c, cfg->cfgIn );
                TY_(UngetChar)( '\n', cfg->cfgIn );
                break;
            }
        }
        */

        while ( i < sizeof(buf)-2 && c != EndOfStream && !TY_(IsWhite)(c) && c != ',' )
        {
            buf[i++] = (tmbchar) c;
            c = AdvanceChar( cfg );
        }

        buf[i] = '\0';
        if (i == 0)          /* Skip empty tag definition.  Possible when */
            continue;        /* there is a trailing space on the line. */
            
        /* add tag to dictionary */
        TY_(DeclareUserTag)( doc, option->id, ttyp, buf );
        i = 0;
        ++nTags;
    }
    while ( c != EndOfStream );

    if ( i > 0 )
      TY_(DeclareUserTag)( doc, option->id, ttyp, buf );
    return ( nTags > 0 );
}

/* a string including whitespace */
/* munges whitespace sequences */

Bool ParseString( TidyDocImpl* doc, const TidyOptionImpl* option )
{
    TidyConfigImpl* cfg = &doc->config;
    tmbchar buf[8192];
    uint i = 0;
    tchar delim = 0;
    Bool waswhite = yes;

    tchar c = SkipWhite( cfg );

    if ( c == '"' || c == '\'' )
    {
        delim = c;
        c = AdvanceChar( cfg );
    }

    while ( i < sizeof(buf)-2 && c != EndOfStream && c != '\r' && c != '\n' )
    {
        if ( delim && c == delim )
            break;

        if ( TY_(IsWhite)(c) )
        {
            if ( waswhite )
            {
                c = AdvanceChar( cfg );
                continue;
            }
            c = ' ';
        }
        else
            waswhite = no;

        buf[i++] = (tmbchar) c;
        c = AdvanceChar( cfg );
    }
    buf[i] = '\0';

    SetOptionValue( doc, option->id, buf );
    return yes;
}

Bool ParseCharEnc( TidyDocImpl* doc, const TidyOptionImpl* option )
{
    tmbchar buf[64] = {0};
    uint i = 0;
    int enc = ASCII;
    Bool validEncoding = yes;
    tchar c = SkipWhite( &doc->config );

    while ( i < sizeof(buf)-2 && c != EndOfStream && !TY_(IsWhite)(c) )
    {
        buf[i++] = (tmbchar) TY_(ToLower)( c );
        c = AdvanceChar( &doc->config );
    }
    buf[i] = 0;

    enc = TY_(CharEncodingId)( doc, buf );

#ifdef TIDY_WIN32_MLANG_SUPPORT
    /* limit support to --input-encoding */
    if (option->id != TidyInCharEncoding && enc > WIN32MLANG)
        enc = -1;
#endif

    if ( enc < 0 )
    {
        validEncoding = no;
        TY_(ReportBadArgument)( doc, option->name );
    }
    else
        TY_(SetOptionInt)( doc, option->id, enc );

    if ( validEncoding && option->id == TidyCharEncoding )
        TY_(AdjustCharEncoding)( doc, enc );
    return validEncoding;
}


int TY_(CharEncodingId)( TidyDocImpl* ARG_UNUSED(doc), ctmbstr charenc )
{
    int enc = TY_(GetCharEncodingFromOptName)( charenc );

#ifdef TIDY_WIN32_MLANG_SUPPORT
    if (enc == -1)
    {
        uint wincp = TY_(Win32MLangGetCPFromName)(doc->allocator, charenc);
        if (wincp)
            enc = wincp;
    }
#endif

    return enc;
}

ctmbstr TY_(CharEncodingName)( int encoding )
{
    ctmbstr encodingName = TY_(GetEncodingNameFromTidyId)(encoding);

    if (!encodingName)
        encodingName = "unknown";

    return encodingName;
}

ctmbstr TY_(CharEncodingOptName)( int encoding )
{
    ctmbstr encodingName = TY_(GetEncodingOptNameFromTidyId)(encoding);

    if (!encodingName)
        encodingName = "unknown";

    return encodingName;
}

/*
   doctype: html5 | omit | auto | strict | loose | <fpi>

   where the fpi is a string similar to

      "-//ACME//DTD HTML 3.14159//EN"
*/
Bool ParseDocType( TidyDocImpl* doc, const TidyOptionImpl* option )
{
    Bool status = yes;
    uint value;
    TidyConfigImpl* cfg = &doc->config;
    tchar c = SkipWhite( cfg );

    /* "-//ACME//DTD HTML 3.14159//EN" or similar */

    if ( c == '"' || c == '\'' )
    {
        status = ParseString(doc, option);
        if (status)
        {
            TY_(SetOptionInt)( doc, TidyDoctypeMode, TidyDoctypeUser );
        }
        return status;
    }
    
    if ( (status = GetParsePickListValue( doc, option, &value ) ) )
    {
        TY_(SetOptionInt)( doc, TidyDoctypeMode, value );
    }
    else
    {
        TY_(ReportBadArgument)( doc, option->name );
    }
    
    return status;
}

/* Use TidyOptionId as iterator.
** Send index of 1st option after TidyOptionUnknown as start of list.
*/
TidyIterator TY_(getOptionList)( TidyDocImpl* ARG_UNUSED(doc) )
{
    return (TidyIterator) (size_t)1;
}

/* Check if this item is last valid option.
** If so, zero out iterator.
*/
const TidyOptionImpl*  TY_(getNextOption)( TidyDocImpl* ARG_UNUSED(doc),
                                           TidyIterator* iter )
{
    const TidyOptionImpl* option = NULL;
    size_t optId;
    assert( iter != NULL );
    optId = (size_t) *iter;
    if ( optId > TidyUnknownOption && optId < N_TIDY_OPTIONS )
    {
        option = &option_defs[ optId ];
        optId++;
    }
    *iter = (TidyIterator) ( optId < N_TIDY_OPTIONS ? optId : (size_t)0 );
    return option;
}

/* Use a 1-based array index as iterator: 0 == end-of-list
*/
TidyIterator TY_(getOptionPickList)( const TidyOptionImpl* option )
{
    size_t ix = 0;
    if ( option && option->pickList )
        ix = 1;
    return (TidyIterator) ix;
}

ctmbstr      TY_(getNextOptionPick)( const TidyOptionImpl* option,
                                     TidyIterator* iter )
{
    size_t ix;
    ctmbstr val = NULL;
    const PickListItem *item= NULL;
    assert( option!=NULL && iter != NULL );

    ix = (size_t) *iter;
    
    if ( option->pickList )
    {
        if ( ix > 0 && ix < TIDY_PL_SIZE && option->pickList )
        {
            item = &(*option->pickList)[ ix-1 ];
            val = item->label;
        }
        item = &(*option->pickList)[ ix ];
        *iter = (TidyIterator) ( val && item->label ? ix + 1 : (size_t)0 );
    }
    
    return val;
}

static int  WriteOptionString( const TidyOptionImpl* option,
                               ctmbstr sval, StreamOut* out )
{
  ctmbstr cp = option->name;
  while ( *cp )
      TY_(WriteChar)( *cp++, out );
  TY_(WriteChar)( ':', out );
  TY_(WriteChar)( ' ', out );
  cp = sval;
  while ( *cp )
      TY_(WriteChar)( *cp++, out );
  TY_(WriteChar)( '\n', out );
  return 0;
}

static int  WriteOptionInt( const TidyOptionImpl* option, uint ival, StreamOut* out )
{
  tmbchar sval[ 32 ] = {0};
  TY_(tmbsnprintf)(sval, sizeof(sval), "%u", ival );
  return WriteOptionString( option, sval, out );
}

static int  WriteOptionBool( const TidyOptionImpl* option, Bool bval, StreamOut* out )
{
  ctmbstr sval = bval ? "yes" : "no";
  return WriteOptionString( option, sval, out );
}

static int  WriteOptionPick( const TidyOptionImpl* option, uint ival, StreamOut* out )
{
    uint ix = 0;
    const PickListItem *item = NULL;
    
    if ( option-> pickList )
    {
        while ( (item = &(*option->pickList)[ ix ]) && item->label && ix<ival )
        {
            ++ix;
        }
        if ( ix==ival && item->label )
            return WriteOptionString( option, item->label, out );
    }
    
    return -1;
}

Bool  TY_(ConfigDiffThanSnapshot)( TidyDocImpl* doc )
{
  int diff = memcmp( &doc->config.value, &doc->config.snapshot,
                     N_TIDY_OPTIONS * sizeof(uint) );
  return ( diff != 0 );
}

Bool  TY_(ConfigDiffThanDefault)( TidyDocImpl* doc )
{
  Bool diff = no;
  const TidyOptionImpl* option = option_defs + 1;
  const TidyOptionValue* val = doc->config.value;
  for ( /**/; !diff && option && option->name; ++option, ++val )
  {
      diff = !OptionValueEqDefault( option, val );
  }
  return diff;
}


static int  SaveConfigToStream( TidyDocImpl* doc, StreamOut* out )
{
    int rc = 0;
    const TidyOptionImpl* option;
    for ( option=option_defs+1; 0==rc && option && option->name; ++option )
    {
        const TidyOptionValue* val = &doc->config.value[ option->id ];
        if ( option->parser == NULL )
            continue;
        if ( OptionValueEqDefault( option, val ) && option->id != TidyDoctype)
            continue;

        if ( option->id == TidyDoctype )  /* Special case */
        {
          ulong dtmode = cfg( doc, TidyDoctypeMode );
          if ( dtmode == TidyDoctypeUser )
          {
            tmbstr t;
            
            /* add 2 double quotes */
            if (( t = (tmbstr)TidyDocAlloc( doc, TY_(tmbstrlen)( val->p ) + 2 ) ))
            {
              t[0] = '\"'; t[1] = 0;
            
              TY_(tmbstrcat)( t, val->p );
              TY_(tmbstrcat)( t, "\"" );
              rc = WriteOptionString( option, t, out );
            
              TidyDocFree( doc, t );
            }
          }
          else if ( dtmode == option_defs[TidyDoctypeMode].dflt )
            continue;
          else
            rc = WriteOptionPick( option, dtmode, out );
        }
        else if ( option->pickList)
          rc = WriteOptionPick( option, val->v, out );
        else
        {
          switch ( option->type )
          {
          case TidyString:
            rc = WriteOptionString( option, val->p, out );
            break;
          case TidyInteger:
            rc = WriteOptionInt( option, val->v, out );
            break;
          case TidyBoolean:
            rc = WriteOptionBool( option, val->v ? yes : no, out );
            break;
          }
        }
    }
    return rc;
}

int  TY_(SaveConfigFile)( TidyDocImpl* doc, ctmbstr cfgfil )
{
    int status = -1;
    StreamOut* out = NULL;
    uint outenc = cfg( doc, TidyOutCharEncoding );
    uint nl = cfg( doc, TidyNewline );
    FILE* fout = fopen( cfgfil, "wb" );
    if ( fout )
    {
        out = TY_(FileOutput)( doc, fout, outenc, nl );
        status = SaveConfigToStream( doc, out );
        fclose( fout );
        TidyDocFree( doc, out );
    }
    return status;
}

int  TY_(SaveConfigSink)( TidyDocImpl* doc, TidyOutputSink* sink )
{
    uint outenc = cfg( doc, TidyOutCharEncoding );
    uint nl = cfg( doc, TidyNewline );
    StreamOut* out = TY_(UserOutput)( doc, sink, outenc, nl );
    int status = SaveConfigToStream( doc, out );
    TidyDocFree( doc, out );
    return status;
}

/*
 * local variables:
 * mode: c
 * indent-tabs-mode: nil
 * c-basic-offset: 4
 * eval: (c-set-offset 'substatement-open 0)
 * end:
 */
