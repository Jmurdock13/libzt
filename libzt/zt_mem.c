#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "zt.h"
#include "zt_mem.h"
#include "adt/zt_list.h"

/*
 * FIXME: threads will need these wrapped
 */
static struct
{
	void	*(* alloc)(size_t);
	void	 (* dealloc)(void *);
	void	*(* realloc)(void *, size_t);
}GLOBAL_zmt = { malloc, free, realloc };

static zt_elist(pools);
static zt_elist(sets);

//static long x_sys_page_size = 0;

typedef struct zt_mem_elt {
	zt_elist			  free_elt_list;
	struct zt_mem_page	* parent_page;
	unsigned long		  data[0];
} zt_mem_elt;

struct zt_mem_heap {
	char		* name;
	size_t		  size;
	unsigned long	  heap[0];
};

typedef struct zt_mem_page {
	zt_elist	  	  	  page_list;
	struct zt_mem_pool	* parent_pool;
	unsigned long		  num_free_elts;
	unsigned long	 	  data[0];
} zt_mem_page;

struct zt_mem_pool {
	zt_elist	  	  	  pool_list;
	char		 	* name;
	long			  rcache;        /* requested elements in cache */
	long			  ncache;        /* actual elements in cache */
	long			  pcache;        /* pages in cache */
	long			  nfree_pages;   /* number of free pages */
	long			  npages;        /* total pages */
	long	  	  	  elts_per_page; /* elements per page */
	size_t	  	  	  elt_size;	 /* element size */
	size_t			  page_size;	 /* page size */
	long			  page_allocs;	 /* number of page allocs */
	long			  page_frees;	 /* number of page frees */
	zt_page_release_test	  release_test_cb;
	void			* release_test_cb_data;
	int			  flags;
	zt_elist			  page_list;
	zt_elist		  	  free_elt_list;
};

struct zt_mem_pool_group {
	zt_elist		  group_list;
	int		  npools;
	zt_mem_pool	**pools;
};

struct zt_mem_set_elt {
	zt_elist			  elt_list;
	void			* elt;
};

struct zt_mem_set {
	zt_elist		  	  set_list;
	char			* name;
	zt_elist		  	  elt_list;
};


/* forward declarations */
static char *zt_mem_strdup(char *str);
static void zt_mem_page_display(int, zt_mem_page *);
static int zt_mem_page_destroy(zt_mem_page *page);
static zt_mem_page *zt_mem_page_alloc(zt_mem_pool *);
static void zt_mem_elt_list_display(int offset, zt_elist *head);
static int zt_default_release_test(int total_pages, int free_pages, int cache_size, int, void *cb_data);

/* exported functions */
zt_mem_heap*
zt_mem_heap_init(char *name, size_t size)
{
	zt_mem_heap	 *heap;

	if((heap = GLOBAL_zmt.alloc(sizeof(zt_mem_heap) + size)) == NULL)
	{
		return NULL;
	}

	heap->name = zt_mem_strdup(name ? name : "*unknown*");	
	heap->size = size;
	return heap;	
}

void
zt_mem_heap_destroy(zt_mem_heap **heap)
{
	if(!heap)
		return;

	GLOBAL_zmt.dealloc((*heap)->name);
	(*heap)->name = 0;
	(*heap)->size = 0;
	
	GLOBAL_zmt.dealloc(*heap);
	*heap = 0;
}
void *zt_mem_heap_get_data(zt_mem_heap *heap) 
{
	return (void *)heap->heap;
}

char *zt_mem_heap_get_name(zt_mem_heap *heap)
{
	if(heap) {
		return heap->name;
	}
	return 0;
}

static int x_calculate_page_data(long elts, size_t size, long *page_size, long *epp, long *npages)
{
	static long		  sys_page_size = 0;	
	long			  usable_page_size = 0;
	
	if(sys_page_size == 0) {
		sys_page_size = sysconf(_SC_PAGESIZE);
	}

	usable_page_size = sys_page_size - sizeof(zt_mem_page);
	
	/* This algorithm is pretty stupid it mearly tries to
	 * calculate if I can fit a reasonable number of elements into
	 * a system memory page.
	 * if (the element size is < 1/2 (system page size - overhead))
	 *   use a system page for page size
	 * else
	 *   page size is elts * size + overhead
	 */
	if(size < (usable_page_size / 2)) {
		*epp = usable_page_size / (sizeof(zt_mem_elt) + size);
		*page_size = sys_page_size;
		if(elts > *epp) {
			*npages = elts / *epp;
		} else {
			*npages = 1;
		}
	} else {
		*epp = elts;
		*page_size = (elts * size) + sizeof(zt_mem_page);
		*npages = 1;
	}
	
	return 0;
}

