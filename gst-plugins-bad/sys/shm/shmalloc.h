
#include <stdlib.h>

#ifndef __SHMALLOC_H__
#define __SHMALLOC_H__

#ifdef GST_PACKAGE_NAME
#include <glib.h>

#define spalloc_new(type) g_slice_new (type)
#define spalloc_alloc(size) g_slice_alloc (size)

#define spalloc_free(type, buf) g_slice_free (type, buf)
#define spalloc_free1(size, buf) g_slice_free1 (size, buf)

#else

#define spalloc_new(type) malloc (sizeof (type))
#define spalloc_alloc(size) malloc (size)

#define spalloc_free(type, buf) free (buf)
#define spalloc_free1(size, buf) free (buf)

#endif

typedef struct _ShmAllocSpace ShmAllocSpace;
typedef struct _ShmAllocBlock ShmAllocBlock;

ShmAllocSpace *shm_alloc_space_new (size_t size);
void shm_alloc_space_free (ShmAllocSpace * self);


ShmAllocBlock *shm_alloc_space_alloc_block (ShmAllocSpace * self,
    unsigned long size);
unsigned long shm_alloc_space_alloc_block_get_offset (ShmAllocBlock *block);

void shm_alloc_space_block_inc (ShmAllocBlock * block);
void shm_alloc_space_block_dec (ShmAllocBlock * block);
ShmAllocBlock * shm_alloc_space_block_get (ShmAllocSpace * space,
    unsigned long offset);


#ifdef __cplusplus
}
#endif

#endif /* __SHMALLOC_H__ */
