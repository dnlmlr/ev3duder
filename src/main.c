#define MAIN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <wchar.h>

#include <hidapi.h>
#include "btserial.h"
#include "ev3_io.h"

#include "defs.h"
#include "systemcmd.h"
#include "error.h"
#include "utf8.h"
#include "funcs.h"

#define VendorID 0x694 /* LEGO GROUP */
#define ProductID 0x005 /* EV3 */
#define SerialID NULL

static void params_print()
{
    puts(	"USAGE: ev3duder "
            "[ up loc rem | dl rem loc | exec rem | kill rem |\n"
            "                  "
            "cp rem rem | mv rem rem | rm rem | ls [rem] | tree [rem] |\n"
            "                  "
            "shell str | pwd [rem] | test ]\n"
            "\n"
            "       "
            "rem = remote (EV3) system path, loc = (local file system) path"	"\n");
}
#define FOREACH_ARG(ARG) \
    ARG(test)            \
    ARG(up)              \
    ARG(dl)              \
    ARG(exec)            \
    ARG(pwd)             \
    ARG(ls)              \
    ARG(rm)              \
    ARG(mkdir)           \
    ARG(mkrbf)           \
    ARG(end)

#define MK_ENUM(x) ARG_##x,
#define MK_STR(x) T(#x),
enum ARGS { FOREACH_ARG(MK_ENUM) };
static const tchar *args[] = { FOREACH_ARG(MK_STR) };
static tchar * tstrjoin(tchar *, tchar *, size_t *);
static tchar* tchrsub(tchar *s, tchar old, tchar new);
#ifdef _WIN32
#define SANITIZE(s) (tchrsub((s), '/', '\\'))
#else
#define SANITIZE
#endif

int main(int argc, tchar *argv[])
{
    if (argc == 1)
    {
        params_print();
        return ERR_ARG;
    }
   
    if ((handle = hid_open(VendorID, ProductID, SerialID)))
    {
      puts("USB connection established.");
      // the things you do for type safety...
      ev3_write = (int (*)(void*, const u8*, size_t))hid_write;
      ev3_read_timeout = (int (*)(void*, u8*, size_t, int))hid_read_timeout;
      ev3_error = (const wchar_t* (*)(void*))hid_error;
    }
    else if ((handle = bt_open()))
    {
      fputs("Bluetooth serial connection established.", stderr);
      ev3_write = bt_write;
      ev3_read_timeout = bt_read;
      ev3_error = bt_error;
    }
    else if (strcmp(argv[1], "-i") == 0)
    {
        argv++; 
        argc--;
    } else {
        puts("EV3 not found. Either plug it into the USB port or pair over Bluetooth. ");
        return ERR_HID; // TODO: rename
    }
	

    int i;
    for (i = 0; i < ARG_end; ++i)
        if (tstrcmp(argv[1], args[i]) == 0) break;

    argc -= 2;
    argv += 2;
    int ret;
    tchar *cd = tgetenv(T("CD"));
    switch (i)
    {
    case ARG_test:
        assert(argc == 0);
        ret = test();
        break;

    case ARG_up:
        assert(argc == 2);
        {
		FILE *fp = tfopen(SANITIZE(argv[0]), "rb");
		size_t len;
		tchar *path = tstrjoin(cd, argv[1], &len); 
		if (!fp)
		{
			printf("File <%" PRIts "> doesn't exist.\n", argv[0]);
			return ERR_IO;
		}
		ret = up(fp, U8(path, len));
        }
        break;

    case ARG_exec:
        assert(argc == 1);
        {
        size_t len;
        tchar *path = tstrjoin(cd, argv[0], &len); 
        ret = exec(U8(path, len));
        }
        break;

    case ARG_end:
        params_print();
        return ERR_ARG;
    case ARG_pwd:
        assert(argc <= 1);
        {
		size_t len;
		tchar *path = tstrjoin(cd, argv[0], &len); 
		printf("path:%" PRIts " (len=%zu)\nuse export cd=dir to cd to dir\n",
			path ?: T("."), len);  
        }
        return ERR_UNK;
    case ARG_ls:
        assert(argc <= 1);
        {
        size_t len;
        tchar *path = tstrjoin(cd, argv[0], &len); 
        ret = ls(len ? U8(path, len) : ".");
        }
        break;

    case ARG_mkdir:
        assert(argc == 1);
        {
        size_t len;
        tchar *path = tstrjoin(cd, argv[0], &len); 
        ret = mkdir(U8(path, len));
        }
        break;
    case ARG_mkrbf:
        assert(argc == 2);
        {
        FILE *fp = tfopen(SANITIZE(argv[1]), "w");
        if (!fp)
          return ERR_IO;
        char part1[] = 
"LEGO\x52\x00\x00\x00\x68\x00\x01\x00\x00\x00\x00\x00\x1C\x00\x00\x00\x00\x00\x00\x00\x08\x00\x00\x00\x60\x80";
        char part2[] = "\x44\x85\x82\xE8\x03\x40\x86\x40\x0A";
        size_t path_sz = tstrlen(argv[0]);
        char *path = U8(argv[0], path_sz);
        u32 static_sz = (u32)(sizeof part1 -1 + path_sz + 1 + sizeof part2 -1);
        memcpy(&part1[4], &static_sz, 4);
        fwrite(part1, sizeof part1 - 1, 1, fp);
        fwrite(path, path_sz + 1, 1, fp);
        fwrite(part2, sizeof part2 - 1, 1, fp);
        fclose(fp);
        }
        return ERR_UNK;
    case ARG_rm:
        assert(argc == 1);
        {
		if(tstrchr(argv[0], '*'))
		{
			printf("This will probably remove more than one file. Are you sure to proceed? (Y/n)");
			char ch;
			scanf("%c", &ch);
			if ((ch | ' ') == 'n') // check if 'n' or 'N'
			{
				ret = ERR_UNK;
				break;
			}
			fflush(stdin); // technically UB, but POSIX and Windows allow for it
		}
		size_t len;
		tchar *path = tstrjoin(cd, argv[0], &len); 
		ret = rm(U8(path, len));
        }
        break;
    default:
        ret = ERR_ARG;
        printf("<%" PRIts "> hasn't been implemented yet.", argv[0]);
    }

    if (ret == ERR_HID)
        fprintf(stdout, "%ls\n", (wchar_t*)hiderr);
    else
        fprintf(stdout, "%s (%s)\n", errmsg, (char*)hiderr ?: "null");

    // maybe \n to stderr?
    return ret;
}
static tchar *tstrjoin(tchar *s1, tchar *s2, size_t *len)
{
    if (s1 == NULL || *s1 == '\0') {
      if (s2 != NULL)
        *len = tstrlen(s2);
      else
        *len = 0;
      return s2;
    }
    if (s2 == NULL || *s2 == '\0') {
      if (s1 != NULL)
        *len = tstrlen(s1);
      else
        *len = 0;
      return s1;
    }

    size_t s1_sz = tstrlen(s1),
           s2_sz = tstrlen(s2);
    *len = sizeof(tchar[s1_sz+s2_sz]);
    tchar *ret = malloc(*len+1);
    mempcpy(mempcpy(ret,
                s1, sizeof(tchar[s1_sz+1])),
                s2, sizeof(tchar[s2_sz+1])
           );
    ret[s1_sz] = '/';
    return ret;
}
static tchar* tchrsub(tchar *s, tchar old, tchar new)
{
	tchar *ptr = s;
	if (ptr == NULL || *ptr == T('\0'))
		return NULL;
	do
	{
		if (*ptr == old) *ptr = new;
	}while(*++ptr);
	return s;
}