zt_mem_pool*
zt_mem_pool_init(char *name, long elts,
		 size_t size, zt_page_release_test release_test,
		 void *cb_data, int flags)
{
	zt_mem_pool	* pool;
	zt_mem_page	* page;

	long		  epp;
	long		  npage_size;
	long		  npages;
	long		  pcache;

	x_calculate_page_data(elts ? elts : 1, size, &npage_size, &epp, &npages);

	if(elts > 0) {
		if(elts < epp) {
			pcache = 1;
		} else {
			long tmp = elts % epp;
			pcache = elts / epp;
			if(tmp > 0) {
				pcache++;
			}
		}
	} else {
		/* will be 1 with the current algorithm */
		pcache = npages;
	}
	
	if((pool = GLOBAL_zmt.alloc(sizeof(zt_mem_pool))) == 0){
		return NULL;
	}
	
	zt_elist_reset(&pool->pool_list);

	pool->name = zt_mem_strdup(name ? name : "*unknown*");	
	pool->rcache = elts;
	pool->ncache = pcache * epp;
	pool->pcache = pcache;
	// > 0 ? cache : 0; /* cache should never be negative*/
	pool->nfree_pages = 0;
	pool->npages = 0;
	/* pool->elts_per_page = elts; */
	pool->elts_per_page = epp;
	pool->elt_size = size;
	pool->page_size = npage_size;
	pool->page_allocs = 0;
	pool->page_frees = 0;
	pool->release_test_cb = release_test ? release_test : zt_default_release_test;
	pool->release_test_cb_data = cb_data ? cb_data : NULL;
	pool->flags = flags;
	
/* 	printf("page size calculated: %ld\npage size actual: %ld\n", */
/* 	       pool->page_size, */
/* 	       sizeof(zt_mem_page) + (pool->elts_per_page * (sizeof(zt_mem_elt) + pool->elt_size))); */
	
	zt_elist_reset(&pool->page_list);
	zt_elist_reset(&pool->free_elt_list);

	if(npages > 0) {
		/* fill the cache */
		while(npages--){
			page  = zt_mem_page_alloc(pool);
			zt_elist_add_tail(&pool->page_list, &page->page_list);
			pool->page_allocs++;
		}
	}
	
	zt_elist_add_tail(&pools, &pool->pool_list);
	return pool;
}

void *
zt_mem_pool_alloc(zt_mem_pool *pool)
{
	zt_elist		 *tmp;
	zt_mem_elt	 *elt;
	
	if(pool == 0) {
		return 0;
	}

	/* if the free elt list is empty get a new page */
	if(zt_elist_empty(&pool->free_elt_list) == 1) {
		/* alloc a new page */
		zt_mem_page	 *page;
		page = zt_mem_page_alloc(pool);
		zt_elist_add_tail(&page->page_list, &pool->page_list);
		pool->page_allocs++;
	}

	tmp = zt_elist_get_next(&pool->free_elt_list);
	zt_elist_remove(tmp);
	elt = zt_elist_entry(tmp, zt_mem_elt, free_elt_list);

	--elt->parent_page->num_free_elts;
	
	/* if we have used the first elt then decrement the free pages */
	if(elt->parent_page->num_free_elts == pool->elts_per_page - 1) {
		--pool->nfree_pages;
	}
	
	return (void *)&elt->data;
}

void
zt_mem_pool_release(void **data){
	zt_mem_elt	 *elt;
	zt_mem_pool 	 *pool;
	zt_mem_page 	 *page;

	elt = zt_elist_entry(*data, zt_mem_elt, data);
	page = elt->parent_page;
	pool = page->parent_pool;
	
	/* add the element to the pools free elt list */
	page->num_free_elts++;
	zt_elist_add(&pool->free_elt_list, &elt->free_elt_list);

	/* if all of the pages elements are free */
	if(page->num_free_elts == pool->elts_per_page) {
		pool->nfree_pages++;
	}	

	if(page->num_free_elts == pool->elts_per_page) {
		if(pool->release_test_cb(pool->rcache,
					 pool->ncache * pool->nfree_pages,
					 pool->ncache * (pool->npages - pool->nfree_pages),
					 pool->flags,
					 pool->release_test_cb_data)) {
			/* release the page */
			zt_mem_page_destroy(page);
			pool->page_frees++;
		}
	}
}

