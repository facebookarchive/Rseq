#include "rseq/rseq_c.h"

extern inline int rseq_begin();
extern inline int rseq_load(rseq_value_t *dst, rseq_repr_t *src);
extern inline int rseq_store(rseq_repr_t *dst, rseq_value_t val);
extern inline int rseq_store_fence(rseq_repr_t *dst, rseq_value_t val);
extern inline int rseq_validate();
