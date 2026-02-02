#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "custom_alloc.h"

/* ---------- 1. Конфигурация ---------- */

/* Количество блоков в каждом пуле */
#ifndef SMALL_POOL_BLOCKS
#define SMALL_POOL_BLOCKS 1024
#endif

#ifndef LARGE_POOL_BLOCKS
#define LARGE_POOL_BLOCKS 128
#endif

/* Размеры блоков */
enum
{
    REQ_SMALL = 15,
    REQ_LARGE = 180
};

/* Размер указателя x64 - 8 байт, x32 - 4 байта, x16 - 2 байта
   Размер указателя = размер адреса */
enum
{
    PTR_SZ = sizeof(void *)
};

/* Каждый блок должен быть не меньше, чем sizeof(void*) */
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* Полезный размер блока. Если пользователь попросил меньше,
   чем размер указателя — мы увеличиваем Иначе оставляем как есть */
enum
{
    BLOCK_PAYLOAD_SMALL = MAX(REQ_SMALL, PTR_SZ),
    BLOCK_PAYLOAD_LARGE = MAX(REQ_LARGE, PTR_SZ)
};

/* Округление в слова размером PTR_SZ = ceil (округление вверх)
   Количество "слов" в payload (где слово = PTR_SZ байт) */
enum
{
    BLOCK_WORDS_SMALL = (BLOCK_PAYLOAD_SMALL + PTR_SZ - 1) / PTR_SZ,
    BLOCK_WORDS_LARGE = (BLOCK_PAYLOAD_LARGE + PTR_SZ - 1) / PTR_SZ
};

/* Итоговый байтовый размер блока (кратный PTR_SZ) */
enum
{
    BLOCK_BYTES_SMALL = BLOCK_WORDS_SMALL * PTR_SZ,
    BLOCK_BYTES_LARGE = BLOCK_WORDS_LARGE * PTR_SZ
};

/* Размер пулов в "словах" (uintptr_t) для объявления массивов uintptr_t */
enum
{
    POOL_SMALL_WORDS = SMALL_POOL_BLOCKS * BLOCK_WORDS_SMALL,
    POOL_LARGE_WORDS = LARGE_POOL_BLOCKS * BLOCK_WORDS_LARGE
};

/* ---------- 2. Пулы памяти (статические массивы, выровненные по uintptr_t) ---------- */

/* Используем uintptr_t массивы: они обеспечивают выравнивание */
static uintptr_t pool_small[POOL_SMALL_WORDS];
static uintptr_t pool_large[POOL_LARGE_WORDS];

/* Головные указатели свободных списков (хранят адрес блока) */
static void *free_small = NULL;
static void *free_large = NULL;

/* Флаг инициализации пулов */
static bool pools_inited = false;

/* ---------- 3. Вспомогательные inline функции ---------- */

/* Адрес начала пула */
static inline void *pool_small_begin(void)
{
    return (void *)pool_small;
}

/* Длина пула в байтах */
static inline size_t pool_small_size_bytes(void)
{
    return (size_t)POOL_SMALL_WORDS * (size_t)PTR_SZ;
}

static inline void *pool_large_begin(void)
{
    return (void *)pool_large;
}

static inline size_t pool_large_size_bytes(void)
{
    return (size_t)POOL_LARGE_WORDS * (size_t)PTR_SZ;
}

/* Проверка: принадлежит ли ptr пулу и корректно
   ли выровнен относительно блока */
static inline bool ptr_in_pool_and_aligned(void *ptr, void *pool_begin, size_t pool_bytes, size_t block_bytes)
{
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t b = (uintptr_t)pool_begin;
    if (p < b || p >= b + pool_bytes)
        return false;
    /* должно начинаться на границе блока */
    return ((p - b) % block_bytes) == 0;
}

/* ---------- 4. Инициализация пулов (создание свободных списков) ---------- */

static void init_pools(void)
{
    if (pools_inited)
        return;

    /* small pool */
    {
        uint8_t *base = (uint8_t *)pool_small;
        for (size_t i = 0; i < SMALL_POOL_BLOCKS; ++i)
        {
            uint8_t *blk = base + i * BLOCK_BYTES_SMALL;
            uint8_t *next = (i + 1 < SMALL_POOL_BLOCKS) ? (base + (i + 1) * BLOCK_BYTES_SMALL) : NULL;

            /* в состоянии "free" первые PTR_SZ байт блока
               используются как указатель на следующий */
            void **next_ptr = (void **)blk;
            *next_ptr = (void *)next;
        }
        free_small = (void *)base;
    }

    /* large pool */
    {
        uint8_t *base = (uint8_t *)pool_large;
        for (size_t i = 0; i < LARGE_POOL_BLOCKS; ++i)
        {
            uint8_t *blk = base + i * BLOCK_BYTES_LARGE;
            uint8_t *next = (i + 1 < LARGE_POOL_BLOCKS) ? (base + (i + 1) * BLOCK_BYTES_LARGE) : NULL;
            void **next_ptr = (void **)blk;
            *next_ptr = (void *)next;
        }
        free_large = (void *)base;
    }

    pools_inited = true;
}

/* ---------- 5. malloc и free ---------- */

void *malloc(size_t size)
{
    if (!pools_inited)
        init_pools();
    if (size == 0)
        size = 1;

    if (size <= REQ_SMALL)
    {
        if (!free_small)
            return NULL;
        void *blk = free_small;
        free_small = *((void **)free_small);
        return blk;
    }
    else if (size <= REQ_LARGE)
    {
        if (!free_large)
            return NULL;
        void *blk = free_large;
        free_large = *((void **)free_large);
        return blk;
    }
    else
    {
        return NULL;
    }
}

void free(void *ptr)
{
    if (!ptr)
        return;
    if (!pools_inited)
    {
        return;
    }

    if (ptr_in_pool_and_aligned(ptr, pool_small_begin(), pool_small_size_bytes(), BLOCK_BYTES_SMALL))
    {
        /* записываем текущую голову в первые bytes освободённого блока */
        *((void **)ptr) = free_small;
        free_small = ptr;
        return;
    }

    if (ptr_in_pool_and_aligned(ptr, pool_large_begin(), pool_large_size_bytes(), BLOCK_BYTES_LARGE))
    {
        *((void **)ptr) = free_large;
        free_large = ptr;
        return;
    }

    return;
}

/* ---------- 6. Вспомогательная функция для отладки ---------- */

#ifdef CUSTOM_ALLOC_DEBUG
#include <stdio.h>
/* Вывод состояния пулов: сколько свободных блоков осталось */
void custom_alloc_debug_print(void)
{
    size_t count = 0;
    void *p = free_small;
    while (p)
    {
        ++count;
        p = *((void **)p);
    }
    printf("Количество свободных маленьких блоков: %zu (каждый %u байт)\n", count, (unsigned)BLOCK_BYTES_SMALL);

    count = 0;
    p = free_large;
    while (p)
    {
        ++count;
        p = *((void **)p);
    }
    printf("Количество свободных больших блоков: %zu (каждый %u байт)\n", count, (unsigned)BLOCK_BYTES_LARGE);
    printf("\n");
}
#endif