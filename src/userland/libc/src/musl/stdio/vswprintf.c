/* Parseur derive de musl 1.2.6 src/stdio/vfwprintf.c (MIT, voir
 * COPYRIGHT.musl). Adaptation ALOS : puits wchar_t direct, conversions
 * numeriques ASCII deleguees au moteur snprintf existant. */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define ALT_FORM (1U << ('#'-' '))
#define ZERO_PAD (1U << ('0'-' '))
#define LEFT_ADJ (1U << ('-'-' '))
#define PAD_POS  (1U << (' '-' '))
#define MARK_POS (1U << ('+'-' '))
#define GROUPED  (1U << ('\''-' '))
#define FLAGMASK (ALT_FORM|ZERO_PAD|LEFT_ADJ|PAD_POS|MARK_POS|GROUPED)

enum {
	BARE, LPRE, LLPRE, HPRE, HHPRE, BIGLPRE, ZTPRE, JPRE, STOP,
	PTR, INT, UINT, ULLONG, LONG, ULONG, SHORT, USHORT, CHAR, UCHAR,
	LLONG, SIZET, IMAX, UMAX, PDIFF, UIPTR, DBL, LDBL, NOARG
};
#define S(x) [(x)-'A']
static const unsigned char states[]['z'-'A'+1] = {
	{
		S('d')=INT, S('i')=INT, S('o')=UINT, S('u')=UINT,
		S('x')=UINT, S('X')=UINT,
		S('e')=DBL, S('f')=DBL, S('g')=DBL, S('a')=DBL,
		S('E')=DBL, S('F')=DBL, S('G')=DBL, S('A')=DBL,
		S('c')=INT, S('C')=UINT, S('s')=PTR, S('S')=PTR,
		S('p')=UIPTR, S('n')=PTR, S('m')=NOARG,
		S('l')=LPRE, S('h')=HPRE, S('L')=BIGLPRE,
		S('z')=ZTPRE, S('j')=JPRE, S('t')=ZTPRE,
	}, {
		S('d')=LONG, S('i')=LONG, S('o')=ULONG, S('u')=ULONG,
		S('x')=ULONG, S('X')=ULONG,
		S('e')=DBL, S('f')=DBL, S('g')=DBL, S('a')=DBL,
		S('E')=DBL, S('F')=DBL, S('G')=DBL, S('A')=DBL,
		S('c')=UINT, S('s')=PTR, S('n')=PTR, S('l')=LLPRE,
	}, {
		S('d')=LLONG, S('i')=LLONG, S('o')=ULLONG, S('u')=ULLONG,
		S('x')=ULLONG, S('X')=ULLONG, S('n')=PTR,
	}, {
		S('d')=SHORT, S('i')=SHORT, S('o')=USHORT, S('u')=USHORT,
		S('x')=USHORT, S('X')=USHORT, S('n')=PTR, S('h')=HHPRE,
	}, {
		S('d')=CHAR, S('i')=CHAR, S('o')=UCHAR, S('u')=UCHAR,
		S('x')=UCHAR, S('X')=UCHAR, S('n')=PTR,
	}, {
		S('e')=LDBL, S('f')=LDBL, S('g')=LDBL, S('a')=LDBL,
		S('E')=LDBL, S('F')=LDBL, S('G')=LDBL, S('A')=LDBL,
	}, {
		S('d')=PDIFF, S('i')=PDIFF, S('o')=SIZET, S('u')=SIZET,
		S('x')=SIZET, S('X')=SIZET, S('n')=PTR,
	}, {
		S('d')=IMAX, S('i')=IMAX, S('o')=UMAX, S('u')=UMAX,
		S('x')=UMAX, S('X')=UMAX, S('n')=PTR,
	}
};
#undef S

union arg { uintmax_t i; long double f; void *p; };
static void pop_arg(union arg *a, int type, va_list *ap)
{
	switch (type) {
	case PTR: a->p=va_arg(*ap, void *); break;
	case INT: a->i=va_arg(*ap, int); break;
	case UINT: a->i=va_arg(*ap, unsigned); break;
	case LONG: a->i=va_arg(*ap, long); break;
	case ULONG: a->i=va_arg(*ap, unsigned long); break;
	case LLONG: a->i=va_arg(*ap, long long); break;
	case ULLONG: a->i=va_arg(*ap, unsigned long long); break;
	case SHORT: a->i=(short)va_arg(*ap, int); break;
	case USHORT: a->i=(unsigned short)va_arg(*ap, int); break;
	case CHAR: a->i=(signed char)va_arg(*ap, int); break;
	case UCHAR: a->i=(unsigned char)va_arg(*ap, int); break;
	case SIZET: a->i=va_arg(*ap, size_t); break;
	case IMAX: a->i=va_arg(*ap, intmax_t); break;
	case UMAX: a->i=va_arg(*ap, uintmax_t); break;
	case PDIFF: a->i=va_arg(*ap, ptrdiff_t); break;
	case UIPTR: a->i=(uintptr_t)va_arg(*ap, void *); break;
	case DBL: a->f=va_arg(*ap, double); break;
	case LDBL: a->f=va_arg(*ap, long double); break;
	}
}

