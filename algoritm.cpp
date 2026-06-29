#include "shaker_sort.h"
#include <string.h>
#include <time.h>

/* ═══════════════════════════════════════════════════════════════════
 *  РЕАЛИЗАЦИЯ АЛГОРИТМА ШЕЙКЕРНОЙ СОРТИРОВКИ
 * ═══════════════════════════════════════════════════════════════════ */

void push_step(App* app, Step* s) {
    if (app->step_count >= app->step_alloc) {
        int new_alloc = app->step_alloc ? app->step_alloc * 2 : 512;
        Step* tmp = (Step*)realloc(app->steps, new_alloc * sizeof(Step));
        if (!tmp) return;
        app->steps = tmp;
        app->step_alloc = new_alloc;
    }
    app->steps[app->step_count++] = *s;
}

void collect_steps(App* app) {
    app->step_count = 0;
    app->trace_count = 0;

    int a[MAX_N];
    memcpy(a, app->original, app->n * sizeof(int));
    int n = app->n;

    int left = 0;
    int right = n - 1;
    int pass = 1;
    int cmp = 0;
    int swaps = 0;

    Step s;
    memset(&s, 0, sizeof(s));
    s.n = n;
    s.kind = STEP_COMPARE;

    while (left < right) {
        int last_swap = -1;

        /* Проход слева направо (вверх) */
        for (int i = left; i < right; i++) {
            cmp++;
            s.kind = STEP_COMPARE;
            s.i = i;
            s.j = i + 1;
            s.left = left;
            s.right = right;
            s.pass = pass;
            s.dir = 1;
            s.cmp_total = cmp;
            s.swap_total = swaps;
            memcpy(s.arr, a, n * sizeof(int));
            push_step(app, &s);

            if (a[i] > a[i + 1]) {
                int t = a[i];
                a[i] = a[i + 1];
                a[i + 1] = t;
                swaps++;
                last_swap = i;
                s.kind = STEP_SWAP;
                s.i = i;
                s.j = i + 1;
                s.cmp_total = cmp;
                s.swap_total = swaps;
                memcpy(s.arr, a, n * sizeof(int));
                push_step(app, &s);
            }
        }
        right = (last_swap >= 0) ? last_swap : left;

        s.kind = STEP_PASS_END;
        s.left = left;
        s.right = right;
        s.pass = pass;
        s.dir = 1;
        s.cmp_total = cmp;
        s.swap_total = swaps;
        memcpy(s.arr, a, n * sizeof(int));
        push_step(app, &s);

        if (app->trace_count < 64) {
            snprintf(app->trace[app->trace_count++], 128,
                "Pass %2d^  L=%-3d R=%-3d  Swap: %d",
                pass, left, right, swaps);
        }

        if (left >= right) break;

        last_swap = -1;
        /* Проход справа налево (вниз) */
        for (int i = right; i > left; i--) {
            cmp++;
            s.kind = STEP_COMPARE;
            s.i = i - 1;
            s.j = i;
            s.left = left;
            s.right = right;
            s.pass = pass;
            s.dir = -1;
            s.cmp_total = cmp;
            s.swap_total = swaps;
            memcpy(s.arr, a, n * sizeof(int));
            push_step(app, &s);

            if (a[i] < a[i - 1]) {
                int t = a[i];
                a[i] = a[i - 1];
                a[i - 1] = t;
                swaps++;
                last_swap = i;
                s.kind = STEP_SWAP;
                s.i = i - 1;
                s.j = i;
                s.cmp_total = cmp;
                s.swap_total = swaps;
                memcpy(s.arr, a, n * sizeof(int));
                push_step(app, &s);
            }
        }
        left = (last_swap >= 0) ? last_swap : right;

        s.kind = STEP_PASS_END;
        s.left = left;
        s.right = right;
        s.pass = pass;
        s.dir = -1;
        s.cmp_total = cmp;
        s.swap_total = swaps;
        memcpy(s.arr, a, n * sizeof(int));
        push_step(app, &s);

        if (app->trace_count < 64) {
            snprintf(app->trace[app->trace_count++], 128,
                "Pass %2dv  L=%-3d R=%-3d  Swap: %d",
                pass, left, right, swaps);
        }
        pass++;
    }

    /* Финальный шаг - сортировка завершена */
    s.kind = STEP_DONE;
    s.left = 0;
    s.right = n - 1;
    s.cmp_total = cmp;
    s.swap_total = swaps;
    memcpy(s.arr, a, n * sizeof(int));
    push_step(app, &s);

    app->total_cmp = cmp;
    app->total_swaps = swaps;
    app->total_passes = pass - 1;

    if (app->trace_count < 64) {
        snprintf(app->trace[app->trace_count++], 128,
            "Done: %d swaps, %d comps", swaps, cmp);
    }
}

void start_sort(App* app) {
    if (app->n < 2) return;

    clock_t start = clock();
    collect_steps(app);
    clock_t end = clock();
    app->elapsed_ms = (double)(end - start) * 1000.0 / CLOCKS_PER_SEC;

    app->step_idx = 0;
    app->state = STATE_SORTING;
    app->result_saved = 0;
    app->last_tick = SDL_GetTicks();

    snprintf(app->status_msg, sizeof(app->status_msg),
        loc(app->lang,
            "Sorting %d elements...",
            "Сортировка %d элементов..."),
        app->n);
}

int is_sorting_complete(App* app) {
    if (app->step_count == 0) return 0;
    Step* last_step = &app->steps[app->step_count - 1];
    return (last_step->kind == STEP_DONE);
}

Step* get_current_step(App* app) {
    if (app->step_count == 0) return NULL;
    int idx = app->step_idx;
    if (idx >= app->step_count) idx = app->step_count - 1;
    return &app->steps[idx];
}

int advance_step(App* app) {
    if (app->step_idx < app->step_count) {
        Step* s = &app->steps[app->step_idx];
        app->step_idx++;

        if (s->kind == STEP_DONE) {
            app->state = STATE_DONE;
            snprintf(app->status_msg, sizeof(app->status_msg),
                loc(app->lang,
                    "DONE! %d elem, %d cmp, %d swaps, %.3f ms. S=save",
                    "ГОТОВО! %d эл, %d сравн, %d обменов, %.3f мс. S=сохр"),
                app->n, app->total_cmp, app->total_swaps, app->elapsed_ms);
            return 0;
        }
        return 1;
    }
    return 0;
}