#include "cachelab.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>


/* S groups,  E lines per group,   B bytes per block */

struct Line {
	char 			*block;
	char 			valid;
	unsigned int		last;
	unsigned int		count;
	unsigned long int 	tag;
	
};

struct Group {
	struct Line *lines;
};

struct Cache {
	/*   C = S*E*B   */
	int 		S;
	int 		E;
	int 		B;
	/* think addr consisting of t s b bits, i.e. t+s+b=64 in x86-64 */
	int		s;
	int		b;
	int 		t;
	struct Group 	*groups;
};

struct Cache cache;

int		verbose;
unsigned int 	time_count = 0;

int init_cache(void)
{
	int 		i;
	int		j;
	struct Line 	*ln;
	struct Group 	*grp;
	int		S = cache.S;
	int 		E = cache.E;
	int		B = cache.B;

	cache.groups = (struct Group*)malloc(S*sizeof(struct Group));
	if (!cache.groups)
		exit(3);
	for (i = 0; i < S; i++) {
		grp = cache.groups + i;
		grp->lines = (struct Line*)malloc(E * sizeof(struct Line));
		if (!grp->lines)
			exit(3);
		for (j = 0; j < E; j++) {
			ln = grp->lines + j;
			ln->valid = 0;
			ln->tag = 0;
			ln->count = 0;
			ln->last = time_count;
			ln->block = (char *)malloc(B);
			if (!ln->block)
				exit(3);
		}
	}

	cache.t = 64 - cache.s - cache.b;

	return 0;
}

void show_help(void)
{
	printf("Example:\n"
		"./csim -h  	<help>\n"
		"./csim -v	<verbose>\n"
		"./csim -s 4 -E 1 -b 4 -t traces/yi.trace\n"
		"./csim-ref -v -s 8 -E 2 -b 4 -t traces/yi.trace\n"
		);
	exit(0);
}


int parse_cmd(int argc, char* argv[], char** p)
{
	int opt;
	while ((opt = getopt(argc, argv, "hvs:E:b:t:")) != -1) {
		switch(opt) {
		case 'h':
			show_help();
			break;
		case 'v':
			verbose = 1;
			break;
		case 's':
			cache.s = atoi(optarg);
			cache.S = 1 << cache.s;
			break;
		case 'E':
			cache.E = atoi(optarg);
			break;
		case 'b':
			cache.b = atoi(optarg);
			cache.B = 1 << cache.b;
			break;
		case 't':
			*p = optarg;
			break;
		default:
			show_help();
			exit(1);
		}
	}
	return 0;
}


/*  "L 7ff000378,8" */
void parse_trace(char* s, unsigned long int* addr, int* size)
{
	char 		*tmp;
	char 		buf[15] = "0x";

	tmp = strtok(s+2, ","); 
	strcat(buf, tmp);
	*addr = (unsigned long int)strtol(buf, NULL, 16);

	tmp = strtok(NULL, ",");
	*size = atoi(tmp);

	return;
}

/********************************************
*@return 1: normal add   2: eviction then add
*********************************************/
int add_line(unsigned long int tag, int grp_idx)
{
	int			idx = -1;
	int			res = 1;
	struct Group		*grp;
	struct Line		*ln;

	grp = cache.groups + grp_idx;

	/* find idle line */
	for(int i = 0; i < cache.E; i++) {
		ln = grp->lines + i;
		if (!ln->valid) {
			idx = i;
			break;
		}
	}

	/* if no idle line, do eviction */
	if (idx < 0){
		/* random */
		// srand(time(NULL));
		// idx = rand() % cache.E;

		/* LFU */
		// unsigned int minval = 0xffffffff;
		// for(int i = 0; i < cache.E; i++) {
		// 	ln = grp->lines + i;
		// 	if (ln->count < minval) {
		// 		idx = i;
		// 		minval = ln->count;
		// 	}
		// }

		/* LRU notice: time(NULL) is not accurate, unit of timestamp is second*/
		time_t		oldest = time_count;
		for(int i = 0; i < cache.E; i++) {
			ln = grp->lines + i;
			if (ln->last < oldest) {
				idx = i;
				oldest = ln->last;
			}
		}

		res = 2;
	}
	
	ln = grp->lines + idx;
	ln->valid = 1;
	ln->count = 1;
	ln->last = time_count;
	ln->tag = tag;
	/* omit block copying */

	return res;
}