struct wide_sink { wchar_t *buffer; size_t capacity, used; };
static void out(struct wide_sink *f, const wchar_t *s, size_t n)
{
	size_t available=f->capacity-f->used;
	if (n>available) n=available;
	if (n) wmemcpy(f->buffer+f->used, s, n);
	f->used+=n;
}
static void pad(struct wide_sink *f, int n, unsigned fl)
{
	if (fl&LEFT_ADJ) return;
	size_t available=f->capacity-f->used;
	size_t copied=(size_t)n<available ? (size_t)n : available;
	wmemset(f->buffer+f->used, L' ', copied);
	f->used+=copied;
}
static int digit(wchar_t c) { return c>=L'0' && c<=L'9'; }
static int getint(const wchar_t **s)
{
	int i=0;
	for (; digit(**s); ++*s) {
		if (i>INT_MAX/10U || **s-L'0'>INT_MAX-10*i) i=-1;
		else i=10*i+(**s-L'0');
	}
	return i;
}
static int register_arg(int *types, int pos, int type, unsigned *mode)
{
	*mode |= pos<0 ? 1 : 2;
	if (*mode==3 || pos==0 || pos>NL_ARGMAX) return -1;
	if (pos>0) {
		if (types[pos] && types[pos]!=type) return -1;
		types[pos]=type;
	}
	return 0;
}

/* Seul un nombre ASCII est formate ici ; ni Unicode ni %n ne passent par
 * snprintf. La taille allouee depend de la sortie disponible, pas du champ. */
static int number(struct wide_sink *f, union arg a, int t, int w, int p, unsigned fl)
{
	char fmt[16], *s=fmt;
	*s++='%';
	if (fl&ALT_FORM) *s++='#';
	if (fl&MARK_POS) *s++='+';
	if (fl&LEFT_ADJ) *s++='-';
	if (fl&PAD_POS) *s++=' ';
	if (fl&ZERO_PAD) *s++='0';
	*s++='*'; *s++='.'; *s++='*';
	if (t!='p') *s++=((t|32)=='a' || (t|32)=='e' ||
		(t|32)=='f' || (t|32)=='g') ? 'L' : 'j';
	*s++=t; *s=0;
	int floating=(t|32)=='a' || (t|32)=='e' || (t|32)=='f' || (t|32)=='g';
	int signed_integer=t=='d' || t=='i';
	int length;
#define FORMAT(dst, size) (floating ? snprintf(dst,size,fmt,w,p,a.f) : \
	t=='p' ? snprintf(dst,size,fmt,w,p,(void *)(uintptr_t)a.i) : \
	signed_integer ? snprintf(dst,size,fmt,w,p,(intmax_t)a.i) : \
	snprintf(dst,size,fmt,w,p,a.i))
	length=FORMAT(NULL,0);
	if (length<0) return -1;
	size_t copied=f->capacity-f->used;
	if (copied>(size_t)length) copied=length;
	if (!copied) return length;
	char local[256];
	char *buffer=copied<sizeof local ? local : malloc(copied+1);
	if (!buffer) { errno=ENOMEM; return -1; }
	int result=FORMAT(buffer,copied+1);
#undef FORMAT
	if (result!=length) {
		if (buffer!=local) free(buffer);
		if (result>=0) errno=EOVERFLOW;
		return -1;
	}
	for (size_t i=0; i<copied; ++i)
		f->buffer[f->used++]=(unsigned char)buffer[i];
	if (buffer!=local) free(buffer);
	return length;
}

