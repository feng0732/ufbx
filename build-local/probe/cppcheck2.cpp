#include "../../ufbx.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#define UFBXT_TEST_GROUP "x"
#define UFBXT_IMPL 1
#define ufbxt_assert(c) do { if(!(c)){printf("fail %s\n",#c);} } while(0)
#define ufbxt_soft_assert(c) ufbxt_assert(c)
#define ufbxt_hintf(...) do{}while(0)
#define ufbxt_logf(...) do{}while(0)
#define ufbxt_log_error(e) do{}while(0)
#define ufbxt_arraycount(a) (sizeof(a)/sizeof(*(a)))
typedef struct { size_t num; double sum; double max; } ufbxt_diff_error;
static void ufbxt_assert_close_real(ufbxt_diff_error*,ufbx_real,ufbx_real){}
static void ufbxt_assert_close_vec3(ufbxt_diff_error*,ufbx_vec3,ufbx_vec3){}
#define UFBXT_FILE_TEST(n) void n()
#define UFBXT_FILE_TEST_ALT(n,f) void n()
#define UFBXT_FILE_TEST_FLAGS(n,fl) void n()
#define UFBXT_FILE_TEST_OPTS(n,g) void n()
#define UFBXT_FILE_TEST_OPTS_ALT_FLAGS(n,f,g,fl) void n()
#define UFBXT_FILE_TEST_SUFFIX(n,s) void n()
#define UFBXT_FILE_TEST_ALT_SUFFIX(n,f,s) void n()
#define UFBXT_FILE_TEST_SUFFIX_FLAGS(n,s,fl) void n()
#define UFBXT_FILE_TEST_ALT_FLAGS(n,f,fl) void n()
#define UFBXT_FILE_TEST_SUFFIX_OPTS(n,s,g) void n()
#define UFBXT_FILE_TEST_PATH(n,p) void n()
#define UFBXT_DEFLATE_TEST(n) void n()
#include "../../test/test_animation.h"
int main(){ baked_quat_half_turn_contract(); return 0; }