/**********************************************
*@param addr: normal memory address, 64 bits in x86-64
*@return 0:hit   1:miss	    2:miss eviction    negative:error
**********************************************/
int check_hit(unsigned long int addr)
{
	int			i;
	int 			tmp;
	// int			blk_bias;
	int			grp_idx;
	unsigned long int 	tag;
	int			b = cache.b;
	int			s = cache.s;
	int			t = cache.t;
	struct Group		*grp;
	struct Line		*ln;
	int			res = 1;

	// tmp = (1 << b) - 1;
	// blk_bias = (int)(addr & tmp);

	tmp = ((1 << s) - 1) << b;
	grp_idx = (int)((addr & tmp) >> b);

	tmp = ((1 << t) - 1) << (s+b);
	tag = (addr & tmp) >> (s+b);

	grp = cache.groups + grp_idx;
	for(i = 0; i < cache.E; i++) {
		ln = grp->lines + i;
		if (tag == ln->tag && ln->valid == 1) {
			res = 0;
			ln->count++;
			ln->last = time_count;
		}
	}

	/* if miss, add it */
	if (res == 1) {
		res = add_line(tag, grp_idx);
	}

	return res;
}

/******************************************
*@param s  "L 7ff000378,8" for example
*@return  0:hit   1:miss    2:miss eviction    negative:error
*******************************************/
int do_cache(char *s)
{
	int			size = 0;
	char			buf[15];
	unsigned long int 	addr = 0;

	strcpy(buf, s);
	parse_trace(buf, &addr, &size);
	return check_hit(addr);
}

int cache_load(char *s)
{
	int	res = do_cache(s);
	char	*strs[] = {"hit", "miss", "miss eviction"};

	if (verbose)
		printf("%s %s\n", s, strs[res]);
	return res;
}


int cache_store(char *s)
{
	// int	res = do_cache(s);
	// char	*strs[] = {"hit", "miss", "miss eviction"};
	// printf("%s %s\n", s, strs[res]);
	// return res;
	return cache_load(s);
}

/*****************
*@return  0:hit+hit     1: miss+hit     2:miss eviction+hit
******************/
int cache_modify(char *s)
{
	int	ret1;
	int	ret2;
	char	*strs[] = {"hit", "miss", "miss eviction"};

	ret1 = do_cache(s);
	/* ret2 always be 0*/
	ret2 = do_cache(s);

	if (verbose)
		printf("%s %s %s\n", s, strs[ret1], strs[ret2]);
	return ret1 + ret2;
}

void summary(int *hits, int *misses, int *evictions, int res, char modify)
{
	if (res == 0){
		*hits += 1;
	}
	else if (res == 1){
		*misses += 1;
	}
	else if (res == 2) {
		*misses += 1;
		*evictions += 1;
	}

	if (modify)
		*hits += 1;
}

int caching(char *filename)
{
	FILE	*file;
	char	row[20];
	int	res;
	int	hits = 0;
	int 	misses = 0;
	int	evictions = 0;

	file = fopen(filename, "r");
	if (!file) {
		printf("open %s failed\n", filename);
		exit(2);
	}

	while (fgets(row, 19, file)) {
		if (row[0] == 'I')
			continue;
		time_count++;
		/* remove '\n' otherwise influence printing*/
		int length = strlen(row);
		if (length > 0 && row[length-1] == '\n')
			row[length - 1] = '\0';

		switch (row[1]) {
		case 'L':
			res = cache_load(row+1);
			summary(&hits, &misses, &evictions, res, 0);
			break;
		case 'S':
			res = cache_store(row+1);
			summary(&hits, &misses, &evictions, res, 0);
			break;
		case 'M':
			res = cache_modify(row+1);
			summary(&hits, &misses, &evictions, res, 1);
			break;
		default:
			break;
		}
	}

	fclose(file);
	printSummary(hits, misses, evictions);

	return 0;
}

int release(void)
{
	int 		i, j;
	struct Group 	*grp;
	struct Line  	*ln;
	for (i = 0; i < cache.S; i++) {
		grp = cache.groups + i;
		for (j = 0; j < cache.E; j++) {
			ln = grp->lines + j;
			free(ln->block);
		}
		free(grp->lines);
	}
	free(cache.groups);
	return 0;
}


int main(int argc, char* argv[])
{
	char	*filename = NULL;
	parse_cmd(argc, argv, &filename);
	init_cache();
	caching(filename);
	release();
	return 0;
}