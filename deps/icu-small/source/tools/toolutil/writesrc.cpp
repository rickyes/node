// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 2005-2012, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  writesrc.c
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2005apr23
*   created by: Markus W. Scherer
*
*   Helper functions for writing source code for data.
*/

#include <stdio.h>
#include <time.h>

// The C99 standard suggested that C++ implementations not define PRId64 etc. constants
// unless this macro is defined.
// See the Notes at https://en.cppreference.com/w/cpp/types/integer .
// Similar to defining __STDC_LIMIT_MACROS in unicode/ptypes.h .
#ifndef __STDC_FORMAT_MACROS
#   define __STDC_FORMAT_MACROS
#endif
#include <cinttypes>

#include "unicode/utypes.h"
#include "unicode/putil.h"
#include "unicode/ucptrie.h"
#include "unicode/errorcode.h"
#include "unicode/uniset.h"
#include "unicode/usetiter.h"
#include "unicode/utf16.h"
#include "utrie2.h"
#include "cstring.h"
#include "writesrc.h"
#include "util.h"

U_NAMESPACE_BEGIN

ValueNameGetter::~ValueNameGetter() {}

U_NAMESPACE_END

U_NAMESPACE_USE

static FILE *
usrc_createWithoutHeader(const char *path, const char *filename) {
    char buffer[1024];
    const char *p;
    char *q;
    FILE *f;
    char c;

    if(path==nullptr) {
        p=filename;
    } else {
        /* concatenate path and filename, with U_FILE_SEP_CHAR in between if necessary */
        uprv_strcpy(buffer, path);
        q=buffer+uprv_strlen(buffer);
        if(q>buffer && (c=*(q-1))!=U_FILE_SEP_CHAR && c!=U_FILE_ALT_SEP_CHAR) {
            *q++=U_FILE_SEP_CHAR;
        }
        uprv_strcpy(q, filename);
        p=buffer;
    }

    f=fopen(p, "w");
    if (f==nullptr) {
        fprintf(
            stderr,
            "usrc_create(%s, %s): unable to create file\n",
            path!=nullptr ? path : "", filename);
    }
    return f;
}

U_CAPI FILE * U_EXPORT2
usrc_create(const char *path, const char *filename, int32_t copyrightYear, const char *generator) {
    FILE *f = usrc_createWithoutHeader(path, filename);
    if (f == nullptr) {
        return f;
    }
    usrc_writeCopyrightHeader(f, "//", copyrightYear);
    usrc_writeFileNameGeneratedBy(f, "//", filename, generator);
    return f;
}

U_CAPI FILE * U_EXPORT2
usrc_createTextData(const char *path, const char *filename, int32_t copyrightYear, const char *generator) {
    FILE *f = usrc_createWithoutHeader(path, filename);
    if (f == nullptr) {
        return f;
    }
    usrc_writeCopyrightHeader(f, "#", copyrightYear);
    usrc_writeFileNameGeneratedBy(f, "#", filename, generator);
    return f;
}

U_CAPI void U_EXPORT2
usrc_writeCopyrightHeader(FILE *f, const char *prefix, int32_t copyrightYear) {
    fprintf(f,
        "%s Copyright (C) %d and later: Unicode, Inc. and others.\n"
        "%s License & terms of use: http://www.unicode.org/copyright.html\n",
        prefix, copyrightYear, prefix);
    if (copyrightYear <= 2016) {
        fprintf(f,
            "%s Copyright (C) 1999-2016, International Business Machines\n"
            "%s Corporation and others.  All Rights Reserved.\n",
            prefix, prefix);
    }
}

U_CAPI void U_EXPORT2
usrc_writeFileNameGeneratedBy(
        FILE *f,
        const char *prefix,
        const char *filename,
        const char *generator) {
    char buffer[1024];
    const struct tm *lt;
    time_t t;

    const char *pattern = 
        "%s\n"
        "%s file name: %s\n"
        "%s\n"
        "%s machine-generated by: %s\n"
        "\n";

    time(&t);
    lt=localtime(&t);
    if(generator==nullptr) {
        strftime(buffer, sizeof(buffer), "%Y-%m-%d", lt);
        fprintf(f, pattern, prefix, prefix, filename, prefix, prefix, buffer);
    } else {
        fprintf(f, pattern, prefix, prefix, filename, prefix, prefix, generator);
    }
}

