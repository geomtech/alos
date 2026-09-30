#ifndef _INTTYPES_H
#define _INTTYPES_H

#include <stdint.h>

/* ABI LP64 : int64_t, intmax_t et intptr_t sont des long. */
#define PRId8 "d"
#define PRIi8 "i"
#define PRIo8 "o"
#define PRIu8 "u"
#define PRIx8 "x"
#define PRIX8 "X"
#define PRId16 PRId8
#define PRIi16 PRIi8
#define PRIo16 PRIo8
#define PRIu16 PRIu8
#define PRIx16 PRIx8
#define PRIX16 PRIX8
#define PRId32 PRId8
#define PRIi32 PRIi8
#define PRIo32 PRIo8
#define PRIu32 PRIu8
#define PRIx32 PRIx8
#define PRIX32 PRIX8
#define PRId64 "ld"
#define PRIi64 "li"
#define PRIo64 "lo"
#define PRIu64 "lu"
#define PRIx64 "lx"
#define PRIX64 "lX"
#define PRIdMAX PRId64
#define PRIiMAX PRIi64
#define PRIoMAX PRIo64
#define PRIuMAX PRIu64
#define PRIxMAX PRIx64
#define PRIXMAX PRIX64
#define PRIdPTR PRId64
#define PRIiPTR PRIi64
#define PRIoPTR PRIo64
#define PRIuPTR PRIu64
#define PRIxPTR PRIx64
#define PRIXPTR PRIX64

#define PRIdLEAST8 PRId8
#define PRIiLEAST8 PRIi8
#define PRIoLEAST8 PRIo8
#define PRIuLEAST8 PRIu8
#define PRIxLEAST8 PRIx8
#define PRIXLEAST8 PRIX8
#define PRIdLEAST16 PRId16
#define PRIiLEAST16 PRIi16
#define PRIoLEAST16 PRIo16
#define PRIuLEAST16 PRIu16
#define PRIxLEAST16 PRIx16
#define PRIXLEAST16 PRIX16
#define PRIdLEAST32 PRId32
#define PRIiLEAST32 PRIi32
#define PRIoLEAST32 PRIo32
#define PRIuLEAST32 PRIu32
#define PRIxLEAST32 PRIx32
#define PRIXLEAST32 PRIX32
#define PRIdLEAST64 PRId64
#define PRIiLEAST64 PRIi64
#define PRIoLEAST64 PRIo64
#define PRIuLEAST64 PRIu64
#define PRIxLEAST64 PRIx64
#define PRIXLEAST64 PRIX64
#define PRIdFAST8 PRId8
#define PRIiFAST8 PRIi8
#define PRIoFAST8 PRIo8
#define PRIuFAST8 PRIu8
#define PRIxFAST8 PRIx8
#define PRIXFAST8 PRIX8
#define PRIdFAST16 PRId64
#define PRIiFAST16 PRIi64
#define PRIoFAST16 PRIo64
#define PRIuFAST16 PRIu64
#define PRIxFAST16 PRIx64
#define PRIXFAST16 PRIX64
#define PRIdFAST32 PRId64
#define PRIiFAST32 PRIi64
#define PRIoFAST32 PRIo64
#define PRIuFAST32 PRIu64
#define PRIxFAST32 PRIx64
#define PRIXFAST32 PRIX64
#define PRIdFAST64 PRId64
#define PRIiFAST64 PRIi64
#define PRIoFAST64 PRIo64
#define PRIuFAST64 PRIu64
#define PRIxFAST64 PRIx64
#define PRIXFAST64 PRIX64

#endif