int
zt_mem_pool_release_free_pages(zt_mem_pool *pool) 
{
	zt_elist		* pages,
		        * tpage;
	zt_mem_page	* page;
	
	zt_elist_for_each_safe(&(pool)->page_list, pages, tpage) {
		page = zt_elist_entry(pages, zt_mem_page, page_list);

		if(page->num_free_elts == pool->elts_per_page) {
			zt_mem_page_destroy(page);
		}
	}

	return 0;
}


int
zt_mem_pool_destroy(zt_mem_pool **pool) {
	zt_elist		* tmp, * ptmp;
	zt_mem_page	* page;	

	zt_elist_for_each_safe(&(*pool)->page_list, tmp, ptmp) {
		page = zt_elist_entry(tmp, zt_mem_page, page_list);
		
		/* sanity checking */
		if(page->num_free_elts < (*pool)->elts_per_page) {
			return -1;
		} else {
			zt_mem_page_destroy(page);
		}
	}

	zt_elist_remove(&(*pool)->pool_list);

	GLOBAL_zmt.dealloc((*pool)->name);
	(*pool)->name = NULL;
	
	GLOBAL_zmt.dealloc((*pool));
	*pool = NULL;
	
	return 0;
}

int
zt_mem_pool_get_stats(zt_mem_pool *pool, zt_mem_pool_stats *stats) 
{
	if(!pool || !stats) {
		return ZT_FAIL;
	}

	stats->requested_elts = pool->rcache;
	stats->actual_elts = pool->ncache;
	stats->cached_pages = pool->pcache;
	stats->free_pages = pool->nfree_pages;
	stats->pages = pool->npages;
	stats->elts_per_page = pool->elts_per_page;
	stats->elt_size = pool->elt_size;
	stats->page_size = pool->page_size;
	stats->page_allocs = pool->page_allocs;
	stats->page_frees = pool->page_frees;
	stats->flags = pool->flags;
	
	return ZT_PASS;
}

void
zt_mem_pool_display(int offset, zt_mem_pool *pool, int flags)
{
	zt_elist		* tmp;
	zt_mem_page	* page;
	long		  felts = 0;

	zt_elist_for_each(&pool->page_list, tmp) {
		page = zt_elist_entry(tmp, zt_mem_page, page_list);
		felts += page->num_free_elts;
	}

	printf(BLANK "pool: \"%s\" [%p] {\n"
	       BLANK "elements: {\n"
	       BLANK "elt cache requested: %ld elements\n"
	       BLANK "elt cache actual: %ld elements\n"
	       BLANK "total elts: %ld\n"
	       BLANK "free elts: %ld\n"
	       BLANK "}\n"
	       BLANK "pages: {\n"
	       BLANK "page cache: %ld page(s)\n"
	       BLANK "free pages: %ld\n"
	       BLANK "page size: %ld\n"
	       BLANK "pages in use: %ld\n"
	       BLANK "total pages: %ld\n"
	       BLANK "elements per page: %ld\n"
	       BLANK "page allocs: %ld\n"
	       BLANK "page frees: %ld\n"
	       BLANK "}\n"
	       BLANK "overall: {\n"
	       BLANK "element size: %ld bytes + overhead = %ld bytes \n"
	       BLANK "page usage (num elts * elt size): %ld bytes\n"
	       BLANK "page memory (page size * total pages): %ld bytes\n"
	       BLANK "}\n",
	       INDENT(offset), pool->name, pool,
	       INDENT(offset+1),
	       INDENT(offset+2), pool->rcache,
	       INDENT(offset+2), pool->ncache,
	       INDENT(offset+2), (pool->elts_per_page * pool->npages),
	       INDENT(offset+2), felts,
	       INDENT(offset+1), INDENT(offset+1),
	       INDENT(offset+2), pool->pcache,
	       INDENT(offset+2), pool->nfree_pages,
	       INDENT(offset+2), pool->page_size,	       
	       INDENT(offset+2), (pool->npages - pool->nfree_pages),
	       INDENT(offset+2), pool->npages,
	       INDENT(offset+2), pool->elts_per_page,
	       INDENT(offset+2), pool->page_allocs,
	       INDENT(offset+2), pool->page_frees,
	       INDENT(offset+1), INDENT(offset+1),
	       INDENT(offset+2), (long)pool->elt_size, (unsigned long)(sizeof(zt_mem_elt) + pool->elt_size),
	       INDENT(offset+2), (sizeof(zt_mem_elt) + pool->elt_size) * pool->elts_per_page,
	       INDENT(offset+2), pool->page_size * pool->npages,
               INDENT(offset+1));

	if(flags & DISPLAY_POOL_FREE_LIST) {
		printf(BLANK "free_list {\n", INDENT(offset+1));
		zt_mem_elt_list_display(offset+2, &pool->free_elt_list);
		printf("\n" BLANK "}\n", INDENT(offset+1));
	}

	if(flags & DISPLAY_POOL_PAGES) {
		printf(BLANK "pages {\n", INDENT(offset+1));
		zt_elist_for_each(&pool->page_list, tmp){
			page = zt_elist_entry(tmp, zt_mem_page, page_list);
			zt_mem_page_display(offset+2, page);
		}
		printf(BLANK "}\n", INDENT(offset+1));
	}
	
	printf(BLANK "}\n", INDENT(offset));
}