U_CAPI void U_EXPORT2
usrc_writeArray(FILE *f,
                const char *prefix,
                const void *p, int32_t width, int32_t length,
                const char *indent,
                const char *postfix) {
    const uint8_t *p8;
    const uint16_t *p16;
    const uint32_t *p32;
    const int64_t *p64; // Signed due to TOML!
    int64_t value; // Signed due to TOML!
    int32_t i, col;

    p8=nullptr;
    p16=nullptr;
    p32=nullptr;
    p64=nullptr;
    switch(width) {
    case 8:
        p8=(const uint8_t *)p;
        break;
    case 16:
        p16=(const uint16_t *)p;
        break;
    case 32:
        p32=(const uint32_t *)p;
        break;
    case 64:
        p64=(const int64_t *)p;
        break;
    default:
        fprintf(stderr, "usrc_writeArray(width=%ld) unrecognized width\n", (long)width);
        return;
    }
    if(prefix!=nullptr) {
        fprintf(f, prefix, (long)length);
    }
    for(i=col=0; i<length; ++i, ++col) {
        if(i>0) {
            if(col<16) {
                fputc(',', f);
            } else {
                fputs(",\n", f);
                fputs(indent, f);
                col=0;
            }
        }
        switch(width) {
        case 8:
            value=p8[i];
            break;
        case 16:
            value=p16[i];
            break;
        case 32:
            value=p32[i];
            break;
        case 64:
            value=p64[i];
            break;
        default:
            value=0; /* unreachable */
            break;
        }
        fprintf(f, value<=9 ? "%" PRId64 : "0x%" PRIx64, value);
    }
    if(postfix!=nullptr) {
        fputs(postfix, f);
    }
}

U_CAPI void U_EXPORT2
usrc_writeUTrie2Arrays(FILE *f,
                       const char *indexPrefix, const char *data32Prefix,
                       const UTrie2 *pTrie,
                       const char *postfix) {
    if(pTrie->data32==nullptr) {
        /* 16-bit trie */
        usrc_writeArray(f, indexPrefix, pTrie->index, 16, pTrie->indexLength+pTrie->dataLength, "", postfix);
    } else {
        /* 32-bit trie */
        usrc_writeArray(f, indexPrefix, pTrie->index, 16, pTrie->indexLength, "", postfix);
        usrc_writeArray(f, data32Prefix, pTrie->data32, 32, pTrie->dataLength, "", postfix);
    }
}

U_CAPI void U_EXPORT2
usrc_writeUTrie2Struct(FILE *f,
                       const char *prefix,
                       const UTrie2 *pTrie,
                       const char *indexName, const char *data32Name,
                       const char *postfix) {
    if(prefix!=nullptr) {
        fputs(prefix, f);
    }
    if(pTrie->data32==nullptr) {
        /* 16-bit trie */
        fprintf(
            f,
            "    %s,\n"         /* index */
            "    %s+%ld,\n"     /* data16 */
            "    nullptr,\n",      /* data32 */
            indexName,
            indexName, 
            (long)pTrie->indexLength);
    } else {
        /* 32-bit trie */
        fprintf(
            f,
            "    %s,\n"         /* index */
            "    nullptr,\n"       /* data16 */
            "    %s,\n",        /* data32 */
            indexName,
            data32Name);
    }
    fprintf(
        f,
        "    %ld,\n"            /* indexLength */
        "    %ld,\n"            /* dataLength */
        "    0x%hx,\n"          /* index2NullOffset */
        "    0x%hx,\n"          /* dataNullOffset */
        "    0x%lx,\n"          /* initialValue */
        "    0x%lx,\n"          /* errorValue */
        "    0x%lx,\n"          /* highStart */
        "    0x%lx,\n"          /* highValueIndex */
        "    nullptr, 0, false, false, 0, nullptr\n",
        (long)pTrie->indexLength, (long)pTrie->dataLength,
        (short)pTrie->index2NullOffset, (short)pTrie->dataNullOffset,
        (long)pTrie->initialValue, (long)pTrie->errorValue,
        (long)pTrie->highStart, (long)pTrie->highValueIndex);
    if(postfix!=nullptr) {
        fputs(postfix, f);
    }
}

U_CAPI void U_EXPORT2
usrc_writeUCPTrieArrays(FILE *f,
                        const char *indexPrefix, const char *dataPrefix,
                        const UCPTrie *pTrie,
                        const char *postfix,
                        UTargetSyntax syntax) {
    const char* indent = (syntax == UPRV_TARGET_SYNTAX_TOML) ? "  " : "";
    usrc_writeArray(f, indexPrefix, pTrie->index, 16, pTrie->indexLength, indent, postfix);
    int32_t width=
        pTrie->valueWidth==UCPTRIE_VALUE_BITS_16 ? 16 :
        pTrie->valueWidth==UCPTRIE_VALUE_BITS_32 ? 32 :
        pTrie->valueWidth==UCPTRIE_VALUE_BITS_8 ? 8 : 0;
    usrc_writeArray(f, dataPrefix, pTrie->data.ptr0, width, pTrie->dataLength, indent, postfix);
}