static int core(struct wide_sink *f, const wchar_t *fmt, va_list *ap,
	union arg *args, int *types, int saved_errno)
{
	const wchar_t *s=fmt, *a;
	unsigned mode=0;
	int count=0;
	while (*s) {
		a=s;
		while (*s && *s!='%') ++s;
		size_t literal=s-a;
		if (literal>(size_t)(INT_MAX-count)) goto overflow;
		if (f) out(f,a,literal);
		count+=(int)literal;
		if (!*s) break;
		if (s[1]=='%') {
			if (count==INT_MAX) goto overflow;
			if (f) out(f,L"%",1);
			++count; s+=2; continue;
		}
		++s;
		int pos=-1;
		if (digit(s[0]) && s[1]=='$') { pos=s[0]-'0'; s+=2; }
		unsigned fl=0;
		while ((unsigned)*s-' '<32 && (FLAGMASK&(1U<<(*s-' '))))
			fl|=1U<<(*s++-' ');
		int w, p=-1, xp=0;
		if (*s=='*') {
			++s;
			int wp=-1;
			if (digit(s[0]) && s[1]=='$') { wp=s[0]-'0'; s+=2; }
			if (register_arg(types,wp,INT,&mode)<0) goto inval;
			w=!f ? 0 : wp<0 ? va_arg(*ap,int) : (int)args[wp].i;
			if (w==INT_MIN) goto overflow;
			if (w<0) fl|=LEFT_ADJ, w=-w;
		} else if ((w=getint(&s))<0) goto overflow;
		if (*s=='.') {
			++s;
			if (*s=='*') {
				++s;
				int pp=-1;
				if (digit(s[0]) && s[1]=='$') { pp=s[0]-'0'; s+=2; }
				if (register_arg(types,pp,INT,&mode)<0) goto inval;
				p=!f ? -1 : pp<0 ? va_arg(*ap,int) : (int)args[pp].i;
				xp=p>=0;
			} else {
				p=getint(&s); xp=1;
				if (p<0) goto overflow;
			}
		}
		unsigned st=0, ps;
		do {
			if ((unsigned)*s-'A'>'z'-'A') goto inval;
			ps=st; st=states[st][*s++-'A'];
		} while (st && st<STOP);
		if (!st) goto inval;
		union arg arg={0};
		if (st==NOARG) { if (pos>=0) goto inval; }
		else {
			if (register_arg(types,pos,st,&mode)<0) goto inval;
			if (f) {
				if (pos<0) pop_arg(&arg,st,ap);
				else arg=args[pos];
			}
		}
		if (!f) continue;
		int t=s[-1];
		if (ps && (t=='c' || t=='s')) t&=~32;
		if (t=='n') {
			switch (ps) {
			case BARE: *(int *)arg.p=count; break;
			case LPRE: *(long *)arg.p=count; break;
			case LLPRE: *(long long *)arg.p=count; break;
			case HPRE: *(short *)arg.p=count; break;
			case HHPRE: *(signed char *)arg.p=count; break;
			case ZTPRE: *(ptrdiff_t *)arg.p=count; break;
			case JPRE: *(intmax_t *)arg.p=count; break;
			}
			continue;
		}
		int length;
		if (t=='c' || t=='C') {
			wchar_t wc=t=='C' ? (wchar_t)arg.i : (wchar_t)btowc((int)arg.i);
			if (t=='c' && (wint_t)wc==WEOF) { errno=EILSEQ; return -1; }
			length=w>1 ? w : 1;
			if (length>INT_MAX-count) goto overflow;
			pad(f,length-1,fl); out(f,&wc,1); pad(f,length-1,fl^LEFT_ADJ);
		} else if (t=='S') {
			a=arg.p ? arg.p : L"(null)";
			size_t n=wcsnlen(a,p<0 ? (size_t)INT_MAX : (size_t)p);
			if (p<0 && a[n]) goto overflow;
			length=w>(int)n ? w : (int)n;
			if (length>INT_MAX-count) goto overflow;
			pad(f,length-n,fl); out(f,a,n); pad(f,length-n,fl^LEFT_ADJ);
		} else if (t=='s' || t=='m') {
			const char *bs=t=='m' ? strerror(saved_errno) : arg.p;
			if (!bs) bs="(null)";
			const char *start=bs;
			wchar_t wc;
			int n=0, bytes=0;
			while (n<(p<0 ? INT_MAX : p) && *bs) {
				bytes=mbtowc(&wc,bs,MB_LEN_MAX);
				if (bytes<0) return -1;
				bs+=bytes; ++n;
			}
			if (p<0 && *bs) goto overflow;
			length=w>n ? w : n;
			if (length>INT_MAX-count) goto overflow;
			pad(f,length-n,fl);
			bs=start;
			for (int i=0; i<n; ++i) {
				bytes=mbtowc(&wc,bs,MB_LEN_MAX);
				if (bytes<0) return -1;
				bs+=bytes; out(f,&wc,1);
			}
			pad(f,length-n,fl^LEFT_ADJ);
		} else {
			length=number(f,arg,t,w,xp ? p : -1,fl);
			if (length<0) return -1;
			if (length>INT_MAX-count) goto overflow;
		}
		count+=length;
	}
	if (f) return count;
	if (mode==2) {
		int i=1;
		for (; i<=NL_ARGMAX && types[i]; ++i) pop_arg(args+i,types[i],ap);
		for (; i<=NL_ARGMAX; ++i) if (types[i]) goto inval;
	}
	return 0;
inval:
	errno=EINVAL;
	return -1;
overflow:
	errno=EOVERFLOW;
	return -1;
}

int vswprintf(wchar_t *restrict buffer, size_t size,
	const wchar_t *restrict format, va_list arguments)
{
	if (!format || (size && !buffer)) { errno=EINVAL; return -1; }
	if (!size) return -1;
	buffer[0]=0;
	struct wide_sink sink={buffer,size-1,0};
	int types[NL_ARGMAX+1]={0}, saved_errno=errno;
	union arg args[NL_ARGMAX+1];
	va_list copy;
	va_copy(copy,arguments);
	int result=core(NULL,format,&copy,args,types,saved_errno);
	if (result>=0) result=core(&sink,format,&copy,args,types,saved_errno);
	va_end(copy);
	buffer[sink.used]=0;
	if (result>=0 && (size_t)result>=size) return -1;
	return result;
}

int swprintf(wchar_t *restrict buffer, size_t size,
	const wchar_t *restrict format, ...)
{
	va_list arguments;
	va_start(arguments,format);
	int result=vswprintf(buffer,size,format,arguments);
	va_end(arguments);
	return result;
}
