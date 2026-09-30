/* Point d'entree de type musl utilise par libc++ (LIBCXX_HAS_MUSL_LIBC) :
 * libc++ inclut <bits/alltypes.h> avec __NEED_mbstate_t pour obtenir
 * mbstate_t. Les types sont definis dans <bits/alos_wchar.h>. */
#include <bits/alos_wchar.h>