#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

static int failures;
#define IO_FAIL() do { \
	fprintf(stderr,"wide-format-test: FAIL I/O line %d errno=%d\n",__LINE__,errno); \
	++failures; \
} while (0)

static void check_io(void)
{
	FILE closed={.fd=-1};
	errno=0;
	if (fputwc(L'A',&closed)!=WEOF || errno!=EBADF || !ferror(&closed))
		IO_FAIL();
	clearerr(&closed);
	errno=0;
	if (getwc(&closed)!=WEOF || errno!=EBADF || !ferror(&closed))
		IO_FAIL();
	int fd=open("/wide-io-output",O_RDWR);
	if (fd<0) { IO_FAIL(); return; }
	FILE output={.fd=fd};
	errno=0;
	if (fputwc(L'\u00e9',&output)!=0x00e9 || ferror(&output))
		IO_FAIL();
	if (close(fd)) IO_FAIL();
	fd=open("/wide-io-output",O_RDONLY);
	if (fd<0) { IO_FAIL(); return; }
	char bytes[8]={0};
	if (read(fd,bytes,8)!=7 || memcmp(bytes,"\xc3\xa9!!!!!",7)) IO_FAIL();
	FILE readonly={.fd=fd};
	errno=0;
	if (fputwc(L'A',&readonly)!=WEOF || errno!=EBADF || !ferror(&readonly))
		IO_FAIL();
	if (close(fd)) IO_FAIL();
	puts("wide-format-test: file write and readonly error PASS");
	printf("wide-format-test: console-bytes[");
	if (fputwc(L'\u00e9',stdout)!=0x00e9 ||
		fputwc(L'\U0001f600',stdout)!=0x1f600 ||
		fputwc(L'\0',stdout)!=0 || ferror(stdout)) IO_FAIL();
	puts("]console-bytes-end");
	errno=0;
	if (fputwc((wchar_t)0xd800,stdout)!=WEOF ||
		errno!=EILSEQ || !ferror(stdout)) IO_FAIL();
	clearerr(stdout);
	FILE *input=fopen("/wide-io-valid","r");
	if (!input) { IO_FAIL(); return; }
	if (getwc(input)!=0x00e9 || fgetwc(input)!=0x1f600 ||
		fgetwc(input)!=0 || getwc(input)!=WEOF ||
		!feof(input) || ferror(input)) IO_FAIL();
	errno=ERANGE;
	if (ungetwc(0x1f600,input)!=0x1f600 || errno!=ERANGE || feof(input))
		IO_FAIL();
	errno=0;
	if (ungetc('a',input)!=EOF || errno!=EINVAL ||
		getwc(input)!=0x1f600) IO_FAIL();
	if (getwc(input)!=WEOF || !feof(input)) IO_FAIL();
	if (ungetwc(WEOF,input)!=WEOF || !feof(input)) IO_FAIL();
	if (ungetwc(0xd800,input)!=WEOF || errno!=EILSEQ) IO_FAIL();
	if (ungetc('A',input)!='A' || ungetwc('B',input)!=WEOF ||
		getwc(input)!='A') IO_FAIL();
	if (ungetwc(0x00e9,input)!=0x00e9 ||
		ungetwc('B',input)!=WEOF || getwc(input)!=0x00e9) IO_FAIL();
	if (ungetwc('C',input)!='C') IO_FAIL();
	char byte;
	errno=0;
	if (fread(&byte,1,1,input)!=0 || errno!=EINVAL || !ferror(input))
		IO_FAIL();
	clearerr(input);
	if (getwc(input)!='C' || ferror(input)) IO_FAIL();
	if (fclose(input)) IO_FAIL();

	const char *invalid[]={"/wide-io-invalid","/wide-io-incomplete"};
	for (size_t i=0; i<2; ++i) {
		input=fopen(invalid[i],"r");
		if (!input) { IO_FAIL(); continue; }
		errno=0;
		if (getwc(input)!=WEOF || errno!=EILSEQ || !ferror(input))
			IO_FAIL();
		if (i==1 && !feof(input)) IO_FAIL();
		if (fclose(input)) IO_FAIL();
	}
}

static void check(const wchar_t *expected, const wchar_t *format, ...)
{
	wchar_t buffer[256];
	va_list args;
	va_start(args,format);
	int result=vswprintf(buffer,256,format,args);
	va_end(args);
	if (result!=(int)wcslen(expected) || wcscmp(buffer,expected)) {
		fprintf(stderr,"wide-format-test: FAIL format='%ls' expected='%ls' actual='%ls' length=%d\n",
			format,expected,buffer,result);
		++failures;
	}
}

