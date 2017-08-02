
	/* $Id: fpmi_arrays.h,v 1.2 2008/05/24 17:38:47 anight Exp $ */
	/* (c) 2007,2008 Andrei Nigmatulin */

#ifndef FPMI_ARRAYS_H
#define FPMI_ARRAYS_H 1

#include <stdlib.h>
#include <string.h>

struct fpmi_array_s {
	void *data;
	size_t sz;
	size_t used;
	size_t allocated;
};

static inline struct fpmi_array_s *fpmi_array_init(struct fpmi_array_s *a, unsigned int sz, unsigned int initial_num) /* {{{ */
{
	void *allocated = 0;

	if (!a) {
		a = malloc(sizeof(struct fpmi_array_s));

		if (!a) {
			return 0;
		}

		allocated = a;
	}

	a->sz = sz;

	a->data = calloc(sz, initial_num);

	if (!a->data) {
		free(allocated);
		return 0;
	}

	a->allocated = initial_num;
	a->used = 0;

	return a;
}
/* }}} */

static inline void *fpmi_array_item(struct fpmi_array_s *a, unsigned int n) /* {{{ */
{
	char *ret;

	ret = (char *) a->data + a->sz * n;

	return ret;
}
/* }}} */

static inline void *fpmi_array_item_last(struct fpmi_array_s *a) /* {{{ */
{
	return fpmi_array_item(a, a->used - 1);
}
/* }}} */

static inline int fpmi_array_item_remove(struct fpmi_array_s *a, unsigned int n) /* {{{ */
{
	int ret = -1;

	if (n < a->used - 1) {
		void *last = fpmi_array_item(a, a->used - 1);
		void *to_remove = fpmi_array_item(a, n);

		memcpy(to_remove, last, a->sz);

		ret = n;
	}

	--a->used;

	return ret;
}
/* }}} */

static inline void *fpmi_array_push(struct fpmi_array_s *a) /* {{{ */
{
	void *ret;

	if (a->used == a->allocated) {
		size_t new_allocated = a->allocated ? a->allocated * 2 : 20;
		void *new_ptr = realloc(a->data, a->sz * new_allocated);

		if (!new_ptr) {
			return 0;
		}

		a->data = new_ptr;
		a->allocated = new_allocated;
	}

	ret = fpmi_array_item(a, a->used);

	++a->used;

	return ret;
}
/* }}} */

static inline void fpmi_array_free(struct fpmi_array_s *a) /* {{{ */
{
	free(a->data);
	a->data = 0;
	a->sz = 0;
	a->used = a->allocated = 0;
}
/* }}} */

#endif
