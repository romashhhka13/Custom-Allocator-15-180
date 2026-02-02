#include <stdio.h>
#include <stdint.h>
#include "custom_alloc.h"

int main(void)
{
    printf(" ---------- Тест кастомного аллокатора ---------- \n");

#ifdef CUSTOM_ALLOC_DEBUG
    custom_alloc_debug_print();
#endif

    /* ---------- Выделение 15 байт ---------- */
    void *p1 = malloc(15);
    if (p1)
        printf("Выделили 15 байт по адресу %p\n", p1);
#ifdef CUSTOM_ALLOC_DEBUG
    custom_alloc_debug_print();
#endif

    /* ---------- Выделение 180 байт ---------- */
    void *p2 = malloc(180);
    if (p2)
        printf("Выделили 180 байт по адресу %p\n", p2);
#ifdef CUSTOM_ALLOC_DEBUG
    custom_alloc_debug_print();
#endif

    /* ---------- Освобождение ---------- */
    free(p1);
    printf("Освободили 15 байт\n");
    free(p2);
    printf("Освободили 180 байт\n");
#ifdef CUSTOM_ALLOC_DEBUG
    custom_alloc_debug_print();
#endif

    /* ---------- Выделение нескольких маленьких блоков для 15 байт ---------- */
    void *arr[10];
    for (int i = 0; i < 10; i++)
    {
        arr[i] = malloc(15);
        printf("arr[%d] = %p\n", i, arr[i]);
    }
    printf("Выделили 10 блоков по 15 байт\n");
#ifdef CUSTOM_ALLOC_DEBUG
    custom_alloc_debug_print();
#endif
    for (int i = 0; i < 10; i++)
    {
        free(arr[i]);
    }
    printf("Освободили 10 блоков по 15 байт\n");
#ifdef CUSTOM_ALLOC_DEBUG
    custom_alloc_debug_print();
#endif

    printf("Тест завершён\n");
    return 0;
}