U_CAPI void U_EXPORT2
usrc_writeUCPTrieStruct(FILE *f,
                        const char *prefix,
                        const UCPTrie *pTrie,
                        const char *indexName, const char *dataName,
                        const char *postfix,
                        UTargetSyntax syntax) {
    if(prefix!=nullptr) {
        fputs(prefix, f);
    }
    if (syntax == UPRV_TARGET_SYNTAX_CCODE) {
        fprintf(
            f,
            "    %s,\n"             // index
            "    { %s },\n",        // data (union)
            indexName,
            dataName);
    }
    const char* pattern =
        (syntax == UPRV_TARGET_SYNTAX_CCODE) ?
        "    %ld, %ld,\n"       // indexLength, dataLength
        "    0x%lx, 0x%x,\n"    // highStart, shifted12HighStart
        "    %d, %d,\n"         // type, valueWidth
        "    0, 0,\n"           // reserved32, reserved16
        "    0x%x, 0x%lx,\n"    // index3NullOffset, dataNullOffset
        "    0x%lx,\n"          // nullValue
        :
        "indexLength = %ld\n"
        "dataLength = %ld\n"
        "highStart = 0x%lx\n"
        "shifted12HighStart = 0x%x\n"
        "type = %d\n"
        "valueWidth = %d\n"
        "index3NullOffset = 0x%x\n"
        "dataNullOffset = 0x%lx\n"
        "nullValue = 0x%lx\n"
        ;
    fprintf(
        f,
        pattern,
        (long)pTrie->indexLength, (long)pTrie->dataLength,
        (long)pTrie->highStart, pTrie->shifted12HighStart,
        pTrie->type, pTrie->valueWidth,
        pTrie->index3NullOffset, (long)pTrie->dataNullOffset,
        (long)pTrie->nullValue);
    if(postfix!=nullptr) {
        fputs(postfix, f);
    }
}

U_CAPI void U_EXPORT2
usrc_writeUCPTrie(FILE *f, const char *name, const UCPTrie *pTrie, UTargetSyntax syntax) {
    int32_t width=
        pTrie->valueWidth==UCPTRIE_VALUE_BITS_16 ? 16 :
        pTrie->valueWidth==UCPTRIE_VALUE_BITS_32 ? 32 :
        pTrie->valueWidth==UCPTRIE_VALUE_BITS_8 ? 8 : 0;
    char line[100], line2[100], line3[100], line4[100];

    switch (syntax) {
    case UPRV_TARGET_SYNTAX_CCODE:
        snprintf(line, sizeof(line), "static const uint16_t %s_trieIndex[%%ld]={\n", name);
        snprintf(line2, sizeof(line2), "static const uint%d_t %s_trieData[%%ld]={\n", (int)width, name);
        snprintf(line3, sizeof(line3), "\n};\n\n");
        break;
    case UPRV_TARGET_SYNTAX_TOML:
        snprintf(line, sizeof(line), "index = [\n  ");
        snprintf(line2, sizeof(line2), "data_%d = [\n  ", (int)width);
        snprintf(line3, sizeof(line3), "\n]\n");
        break;
    default:
        UPRV_UNREACHABLE_EXIT;
    }
    usrc_writeUCPTrieArrays(f, line, line2, pTrie, line3, syntax);

    switch (syntax) {
    case UPRV_TARGET_SYNTAX_CCODE:
        snprintf(line, sizeof(line), "static const UCPTrie %s_trie={\n", name);
        snprintf(line2, sizeof(line2), "%s_trieIndex", name);
        snprintf(line3, sizeof(line3), "%s_trieData", name);
        snprintf(line4, sizeof(line4), "};\n\n");
        break;
    case UPRV_TARGET_SYNTAX_TOML:
        line[0] = 0;
        line2[0] = 0;
        line3[0] = 0;
        line4[0] = 0;
        break;
    default:
        UPRV_UNREACHABLE_EXIT;
    }
    usrc_writeUCPTrieStruct(f, line, pTrie, line2, line3, line4, syntax);
}

