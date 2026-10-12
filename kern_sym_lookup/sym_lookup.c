#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/mman.h>

struct ksym {
        long addr;
        char *name;
};

#define MAX_SYMS 300000
static struct ksym syms[MAX_SYMS];
static int sym_cnt;

static int ksym_cmp(const void *p1, const void *p2)
{
        return ((struct ksym *)p1)->addr - ((struct ksym *)p2)->addr;
}

int load_kallsyms(void)
{
        FILE *f = fopen("/proc/kallsyms", "r");
        char func[256], buf[256];
        char symbol;
        void *addr;
        int i = 0;

        if (!f)
                return -ENOENT;

        while (fgets(buf, sizeof(buf), f)) {
                if (sscanf(buf, "%p %c %s", &addr, &symbol, func) != 3)
                        break;
                if (!addr)
                        continue;
                syms[i].addr = (long) addr;
                syms[i].name = strdup(func);
                i++;
        }
        fclose(f);
        sym_cnt = i;
        qsort(syms, sym_cnt, sizeof(struct ksym), ksym_cmp);
        return 0;
}

struct ksym *ksym_search(long key)
{
        int start = 0, end = sym_cnt;
        int result;

        /* kallsyms not loaded. return NULL */
        if (sym_cnt <= 0)
                return NULL;

        while (start < end) {
                size_t mid = start + (end - start) / 2;

                result = key - syms[mid].addr;
                if (result < 0)
                        end = mid;
                else if (result > 0)
                        start = mid + 1;
                else
                        return &syms[mid];
        }

        if (start >= 1 && syms[start - 1].addr < key &&
            key < syms[start].addr)
                /* valid ksym */
                return &syms[start - 1];

        /* out of range. return _stext */
        return &syms[0];
}

long ksym_get_addr(const char *name)
{
        int i;

        for (i = 0; i < sym_cnt; i++) {
                if (strcmp(syms[i].name, name) == 0)
                        return syms[i].addr;
        }

        return 0;
}

/* open kallsyms and read symbol addresses on the fly. Without caching all symbols,
 * this is faster than load + find.
 */
int kallsyms_find(const char *sym, unsigned long long *addr)
{
        char type, name[500];
        unsigned long long value;
        int err = 0;
        FILE *f;

        f = fopen("/proc/kallsyms", "r");
        if (!f)
                return -EINVAL;

        while (fscanf(f, "%llx %c %499s%*[^\n]\n", &value, &type, name) > 0) {
                if (strcmp(name, sym) == 0) {
                        *addr = value;
                        goto out;
                }
        }
        err = -ENOENT;

out:
        fclose(f);
        return err;
}

void remove_spaces(char* s) {
    char *d = s;
    do {
        while (*d == ' ' || *d == 10) {
            ++d;
        }
    } while (*s++ = *d++);
}

void print_debug_str(char *s)
{
	char *p = s;
	while(*p != 0)
		printf("%d ", *p++);
	printf("\n");
}

char *little_endian_convert(char addr[])
{
	remove_spaces(addr);

	/* trim head '0x' */
	if (addr[0] == '0' && addr[1] == 'x') {
		for (int i = 0; i < strlen(addr) - 1; i++)
			addr[i] = addr[i+2];
	}

	if (strstr(addr, "ffffffff") != addr) {
		printf("little endian detected, converted hex: ");
		/* little endian */
		int len = strlen(addr);
		for (int i = 0; i < len / 2; i = i + 2) {
			char a1 = addr[i];
			char a2 = addr[i+1];
			addr[i] = addr[len-i-2];
			addr[i+1] = addr[len-i-1];
			addr[len-i-2] = a1;
			addr[len-i-1] = a2;
		}
		printf("%s\n", addr);
	}

	return addr;
}

#define ADDR_STR_LEN 64
int main(int argc, char *argv[])
{
        struct ksym *sym;
        char addr_str[ADDR_STR_LEN];
        long in_addr, sym_addr;
        load_kallsyms();
	printf("[Ctrl + c] to quit...\n");

        while (1) {
                printf("input kernel symbol address (hex): ");
                fgets(addr_str, ADDR_STR_LEN, stdin);
		little_endian_convert(addr_str);
                in_addr = strtoull(addr_str, NULL, 16);
                sym = ksym_search(in_addr);
		sym_addr = sym->addr;
                printf("\nsymbol at 0x%lx:\n", in_addr);
                printf("[[ %s ]] 0x%lx\n", sym->name, sym->addr);
		printf("===================================================\n");
        }

        return 0;
}