void
zt_mem_pools_display(int offset, int flags)
{
	zt_elist *tmp;
	zt_mem_pool *pool;

	printf(BLANK "Pools { \n", INDENT(offset));
	zt_elist_for_each(&pools, tmp)
	{
		pool = zt_elist_entry(tmp, zt_mem_pool, pool_list);
		zt_mem_pool_display(offset + 1, pool, flags);
	}
	printf(BLANK "}\n", INDENT(offset));
}

void
zt_mem_pool_group_display(int offset, zt_mem_pool_group *group, int flags)
{
	int	  len, i;

	len = group->npools;
	
	printf(BLANK "Group {\n", INDENT(offset));
	for(i = 0; i < len; i++) {
		zt_mem_pool_display(offset + 1, group->pools[i], flags);
	}
	printf(BLANK "}\n", INDENT(offset));
}	


zt_mem_pool *
zt_mem_pool_get(char *name)
{
	int	  nlen;
	zt_elist	* tmp;
	
	if(!name) {
		return 0;
	}

	nlen = strlen(name);
	
	zt_elist_for_each(&pools, tmp)
	{
		zt_mem_pool	* pool;
		int		  olen;
		
		pool = zt_elist_entry(tmp, zt_mem_pool, pool_list);
		olen = strlen(pool->name);
		
		if(olen == nlen && (strncmp(name, pool->name, nlen) == 0)) {
			return pool;
		}
	}

	return 0;
}

zt_mem_pool_group *
zt_mem_pool_group_init(zt_mem_pool_desc	* group, int len)
{
	zt_mem_pool_group	* ngroup;
	int			  i;

	if((ngroup = XCALLOC(zt_mem_pool_group, 1)) == NULL) {
		return 0;
	}
	
	if((ngroup->pools = XCALLOC(zt_mem_pool *, len)) == NULL) {
		return 0;
	}
	zt_elist_reset(&ngroup->group_list);
	ngroup->npools = len;
	
	for(i = 0; i < len && group[i].name; i++) {
		ngroup->pools[i] = zt_mem_pool_init(group[i].name,
						    group[i].elts,
						    group[i].size,
						    group[i].release_test,
						    group[i].cb_data,
						    group[i].flags);
		if(!ngroup->pools[i]) {
			while(--i) {
				zt_mem_pool_destroy(&ngroup->pools[i]);
			}
			return 0;
		}
	}

	return ngroup;
}

void *
zt_mem_pool_group_alloc(zt_mem_pool_group *group, size_t size) 
{
	int	  len, i;
	
	len = group->npools;

	for(i = 0; i < len; i++) {
		if(size < group->pools[i]->elt_size) {
			return zt_mem_pool_alloc(group->pools[i]);
		}
	}
	
	return 0;
}

int
zt_mem_pool_group_destroy(zt_mem_pool_group * group)
{
	int	  len, i, ret = 0;

	len = group->npools;

	for(i = 0; i < len; i++) {
		if(zt_mem_pool_destroy(&group->pools[i]) != 0) {
			ret = -1;
		}
	}

	return ret;
}

zt_mem_set *
zt_mem_set_init(char *name)
{
	zt_mem_set	* set;
	
	if((set = GLOBAL_zmt.alloc(sizeof(zt_mem_set))) == 0) {
		return NULL;
	}
	
	zt_elist_reset(&set->set_list);
	zt_elist_reset(&set->elt_list);
			 
	set->name = zt_mem_strdup(name ? name : "*unknown*");

	return set;
}

