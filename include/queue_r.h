/**
 * @brief   Thread-safe queue
 */

/**
 * @addtogroup queue_r
 * @{
 */

#pragma once
#ifndef QUEUE_R_H
#define QUEUE_R_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * Queue control block.
 */
typedef struct queue_cb {
    size_t b_size;  /*!< Block size in bytes. */
    size_t a_len;   /*!< Array length. */
    size_t m_write; /*!< Write index. */
    size_t m_read;  /*!< Read index. */
} queue_cb_t;

#define QUEUE_INITIALIZER(block_size, array_size)                   \
    (struct queue_cb)                                               \
    {                                                               \
        .b_size = (block_size), .a_len = (array_size / block_size), \
        .m_write = 0, .m_read = 0,                                  \
    }

/**
 * Create a new queue control block.
 * Initializes a new queue control block and returns it as a value.
 * @param block_size the size of single data block/struct/data type in
 *                   data_array in bytes.
 * @param arra_size the size of the data_array in bytes.
 * @return a new queue_cb_t queue control block structure.
 */
static inline queue_cb_t queue_create(size_t block_size, size_t array_size)
{
    queue_cb_t cb = {.b_size = block_size,
                     .a_len = array_size / block_size,
                     .m_read = 0,
                     .m_write = 0};
    return cb;
}

/**
 * Allocate an element from the queue.
 * @param cb is a pointer to the queue control block.
 */
static inline int queue_alloc(queue_cb_t *cb)
{
    const size_t write = cb->m_write;
    const size_t next_element = (write + 1) % cb->a_len;
    const size_t b_size = cb->b_size;

    /* Check that the queue is not full */
    if (next_element == cb->m_read)
        return -1;

    return write * b_size;
}

/**
 * Commit previous allocation from the queue.
 */
static inline void queue_commit(queue_cb_t *cb)
{
    const size_t next_element = (cb->m_write + 1) % cb->a_len;
    cb->m_write = next_element;
}

/**
 * Peek an element from the queue.
 * @param cb is a pointer to the queue control block.
 * @param index is the location where element is located in the buffer.
 * @return 0 if queue is empty; otherwise operation was succeed.
 */
static inline int queue_peek(queue_cb_t *cb, int *index)
{
    const size_t read = cb->m_read;
    const size_t b_size = cb->b_size;

    /* Check that the queue is not empty */
    if (read == cb->m_write)
        return 0;

    *index = read * b_size;

    return 1;
}

/**
 * Discard n number of elements in the queue from the read end.
 * @param cb is a pointer to the queue control block.
 * @return Returns the number of elements skipped.
 */
static inline int queue_discard(queue_cb_t *cb, size_t n)
{
    size_t count;
    for (count = 0; count < n; count++) {
        const size_t read = cb->m_read;

        /* Check that the queue is not empty */
        if (read == cb->m_write)
            break;

        cb->m_read = (read + 1) % cb->a_len;
    }
    return count;
}

/**
 * Clear the queue.
 * This operation is considered safe when committed from the push end thread.
 * @param cb is a pointer to the queue control block.
 */
static inline void queue_clear_from_push_end(queue_cb_t *cb)
{
    cb->m_write = cb->m_read;
}

/**
 * Clear the queue.
 * This operation is considered safe when committed from the pop end thread.
 * @param cb is a pointer to the queue control block.
 */
static inline void queue_clear_from_pop_end(queue_cb_t *cb)
{
    cb->m_read = cb->m_write;
}

/**
 * Check if the queue is empty.
 * @param cb is a pointer to the queue control block.
 * @return 0 if the queue is not empty.
 */
static inline int queue_isempty(queue_cb_t *cb)
{
    return (int) (cb->m_write == cb->m_read);
}

/**
 * Check if the queue is full.
 * @param cb is a pointer to the queue control block.
 * @return 0 if the queue is not full.
 */
static inline int queue_isfull(queue_cb_t *cb)
{
    return (int) (((cb->m_write + 1) % cb->a_len) == cb->m_read);
}

#endif /* QUEUE_R_H */

/**
 * @}
 */
