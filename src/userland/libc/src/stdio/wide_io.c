#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <wchar.h>

static wint_t wide_error(FILE *stream, int error)
{
	errno=error;
	if (stream) stream->error=error;
	return WEOF;
}

wint_t fgetwc(FILE *stream)
{
	if (!stream || stream->fd<0) return wide_error(stream,EBADF);
	if (stream->has_wide_pushback) {
		stream->has_wide_pushback=0;
		return stream->wide_pushed;
	}
	mbstate_t state={0};
	for (;;) {
		int byte=fgetc(stream);
		if (byte==EOF) {
			if (!ferror(stream) && !mbsinit(&state))
				return wide_error(stream,EILSEQ);
			return WEOF;
		}
		char c=(unsigned char)byte;
		wchar_t wc;
		size_t result=mbrtowc(&wc,&c,1,&state);
		if (result==(size_t)-1) return wide_error(stream,EILSEQ);
		if (result!=(size_t)-2) return (wint_t)wc;
	}
}

wint_t getwc(FILE *stream) { return fgetwc(stream); }

wint_t fputwc(wchar_t wc, FILE *stream)
{
	if (!stream || stream->fd<0) return wide_error(stream,EBADF);
	char bytes[MB_LEN_MAX];
	mbstate_t state={0};
	size_t length=wcrtomb(bytes,wc,&state);
	if (length==(size_t)-1) return wide_error(stream,EILSEQ);
	if (fwrite(bytes,1,length,stream)!=length) return WEOF;
	return (wint_t)wc;
}

wint_t ungetwc(wint_t wc, FILE *stream)
{
	if (!stream || stream->fd<0) return wide_error(stream,EBADF);
	if (wc==WEOF) return WEOF;
	char bytes[MB_LEN_MAX];
	mbstate_t state={0};
	if (wcrtomb(bytes,(wchar_t)wc,&state)==(size_t)-1) {
		errno=EILSEQ;
		return WEOF;
	}
	if (stream->has_pushback || stream->has_wide_pushback) {
		errno=ENOSPC;
		return WEOF;
	}
	stream->wide_pushed=wc;
	stream->has_wide_pushback=1;
	stream->eof=0;
	return wc;
}