int
zt_mem_set_add(zt_mem_set *set, void *d)
{
	/* struct zt_mem_set_elt	* elt; */

	/* alloc a new elt wrapper
	 * assign d to the data element
	 * return true
	 */
	return -1;
}

int
zt_mem_set_release(zt_mem_set *set)
{
	/*
	 * release the elt and release the wrapper
	 */
	
	return 0;
}


/* static functions  */
static char *zt_mem_strdup(char *str)
{
	char *tmp = "*unknown*";
	
	if(str)
	{
		int len = strlen(str);
		tmp = GLOBAL_zmt.alloc(len+1 * sizeof(char));
		memcpy(tmp, str, len);
		tmp[len] = '\0';
	}
	return tmp;
}

static int
zt_mem_page_destroy(zt_mem_page *page)
{
	zt_mem_elt	 *elt;
	int			  i;
	size_t			  size;
	
	if(page->num_free_elts < page->parent_pool->elts_per_page){
		printf("error: %s called when elements are still in use!\n", __FUNCTION__);
		return 1;
	}

	page->parent_pool->npages--;
	page->parent_pool->nfree_pages--;
	
	size = sizeof(zt_mem_elt) + page->parent_pool->elt_size;
	elt = (zt_mem_elt *)page->data;
	/* remove the elements from the free element list */
	for(i=0; i < page->num_free_elts; i++) {
		if(zt_elist_empty(&elt->free_elt_list) == 0)
			zt_elist_remove(&elt->free_elt_list);
		elt = (zt_mem_elt *)((unsigned long)elt + size);
	}
	/* remove the page from the page list */
	if(zt_elist_empty(&page->page_list) == 0) {
		zt_elist_remove(&page->page_list);
	}
	GLOBAL_zmt.dealloc(page);
	return 0;
}

static zt_mem_page *
zt_mem_page_alloc(zt_mem_pool *pool)
{
	zt_mem_page	 *page;
	zt_mem_elt	 *head;
	zt_mem_elt	 *elt;
	size_t			  size;
	int			  i;
	int			  epp;
	
	size = sizeof(zt_mem_elt) + pool->elt_size;
	/* sizeof(zt_mem_page) + (pool->elts_per_page * size)) */
	
	if((page = GLOBAL_zmt.alloc(pool->page_size)) == 0){
		return 0;
	}
	
	page->num_free_elts = 0;
	page->parent_pool = pool;
	zt_elist_reset(&page->page_list);

	/* pointer to the first element */
	head = (zt_mem_elt *)page->data;
	
	/* add the first element to the free list*/
	zt_elist_add_tail(&pool->free_elt_list, &head->free_elt_list);
	head->parent_page = page;
	page->num_free_elts++;

	epp = pool->elts_per_page;
	elt = head;
	for(i=1; i < epp; i++){
		elt = (zt_mem_elt *)((unsigned long)elt + size);
		zt_elist_reset(&elt->free_elt_list);
		elt->parent_page = page;
		zt_elist_add_tail(&pool->free_elt_list, &elt->free_elt_list);
		page->num_free_elts++;
	}
	pool->npages++;
	pool->nfree_pages++;
	return page;
}

static void
zt_mem_elt_list_display(int offset, zt_elist *head)
{
	zt_elist		 *tmp;
	zt_mem_elt	 *elt;
	int			  i;

	i = 0;
	zt_elist_for_each(head, tmp)
	{
		elt = zt_elist_entry(tmp, zt_mem_elt, free_elt_list);
		if(i){ printf("\n"); }
		printf(BLANK "elt: %p  parent_page: %p  data: %p", INDENT(offset), elt, elt->parent_page, elt->data);
		i = 1;
	}
}

/* static void */
static void
zt_mem_page_display(int offset, zt_mem_page *page)
{	
	printf(BLANK "page: %p {\n", INDENT(offset), page);
	printf(BLANK "num_free_elts: %ld\n", INDENT(offset+1), page->num_free_elts);
	printf(BLANK "parent pool: %p\n", INDENT(offset+1), page->parent_pool);
	printf(BLANK "}\n", INDENT(offset));
}


static int
zt_default_release_test(int req_elts, int free_elts, int used_elts, int flags, void *cb_data)
{
	if(flags & POOL_NEVER_FREE) {
		return 0;
	}
	return 1;
}