U_CAPI void U_EXPORT2
usrc_writeUnicodeSet(
        FILE *f,
        const USet *pSet,
        UTargetSyntax syntax) {
    // ccode is not yet supported
    U_ASSERT(syntax == UPRV_TARGET_SYNTAX_TOML);

    // Write out a list of ranges
    const UnicodeSet* set = UnicodeSet::fromUSet(pSet);
    UnicodeSetIterator it(*set);
    fprintf(f, "# Inclusive ranges of the code points in the set.\n");
    fprintf(f, "ranges = [\n");
    bool seenFirstString = false;
    while (it.nextRange()) {
        if (it.isString()) {
            if (!seenFirstString) {
                seenFirstString = true;
                fprintf(f, "]\nstrings = [\n");
            }
            const UnicodeString& str = it.getString();
            fprintf(f, "  ");
            usrc_writeStringAsASCII(f, str.getBuffer(), str.length(), syntax);
            fprintf(f, ",\n");
        } else {
            U_ASSERT(!seenFirstString);
            UChar32 start = it.getCodepoint();
            UChar32 end = it.getCodepointEnd();
            fprintf(f, "  [0x%x, 0x%x],\n", start, end);
        }
    }
    fprintf(f, "]\n");
}

U_CAPI void U_EXPORT2
usrc_writeUCPMap(
        FILE *f,
        const UCPMap *pMap,
        icu::ValueNameGetter *valueNameGetter,
        UTargetSyntax syntax) {
    // ccode is not yet supported
    U_ASSERT(syntax == UPRV_TARGET_SYNTAX_TOML);
    (void) syntax; // silence unused variable errors

    // Print out list of ranges
    UChar32 start = 0, end;
    uint32_t value;
    fprintf(f, "# Code points `a` through `b` have value `v`, corresponding to `name`.\n");
    fprintf(f, "ranges = [\n");
    while ((end = ucpmap_getRange(pMap, start, UCPMAP_RANGE_NORMAL, 0, nullptr, nullptr, &value)) >= 0) {
        if (valueNameGetter != nullptr) {
            const char *name = valueNameGetter->getName(value);
            fprintf(f, "  {a=0x%x, b=0x%x, v=%u, name=\"%s\"},\n", start, end, value, name);
        } else {
            fprintf(f, "  {a=0x%x, b=0x%x, v=%u},\n", start, end, value);
        }
        start = end + 1;
    }
    fprintf(f, "]\n");
}

U_CAPI void U_EXPORT2
usrc_writeArrayOfMostlyInvChars(FILE *f,
                                const char *prefix,
                                const char *p, int32_t length,
                                const char *postfix) {
    int32_t i, col;
    int prev2, prev, c;

    if(prefix!=nullptr) {
        fprintf(f, prefix, (long)length);
    }
    prev2=prev=-1;
    for(i=col=0; i<length; ++i, ++col) {
        c=(uint8_t)p[i];
        if(i>0) {
            /* Break long lines. Try to break at interesting places, to minimize revision diffs. */
            if( 
                /* Very long line. */
                col>=32 ||
                /* Long line, break after terminating NUL. */
                (col>=24 && prev2>=0x20 && prev==0) ||
                /* Medium-long line, break before non-NUL, non-character byte. */
                (col>=16 && (prev==0 || prev>=0x20) && 0<c && c<0x20)
            ) {
                fputs(",\n", f);
                col=0;
            } else {
                fputc(',', f);
            }
        }
        fprintf(f, c<0x20 ? "%u" : "'%c'", c);
        prev2=prev;
        prev=c;
    }
    if(postfix!=nullptr) {
        fputs(postfix, f);
    }
}

U_CAPI void U_EXPORT2
usrc_writeStringAsASCII(FILE *f,
        const char16_t* ptr, int32_t length,
        UTargetSyntax) {
    // For now, assume all UTargetSyntax values are valid here.
    fprintf(f, "\"");
    int32_t i = 0;
    UChar32 cp;
    while (i < length) {
        U16_NEXT(ptr, i, length, cp);
        if (cp == u'"') {
            fprintf(f, "\\\"");
        } else if (ICU_Utility::isUnprintable(cp)) {
            UnicodeString u16result;
            ICU_Utility::escapeUnprintable(u16result, cp);
            std::string u8result;
            u16result.toUTF8String(u8result);
            fprintf(f, "%s", u8result.data());
        } else {
            U_ASSERT(cp < 0x80);
            char s[2] = {static_cast<char>(cp), 0};
            fprintf(f, "%s", s);
        }
    }
    fprintf(f, "\"");
}
