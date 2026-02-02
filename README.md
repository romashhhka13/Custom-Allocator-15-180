# Кастомный аллокатор (C)

- [Особенности](#особенности)
- [Сборка на UNIX-системе](#сборка-на-unix-системе)
    - [Release](#release)
    - [Debug](#debug)
    - [Настройка размера пулов при сборке](#настройка-размера-пулов-при-сборке)
- [Запуск на UNIX-системе](#запуск-на-unix-системе)
    - [Release](#release-1)
    - [Debug](#debug-1)
- [Описание работы аллокатора](#описание-работы-аллокатора)
    - [Заголовки, конфигурация и ключевые константы](#заголовки-конфигурация-и-ключевые-константы)
    - [Пулы памяти и указатели свободных списков](#пулы-памяти-и-указатели-свободных-списков)
    - [Вспомогательные inline-функции](#вспомогательные-inline-функции)
    - [Инициализация пулов (создание free list)](#инициализация-пулов-создание-free-list)
    - [Основные функции: `malloc` и `free`](#основные-функции-malloc-и-free)
    - [Отладочная функция](#отладочная-функция)
- [Быстродействие и эффективность работы с памятью](#быстродействие-и-эффективность-работы-с-памятью)
    - [Быстродействие](#быстродействие)
    - [Эффективность использования памяти](#эффективность-использования-памяти)
- [Возможные улучшения](#возможные-улучшения)

## Особенности

Реализация простого и быстрого аллокатора памяти для двух фиксированных размеров 15 и 180 байт
- Реализованы функции `malloc` и `free`
- O(1) время работы для `malloc/free`
- Используются два пула фиксированного размера:
    - `small` — для запросов <= 15 байт;
    - `large` — для запросов <= 180 байт.
- Каждый пул — статический массив `uintptr_t` (гарантирует выравнивание), разбитый на блоки одинакового размера
- Учитывает размер указателя для разных архитектур (8/16/32-битные архитектуры)
- Свободные блоки связаны однонаправленным ***free list***: в первые `sizeof(void*)` байт свободного блока записывается указатель на следующий свободный блок

## Сборка на UNIX-системе

### Release
Для сборки бинарника воспользуйтесь в терминале следующей командой:
```bash
./build.sh Release
```
или:
```bash
./build.sh
```

### Debug
```bash
./build.sh Debug
```

### Настройка размера пулов при сборке

Через переменные окружения при сборке:
```bash
SMALL_POOL_BLOCKS=512 LARGE_POOL_BLOCKS=64 ./build.sh Release
```
```bash
SMALL_POOL_BLOCKS=512 LARGE_POOL_BLOCKS=64 ./build.sh Debug
```
или прямо в коде
```cpp
#ifndef SMALL_POOL_BLOCKS
#define SMALL_POOL_BLOCKS 1024 // нужно изменить значение
#endif
```



## Запуск на UNIX-системе

### Release
```bash
./build-Release/custom_alloc
```

### Debug
```bash
./build-Debug/custom_alloc
```


## Описание работы аллокатора

### Заголовки, конфигурация и ключевые константы

Здесь определяются параметры сборки (количество блоков в каждом пуле), запрошенные пользователем размеры (15 и 180), а также вычисляются внутренние размеры блоков с учётом выравнивания по `sizeof(void *)`.

Мы округляем payload (размер одного блока - 15/180) вверх до целого числа «слов» размером `PTR_SZ` — это обеспечивает корректное выравнивание на 8/16/32-битных платформах. Также гарантируем, что полезная часть блока не меньше `sizeof(void*)`, потому что в свободном блоке первые байты используются для хранения указателя `next`.

```cpp
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "custom_alloc.h"

/* ---------- Конфигурация ---------- */
/* Количество блоков в каждом пуле */
#ifndef SMALL_POOL_BLOCKS
#define SMALL_POOL_BLOCKS 1024
#endif

#ifndef LARGE_POOL_BLOCKS
#define LARGE_POOL_BLOCKS 128
#endif

/* Запрошенные размеры */
enum
{
    REQ_SMALL = 15,
    REQ_LARGE = 180
};

/* Размер указателя (в байтах), работает для 8/16/32/64-бит */
enum
{
    PTR_SZ = sizeof(void *)
};

/* Макрос для выбора максимума */
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* Полезный размер блока: не меньше PTR_SZ */
enum
{
    BLOCK_PAYLOAD_SMALL = MAX(REQ_SMALL, PTR_SZ),
    BLOCK_PAYLOAD_LARGE = MAX(REQ_LARGE, PTR_SZ)
};

/* Количество "слов" (где слово = PTR_SZ байт), округление вверх */
enum
{
    BLOCK_WORDS_SMALL = (BLOCK_PAYLOAD_SMALL + PTR_SZ - 1) / PTR_SZ,
    BLOCK_WORDS_LARGE = (BLOCK_PAYLOAD_LARGE + PTR_SZ - 1) / PTR_SZ
};

/* Итоговый размер блока в байтах (выровнен по PTR_SZ) */
enum
{
    BLOCK_BYTES_SMALL = BLOCK_WORDS_SMALL * PTR_SZ,
    BLOCK_BYTES_LARGE = BLOCK_WORDS_LARGE * PTR_SZ
};

/* Размер пулов в "словах" для объявления массивов uintptr_t */
enum
{
    POOL_SMALL_WORDS = SMALL_POOL_BLOCKS * BLOCK_WORDS_SMALL,
    POOL_LARGE_WORDS = LARGE_POOL_BLOCKS * BLOCK_WORDS_LARGE
};
```

### Пулы памяти и указатели свободных списков
Пулы реализованы как статические массивы `uintptr_t`— это обеспечивает корректное выравнивание памяти под указатели на любой платформе. В этой части также объявляются глобальные указатели на головы списков свободных блоков (`free_small`, `free_large`) и флаг  инициализации пулов `pools_inited`.

```cpp
/* Используем uintptr_t массивы: они обеспечивают выравнивание
   подходящее для указателей */
static uintptr_t pool_small[POOL_SMALL_WORDS];
static uintptr_t pool_large[POOL_LARGE_WORDS];

/* Головные указатели свободных списков (хранят адрес блока) */
static void *free_small = NULL;
static void *free_large = NULL;

/* флаг инициализации пулов (ленивая инициализация) */
static bool pools_inited = false;
```

### Вспомогательные inline-функции

Inline-функции позволяют вычислять начало пула и его длину в байтах, а также проверять, принадлежит ли указатель пулу и выровнен ли он по границе блока. Это нужно, чтобы free мог определить, в какой пул вернуть блок, не требуя от пользователя передавать размер при освобождении.

```cpp
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
```

### Инициализация пулов (создание free list)

Функция `init_pools()` выполняет ленивую инициализацию: при первом вызове `malloc/free` она последовательно раскладывает блоки в массивы и формирует однонаправленные списки свободных блоков. В каждой ячейке свободного блока первые `PTR_SZ` байт используются для хранения указателя на следующий свободный блок.

```cpp
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
            /* в состоянии "free" первые PTR_SZ байт блока используются как указатель на следующий */
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
```

### Основные функции: `malloc` и `free`

`malloc(size_t size)`:
- Инициализирует пулы при первом вызове (если нужно).
- Нормирует `size == 0` в `size = 1` (как делает стандартный `malloc`).
- Если `size <= REQ_SMALL`, пытается взять блок из `free_small`; если пуст — возвращает `NULL`.
- Если `REQ_SMALL < size <= REQ_LARGE`, аналогично для `free_large`.
- Если `size > REQ_LARGE`, возвращает `NULL` (не поддерживается).

`free(void *ptr)`:
- Если `ptr == NULL` — ничего не делает.
- Определяет, к какому пулу принадлежит `ptr` (через `ptr_in_pool_and_aligned`) и возвращает блок в голову соответствующего списка.
- Если `ptr` не принадлежит ни одному пулу — игнорирует
```cpp
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
        /* записываем текущую голову в первые bytes освобождённого блока */
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
```

### Отладочная функция

Если при компиляции указан макрос `CUSTOM_ALLOC_DEBUG`, доступна функция `custom_alloc_debug_print()`, которая печатает количество свободных блоков в каждом пуле. Это удобно для тестов: проверить исчерпание пула и повторное использование адресов.

```cpp
#ifdef CUSTOM_ALLOC_DEBUG
#include <stdio.h>
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
```

## Быстродействие и эффективность работы с памятью

### Быстродействие
- Операции malloc и free выполняются за O(1).
- Не используется поиск подходящего блока, сортировка или объединение областей памяти.
- Нет системных вызовов или взаимодействия с ОС во время работы аллокатора.
- Используется простая структура данных — однонаправленный список свободных блоков (free list).

### Эффективность использования памяти
- Используются фиксированные блоки, что полностью исключает внешнюю фрагментацию.
- Размер каждого блока выровнен по sizeof(void*), что обеспечивает:
    - корректную работу с указателями,
    - отсутствие неопределённого поведения,
    - эффективный доступ к памяти на всех целевых архитектурах.
- Свободные блоки не требуют дополнительных структур — указатель next хранится прямо в памяти блока.

## Возможные улучшения

1. ***Потокобезопасность***. Добавление mutex или spinlock вокруг операций malloc и free.
2. ***Fallback при нехватке памяти***. Аллокатор использует заранее выделенные пулы памяти фиксированного размера. При исчерпании внутренних пулов аллокатор может не расширять их, а делегировать выделение памяти стандартному системному malloc в качестве запасного варианта.

