#ifndef _LIB__VECTOR_H
#define _LIB__VECTOR_H

#include <stddef.h>
#include <lib/alloc.h>
#include <lib/libc.h>

#define VECTOR_INVALID_INDEX ((size_t)-1)

#define VECTOR_INIT {0}

#define VECTOR_TYPE(TYPE) \
    struct { \
        TYPE *data; \
        size_t length; \
        size_t capacity; \
    }

#define VECTOR_ENSURE_LENGTH(VEC, LENGTH) ({ \
    typeof(VEC) *VECTOR_ENSURE_LENGTH_vec = &(VEC); \
    if ((LENGTH) >= VECTOR_ENSURE_LENGTH_vec->capacity) { \
        if (VECTOR_ENSURE_LENGTH_vec->capacity == 0) { \
            VECTOR_ENSURE_LENGTH_vec->capacity = 8; \
        } else { \
            VECTOR_ENSURE_LENGTH_vec->capacity *= 2; \
        } \
        VECTOR_ENSURE_LENGTH_vec->data = realloc(VECTOR_ENSURE_LENGTH_vec->data, \
            VECTOR_ENSURE_LENGTH_vec->capacity * sizeof(*VECTOR_ENSURE_LENGTH_vec->data)); \
    } \
})

#define VECTOR_PUSH_BACK(VEC, VALUE) ({ \
    typeof(VEC) *VECTOR_PUSH_BACK_vec = &(VEC); \
    VECTOR_ENSURE_LENGTH(VEC, VECTOR_PUSH_BACK_vec->length); \
    VECTOR_PUSH_BACK_vec->data[VECTOR_PUSH_BACK_vec->length++] = VALUE; \
    VECTOR_PUSH_BACK_vec->length - 1; \
})

#define VECTOR_PUSH_FRONT(VEC, VALUE) VECTOR_INSERT(VEC, 0, VALUE)

#define VECTOR_INSERT(VEC, IDX, VALUE) ({ \
    typeof(VEC) *VECTOR_INSERT_vec = &(VEC); \
    size_t VECTOR_INSERT_index = IDX; \
    VECTOR_ENSURE_LENGTH(VEC, VECTOR_INSERT_vec->length); \
    for (size_t i = VECTOR_INSERT_vec->length; i > VECTOR_INSERT_index; i--) { \
        VECTOR_INSERT_vec->data[i] = VECTOR_INSERT_vec->data[i - 1]; \
    } \
    VECTOR_INSERT_vec->length++; \
    VECTOR_INSERT_vec->data[VECTOR_INSERT_index] = VALUE; \
})

#define VECTOR_REMOVE(VEC, IDX) ({ \
    typeof(VEC) *VECTOR_REMOVE_vec = &(VEC); \
    for (size_t i = (IDX); i < VECTOR_REMOVE_vec->length - 1; i++) { \
        VECTOR_REMOVE_vec->data[i] = VECTOR_REMOVE_vec->data[i + 1]; \
    } \
})

#define VECTOR_ITEM(VEC, IDX) ((VEC).data[IDX])

#define VECTOR_FIND(VEC, VALUE) ({ \
    typeof(VEC) *VECTOR_FIND_vec = &(VEC); \
    size_t VECTOR_FIND_result = VECTOR_INVALID_INDEX; \
    for (size_t i = 0; i < VECTOR_FIND_vec->length; i++) { \
        if (VECTOR_FIND_vec->data[i] == (VALUE)) { \
            VECTOR_FIND_result = i; \
            break; \
        } \
    } \
    VECTOR_FIND_result; \
})

#define VECTOR_FOR_EACH(VEC, BINDING) \
    for (typeof((VEC).data) BINDING = (VEC).data; \
        BINDING != (VEC).data + (VEC).length; BINDING++)

#endif