int main(void)
{
	check_io();
	check(L"-9223372036854775808 18446744073709551615",
		L"%lld %ju",LLONG_MIN,UINTMAX_MAX);
	check(L"-7 255 -123 456",L"%hhd %hhu %hd %hu",-7,255,-123,456);
	check(L"-9 12 -13 14",L"%ld %lu %zd %zu",-9L,12UL,(ptrdiff_t)-13,(size_t)14);
	check(L"0xff 011 00042",L"%#x %#o %05d",255,9,42);
	check(L"     007|abc  |",L"%8.3d|%-5.3s|",7,"abcdef");
	check(L"22 11",L"%2$d %1$d",11,22);
	check(L"    1.25",L"%3$*1$.*2$f",8,2,1.25);
	check(L"1.250000 -0.00 1.25e+01 123",L"%f %.2f %.2e %.3g",
		1.25,-0.0,12.5,123.0);
	check(L"0x1.8p+0 1.125",L"%a %.3Lf",1.5,1.125L);
	check(L"1.00000000000000000011",L"%.20Lf",1.0L+0x1p-63L);
	check(L"|0|+0007|",L"%.0d|%#.0o|%+05d|",0,0,7);
	check(L"inf -inf nan",L"%g %g %g",INFINITY,-INFINITY,NAN);
	check(L"0x1234",L"%p",(void *)(uintptr_t)0x1234);
	check(L"wide Z A",L"%ls %lc %c",L"wide",(wint_t)L'Z','A');
	check(L"\u00e9 \U0001f600",L"%s %ls","\xc3\xa9",L"\U0001f600");
	check(L"   \u00e9\U0001f600|\u00e9 |",L"%5.2s|%-2.1ls|",
		"\xc3\xa9\xf0\x9f\x98\x80x",L"\u00e9\U0001f600");
	check(L"\u00e9\U0001f600%",L"\u00e9%lc%%",(wint_t)L'\U0001f600');
	check(L"abc  |1.250000",L"%*.*s|%.*f",-5,3,"abcdef",-1,1.25);
	check(L"",L"%.0s","\xff");
	int n=-1;
	long ln=-1;
	long long lln=-1;
	short hn=-1;
	signed char hhn=-1;
	ptrdiff_t zn=-1;
	intmax_t jn=-1;
	wchar_t buffer[32];
	int result=swprintf(buffer,32,L"\u00e9%ls%n%ln%lln%hn%hhn%zn%jn!",
		L"\U0001f600",&n,&ln,&lln,&hn,&hhn,&zn,&jn);
	if (result!=3 || n!=2 || ln!=2 || lln!=2 || hn!=2 ||
		hhn!=2 || zn!=2 || jn!=2 || wcscmp(buffer,L"\u00e9\U0001f600!"))
		++failures;
	wmemset(buffer,L'!',32);
	if (swprintf(buffer,4,L"%s","\xc3\xa9\xf0\x9f\x98\x80xy")>=0 ||
		wmemcmp(buffer,L"\u00e9\U0001f600x\0!",5)) ++failures;
	buffer[0]=L'!';
	if (swprintf(buffer,1,L"abc")>=0 || buffer[0]) ++failures;
	buffer[0]=L'!';
	if (swprintf(buffer,0,L"abc")>=0 || buffer[0]!=L'!' ||
		swprintf(NULL,0,L"abc")>=0) ++failures;
	if (swprintf(buffer,4,L"abc")!=3 || wcscmp(buffer,L"abc")) ++failures;
	if (swprintf(buffer,32,L"%lc",(wint_t)0)!=1 || buffer[0] || buffer[1])
		++failures;
	n=-1;
	if (swprintf(buffer,3,L"\u00e9abc%n",&n)>=0 || n!=4 ||
		wcscmp(buffer,L"\u00e9a")) ++failures;
	if (swprintf(buffer,4,L"%1000000d",7)>=0 ||
		wmemcmp(buffer,L"   \0",4)) ++failures;
	if (swprintf(buffer,4,L"%.1000000f",1.0)>=0 ||
		wmemcmp(buffer,L"1.0\0",4)) ++failures;
	errno=0;
	if (swprintf(buffer,32,L"%s","\xff")>=0 || errno!=EILSEQ || buffer[0])
		++failures;
	errno=0;
	if (swprintf(buffer,32,L"%2147483648d",1)>=0 || errno!=EOVERFLOW || buffer[0])
		++failures;
	errno=0;
	n=-1;
	if (swprintf(buffer,32,L"%n%", &n)>=0 || errno!=EINVAL || n!=-1)
		++failures;
	errno=0;
	if (swprintf(buffer,32,L"%2$d",1,2)>=0 || errno!=EINVAL) ++failures;
	errno=0;
	if (swprintf(buffer,32,L"%1$d %d",1,2)>=0 || errno!=EINVAL) ++failures;
	errno=0;
	if (swprintf(buffer,32,L"%0$d",1)>=0 || errno!=EINVAL) ++failures;
	errno=0;
	if (swprintf(buffer,32,L"%*d",INT_MIN,1)>=0 || errno!=EOVERFLOW) ++failures;
	if (failures) {
		fprintf(stderr,"wide-format-test: FAIL %d checks\n",failures);
		return 1;
	}
	puts("wide-format-test: PASS");
	return 0;
}
