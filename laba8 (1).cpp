#define _CRT_SECURE_NO_WARNINGS
#define _POSIX_C_SOURCE 200809L
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ─────────────────────────── константы окна ─────────────────────── */
#define WIN_W        1000
#define WIN_H         680
#define BAR_AREA_X    40
#define BAR_AREA_Y    90
#define BAR_AREA_W   920
#define BAR_AREA_H   380
#define PANEL_Y      490
#define FPS           60

/* ─────────────────────────── файлы ──────────────────────────────── */
#define INPUT_FILE   "input.txt"
#define OUTPUT_FILE  "output.txt"
#define MAX_N        128

/* ─────────────────────────── цветовая схема ─────────────────────── */
typedef struct { Uint8 r, g, b, a; } Color;

static inline Color make_color(Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    Color c = { r, g, b, a };
    return c;
}

#define C_BG          make_color(15, 17, 23, 255)
#define C_SURFACE     make_color(25, 28, 42, 255)
#define C_PANEL       make_color(20, 23, 36, 255)
#define C_BAR_NORMAL  make_color(72, 111, 220, 255)
#define C_BAR_CMP     make_color(240, 192,  60, 255)
#define C_BAR_SWAP    make_color(224,  80, 100, 255)
#define C_BAR_LEFT    make_color(100, 210, 160, 255)
#define C_BAR_RIGHT   make_color(210, 130, 220, 255)
#define C_BAR_DONE    make_color( 60, 190, 120, 255)
#define C_TEXT        make_color(220, 224, 240, 255)
#define C_MUTED       make_color(110, 120, 150, 255)
#define C_ACCENT      make_color( 72, 111, 220, 255)
#define C_DIVIDER     make_color( 40,  44,  65, 255)
#define C_BUTTON      make_color(35, 38, 52, 255)
#define C_BUTTON_HOVER make_color(55, 58, 72, 255)

/* ─────────────────────────── состояния ──────────────────────────── */
typedef enum {
    STATE_MENU,
    STATE_IDLE,
    STATE_SORTING,
    STATE_PAUSED,
    STATE_DONE
} AppState;

/* ─────────────────────────── шаг алгоритма ─────────────────────── */
typedef enum {
    STEP_COMPARE,
    STEP_SWAP,
    STEP_PASS_END,
    STEP_DONE
} StepKind;

typedef struct {
    StepKind kind;
    int      arr[MAX_N];
    int      n;
    int      i, j;
    int      left, right;
    int      pass;
    int      dir;
    int      cmp_total;
    int      swap_total;
} Step;

/* ─────────────────────────── главная структура ──────────────────── */
typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;

    int  original[MAX_N];
    int  n;

    Step* steps;
    int   step_count;
    int   step_alloc;
    int   step_idx;

    AppState state;
    int      speed;
    Uint64   last_tick;

    int  total_cmp;
    int  total_swaps;
    int  total_passes;
    double elapsed_ms;

    int  result_saved;
    char status_msg[256];
    char selected_file[256];

    char trace[64][128];
    int  trace_count;
    int  trace_capacity;

    int menu_button_hover;  /* 0-2: START, SELECT, EXIT */
    int lang;               /* 0=EN, 1=RU */
    int lang_btn_hover;
} App;

/* ─────────────────────────── локализация ───────────────────────── */
static const char* loc(int lang, const char* en, const char* ru) {
    return (lang == 1) ? ru : en;
}

/* ═══════════════════════════════════════════════════════════════════
 *  СБОР ШАГОВ АЛГОРИТМА
 * ═══════════════════════════════════════════════════════════════════ */

static void push_step(App* app, Step* s)
{
    if (app->step_count >= app->step_alloc) {
        int new_alloc = app->step_alloc ? app->step_alloc * 2 : 512;
        Step* tmp = (Step*)realloc(app->steps, new_alloc * sizeof(Step));
        if (!tmp) return;
        app->steps = tmp;
        app->step_alloc = new_alloc;
    }
    app->steps[app->step_count++] = *s;
}

static void collect_steps(App* app)
{
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

        for (int i = left; i < right; i++) {
            cmp++;
            s.kind = STEP_COMPARE; s.i = i; s.j = i + 1;
            s.left = left; s.right = right; s.pass = pass; s.dir = 1;
            s.cmp_total = cmp; s.swap_total = swaps;
            memcpy(s.arr, a, n * sizeof(int));
            push_step(app, &s);

            if (a[i] > a[i + 1]) {
                int t = a[i]; a[i] = a[i + 1]; a[i + 1] = t;
                swaps++; last_swap = i;
                s.kind = STEP_SWAP; s.i = i; s.j = i + 1;
                s.cmp_total = cmp; s.swap_total = swaps;
                memcpy(s.arr, a, n * sizeof(int));
                push_step(app, &s);
            }
        }
        right = (last_swap >= 0) ? last_swap : left;

        s.kind = STEP_PASS_END; s.left = left; s.right = right;
        s.pass = pass; s.dir = 1;
        s.cmp_total = cmp; s.swap_total = swaps;
        memcpy(s.arr, a, n * sizeof(int));
        push_step(app, &s);

        if (app->trace_count < 64) {
            snprintf(app->trace[app->trace_count++], 128,
                "Pass %2d^  L=%-3d R=%-3d  Swap: %d",
                pass, left, right, swaps);
        }

        if (left >= right) break;

        last_swap = -1;
        for (int i = right; i > left; i--) {
            cmp++;
            s.kind = STEP_COMPARE; s.i = i - 1; s.j = i;
            s.left = left; s.right = right; s.pass = pass; s.dir = -1;
            s.cmp_total = cmp; s.swap_total = swaps;
            memcpy(s.arr, a, n * sizeof(int));
            push_step(app, &s);

            if (a[i] < a[i - 1]) {
                int t = a[i]; a[i] = a[i - 1]; a[i - 1] = t;
                swaps++; last_swap = i;
                s.kind = STEP_SWAP; s.i = i - 1; s.j = i;
                s.cmp_total = cmp; s.swap_total = swaps;
                memcpy(s.arr, a, n * sizeof(int));
                push_step(app, &s);
            }
        }
        left = (last_swap >= 0) ? last_swap : right;

        s.kind = STEP_PASS_END; s.left = left; s.right = right;
        s.pass = pass; s.dir = -1;
        s.cmp_total = cmp; s.swap_total = swaps;
        memcpy(s.arr, a, n * sizeof(int));
        push_step(app, &s);

        if (app->trace_count < 64) {
            snprintf(app->trace[app->trace_count++], 128,
                "Pass %2dv  L=%-3d R=%-3d  Swap: %d",
                pass, left, right, swaps);
        }
        pass++;
    }

    s.kind = STEP_DONE; s.left = 0; s.right = n - 1;
    s.cmp_total = cmp; s.swap_total = swaps;
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

/* ═══════════════════════════════════════════════════════════════════
 *  ВВОД / ВЫВОД ФАЙЛОВ
 * ═══════════════════════════════════════════════════════════════════ */

static int load_file(App* app, const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    int n = 0, v;
    while (n < MAX_N && fscanf(f, "%d", &v) == 1)
        app->original[n++] = v;
    fclose(f);
    if (n < 2) return 0;
    app->n = n;
    return 1;
}

static void save_result(App* app)
{
    if (app->state != STATE_DONE) return;
    FILE* f = fopen(OUTPUT_FILE, "w");
    if (!f) {
        snprintf(app->status_msg, sizeof(app->status_msg), "%s",
            loc(app->lang, "Error: cannot open output.txt", "\xd0\x9e\xd1\x88\xd0\xb8\xd0\xb1\xd0\xba\xd0\xb0: \xd0\xbd\xd0\xb5 \xd0\xbc\xd0\xbe\xd0\xb3\xd1\x83 \xd0\xbe\xd1\x82\xd0\xba\xd1\x80\xd1\x8b\xd1\x82\xd1\x8c output.txt"));
        return;
    }
    Step* last = &app->steps[app->step_count - 1];

    fprintf(f, "=== Cocktail Shaker Sort ===\n\n");
    fprintf(f, "Elements: %d\n\n", app->n);

    fprintf(f, "Original array:\n");
    for (int i = 0; i < app->n; i++)
        fprintf(f, "%d%s", app->original[i], i < app->n - 1 ? " " : "\n");

    fprintf(f, "\nSorted array:\n");
    for (int i = 0; i < app->n; i++)
        fprintf(f, "%d%s", last->arr[i], i < app->n - 1 ? " " : "\n");

    fprintf(f, "\nStatistics:\n");
    fprintf(f, "  Comparisons:    %d\n", app->total_cmp);
    fprintf(f, "  Swaps:          %d\n", app->total_swaps);
    fprintf(f, "  Passes:         %d\n", app->total_passes);
    fprintf(f, "  Time:           %.4f ms\n", app->elapsed_ms);

    fprintf(f, "\nTrace:\n");
    for (int i = 0; i < app->trace_count; i++)
        fprintf(f, "  %s\n", app->trace[i]);

    fclose(f);
    app->result_saved = 1;
    snprintf(app->status_msg, sizeof(app->status_msg), "%s",
        loc(app->lang, "Result saved to output.txt", "\xd0\xa0\xd0\xb5\xd0\xb7\xd1\x83\xd0\xbb\xd1\x8c\xd1\x82\xd0\xb0\xd1\x82 \xd1\x81\xd0\xbe\xd1\x85\xd1\x80\xd0\xb0\xd0\xbd\xd1\x91\xd0\xbd \xd0\xb2 output.txt"));
}

/* ═══════════════════════════════════════════════════════════════════
 *  ГЕНЕРАТОР СЛУЧАЙНОГО МАССИВА (больше не используется)
 * ═══════════════════════════════════════════════════════════════════ */

static void randomize(App* app)
{
    int n = 20 + rand() % 21;
    for (int i = 0; i < n; i++)
        app->original[i] = rand() % 201 - 100;
    app->n = n;
    snprintf(app->status_msg, sizeof(app->status_msg),
        loc(app->lang,
            "Random array of %d elements. Press ENTER.",
            "\xd0\xa1\xd0\xbb\xd1\x83\xd1\x87\xd0\xb0\xd0\xb9\xd0\xbd\xd1\x8b\xd0\xb9 \xd0\xbc\xd0\xb0\xd1\x81\xd1\x81\xd0\xb8\xd0\xb2 \xd0\xb8\xd0\xb7 %d \xd1\x8d\xd0\xbb\xd0\xb5\xd0\xbc\xd0\xb5\xd0\xbd\xd1\x82\xd0\xbe\xd0\xb2. \xd0\x9d\xd0\xb0\xd0\xb6\xd0\xbc\xd0\xb8\xd1\x82\xd0\xb5 ENTER."),
        n);
}

/* ═══════════════════════════════════════════════════════════════════
 *  РЕНДЕРИНГ (общие функции)
 * ═══════════════════════════════════════════════════════════════════ */

static void set_color(SDL_Renderer* r, Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static void fill_rect(SDL_Renderer* r, int x, int y, int w, int h, Color c)
{
    set_color(r, c);
    SDL_FRect rc = { (float)x, (float)y, (float)w, (float)h };
    SDL_RenderFillRect(r, &rc);
}

static void draw_rect(SDL_Renderer* r, int x, int y, int w, int h, Color c)
{
    set_color(r, c);
    SDL_FRect rc = { (float)x, (float)y, (float)w, (float)h };
    SDL_RenderRect(r, &rc);
}

/* ── ASCII шрифт 5×7 (символы 32–127) ── */
static const Uint8 FONT5X7[96][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x04,0x04,0x04,0x04,0x00,0x04,0x00}, /* '!' */
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, /* '"' */
    {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, /* '#' */
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, /* '$' */
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, /* '%' */
    {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, /* '&' */
    {0x0C,0x04,0x08,0x00,0x00,0x00,0x00}, /* '\'' */
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, /* '(' */
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, /* ')' */
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, /* '*' */
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, /* '+' */
    {0x00,0x00,0x00,0x00,0x0C,0x04,0x08}, /* ',' */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* '-' */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, /* '.' */
    {0x00,0x01,0x02,0x04,0x08,0x10,0x00}, /* '/' */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* '0' */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* '1' */
    {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, /* '2' */
    {0x1F,0x02,0x04,0x06,0x01,0x11,0x0E}, /* '3' */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* '4' */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* '5' */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* '6' */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* '7' */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* '8' */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* '9' */
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, /* ':' */
    {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08}, /* ';' */
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, /* '<' */
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, /* '=' */
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, /* '>' */
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, /* '?' */
    {0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E}, /* '@' */
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* 'A' */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* 'B' */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* 'C' */
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, /* 'D' */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* 'E' */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* 'F' */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, /* 'G' */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* 'H' */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* 'I' */
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* 'J' */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* 'K' */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* 'L' */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* 'M' */
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, /* 'N' */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* 'O' */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* 'P' */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* 'Q' */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* 'R' */
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* 'S' */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* 'T' */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* 'U' */
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* 'V' */
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, /* 'W' */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* 'X' */
    {0x11,0x11,0x11,0x0A,0x04,0x04,0x04}, /* 'Y' */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* 'Z' */
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, /* '[' */
    {0x00,0x10,0x08,0x04,0x02,0x01,0x00}, /* '\\' */
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, /* ']' */
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, /* '^' */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, /* '_' */
    {0x08,0x04,0x02,0x00,0x00,0x00,0x00}, /* '`' */
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, /* 'a' */
    {0x10,0x10,0x16,0x19,0x11,0x11,0x1E}, /* 'b' */
    {0x00,0x00,0x0E,0x10,0x10,0x11,0x0E}, /* 'c' */
    {0x01,0x01,0x0D,0x13,0x11,0x11,0x0F}, /* 'd' */
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, /* 'e' */
    {0x06,0x09,0x08,0x1C,0x08,0x08,0x08}, /* 'f' */
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}, /* 'g' */
    {0x10,0x10,0x16,0x19,0x11,0x11,0x11}, /* 'h' */
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, /* 'i' */
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, /* 'j' */
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, /* 'k' */
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, /* 'l' */
    {0x00,0x00,0x1A,0x15,0x15,0x15,0x15}, /* 'm' */
    {0x00,0x00,0x16,0x19,0x11,0x11,0x11}, /* 'n' */
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, /* 'o' */
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, /* 'p' */
    {0x00,0x00,0x0D,0x13,0x0F,0x01,0x01}, /* 'q' */
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, /* 'r' */
    {0x00,0x00,0x0E,0x10,0x0E,0x01,0x0E}, /* 's' */
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06}, /* 't' */
    {0x00,0x00,0x11,0x11,0x11,0x13,0x0D}, /* 'u' */
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, /* 'v' */
    {0x00,0x00,0x11,0x15,0x15,0x15,0x0A}, /* 'w' */
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, /* 'x' */
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, /* 'y' */
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, /* 'z' */
    {0x06,0x08,0x08,0x18,0x08,0x08,0x06}, /* '{' */
    {0x04,0x04,0x04,0x00,0x04,0x04,0x04}, /* '|' */
    {0x0C,0x02,0x02,0x03,0x02,0x02,0x0C}, /* '}' */
    {0x08,0x15,0x02,0x00,0x00,0x00,0x00}, /* '~' */
    {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F}, /* DEL */
};

/* ── Кириллический шрифт 5×7: 33 заглавные (А–Я), 33 строчные (а–я) ── */
/* Кодировка: U+0410–U+042F (А–Я), U+0430–U+044F (а–я) */
static const Uint8 FONT5X7_CYRILLIC[66][7] = {
    /* А (U+0410) */ {0x04,0x0A,0x11,0x1F,0x11,0x11,0x11},
    /* Б */ {0x1F,0x10,0x10,0x1E,0x11,0x11,0x1E},
    /* В */ {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    /* Г */ {0x1F,0x10,0x10,0x10,0x10,0x10,0x10},
    /* Д */ {0x0E,0x0A,0x0A,0x0A,0x0A,0x1F,0x11},
    /* Е */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    /* Ж */ {0x15,0x15,0x15,0x0E,0x15,0x15,0x15},
    /* З */ {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    /* И */ {0x11,0x11,0x13,0x15,0x19,0x11,0x11},
    /* Й */ {0x0A,0x04,0x11,0x13,0x15,0x19,0x11},
    /* К */ {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    /* Л */ {0x0F,0x09,0x09,0x09,0x09,0x11,0x11},
    /* М */ {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    /* Н */ {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* О */ {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* П */ {0x1F,0x11,0x11,0x11,0x11,0x11,0x11},
    /* Р */ {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    /* С */ {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    /* Т */ {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    /* У */ {0x11,0x11,0x11,0x0F,0x01,0x01,0x0E},
    /* Ф */ {0x04,0x0E,0x15,0x15,0x15,0x0E,0x04},
    /* Х */ {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    /* Ц */ {0x11,0x11,0x11,0x11,0x11,0x1F,0x01},
    /* Ч */ {0x11,0x11,0x11,0x0F,0x01,0x01,0x01},
    /* Ш */ {0x15,0x15,0x15,0x15,0x15,0x15,0x1F},
    /* Щ */ {0x15,0x15,0x15,0x15,0x15,0x1F,0x01},
    /* Ъ */ {0x18,0x08,0x08,0x0E,0x09,0x09,0x0E},
    /* Ы */ {0x11,0x11,0x11,0x19,0x15,0x15,0x19},
    /* Ь */ {0x10,0x10,0x10,0x1E,0x11,0x11,0x1E},
    /* Э */ {0x0E,0x11,0x01,0x0F,0x01,0x11,0x0E},
    /* Ю */ {0x12,0x15,0x15,0x1D,0x15,0x15,0x12},
    /* Я */ {0x0F,0x11,0x11,0x0F,0x05,0x09,0x11},

    /* а */ {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
    /* б */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    /* в */ {0x00,0x00,0x1E,0x11,0x1E,0x11,0x1E},
    /* г */ {0x00,0x00,0x1F,0x10,0x10,0x10,0x10},
    /* д */ {0x00,0x00,0x0E,0x0A,0x0A,0x1F,0x11},
    /* е */ {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},
    /* ж */ {0x00,0x00,0x15,0x15,0x0E,0x15,0x15},
    /* з */ {0x00,0x00,0x0E,0x01,0x06,0x01,0x0E},
    /* и */ {0x00,0x00,0x11,0x13,0x15,0x19,0x11},
    /* й */ {0x0A,0x04,0x11,0x13,0x15,0x19,0x11},
    /* к */ {0x00,0x00,0x12,0x14,0x18,0x14,0x12},
    /* л */ {0x00,0x00,0x0F,0x09,0x09,0x11,0x11},
    /* м */ {0x00,0x00,0x11,0x1B,0x15,0x11,0x11},
    /* н */ {0x00,0x00,0x11,0x11,0x1F,0x11,0x11},
    /* о */ {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},
    /* п */ {0x00,0x00,0x1F,0x11,0x11,0x11,0x11},
    /* р */ {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
    /* с */ {0x00,0x00,0x0E,0x10,0x10,0x11,0x0E},
    /* т */ {0x00,0x00,0x1F,0x04,0x04,0x04,0x04},
    /* у */ {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E},
    /* ф */ {0x04,0x04,0x0E,0x15,0x15,0x0E,0x04},
    /* х */ {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
    /* ц */ {0x00,0x00,0x11,0x11,0x11,0x1F,0x01},
    /* ч */ {0x00,0x00,0x11,0x11,0x0F,0x01,0x01},
    /* ш */ {0x00,0x00,0x15,0x15,0x15,0x15,0x1F},
    /* щ */ {0x00,0x00,0x15,0x15,0x15,0x1F,0x01},
    /* ъ */ {0x00,0x00,0x18,0x08,0x0E,0x09,0x0E},
    /* ы */ {0x00,0x00,0x11,0x11,0x19,0x15,0x19},
    /* ь */ {0x00,0x00,0x10,0x10,0x1E,0x11,0x1E},
    /* э */ {0x00,0x00,0x0E,0x01,0x0F,0x01,0x0E},
    /* ю */ {0x00,0x00,0x12,0x15,0x1D,0x15,0x12},
    /* я */ {0x00,0x00,0x0F,0x11,0x0F,0x05,0x09},
};

/* ── определение Unicode-кода для кириллицы ── */
static int cyrillic_index(unsigned char byte1, unsigned char byte2) {
    if (byte1 == 0xD0) {
        if (byte2 >= 0x90 && byte2 <= 0xAF) /* А–Я (0x410–0x42F) -> 0–31 */
            return byte2 - 0x90;
        if (byte2 >= 0xB0 && byte2 <= 0xBF) /* а–п (0x430–0x43F) -> 32–47 */
            return byte2 - 0xB0 + 32;
    }
    else if (byte1 == 0xD1) {
        if (byte2 >= 0x80 && byte2 <= 0x8F) /* р–я (0x440–0x44F) -> 48–63 */
            return byte2 - 0x80 + 48;
    }
    return -1; /* не кириллица */
}

/* ── рисование одного символа (ASCII или кириллица) ── */
static void draw_char_utf8(SDL_Renderer* r, int cx, int cy,
    const unsigned char** pt, Color col, int scale)
{
    const unsigned char* t = *pt;
    if (*t < 128) {
        /* ASCII */
        if (*t >= 32 && *t <= 127) {
            const Uint8* bmp = FONT5X7[*t - 32];
            set_color(r, col);
            for (int row = 0; row < 7; row++) {
                for (int bit = 4; bit >= 0; bit--) {
                    if (bmp[row] & (1 << bit)) {
                        SDL_FRect px = {
                            (float)(cx + (4 - bit) * scale),
                            (float)(cy + row * scale),
                            (float)scale, (float)scale
                        };
                        SDL_RenderFillRect(r, &px);
                    }
                }
            }
        }
        *pt = t + 1;
    }
    else {
        /* возможна кириллица (2 байта) */
        if ((t[0] == 0xD0 || t[0] == 0xD1) && t[1] != 0) {
            int idx = cyrillic_index(t[0], t[1]);
            if (idx >= 0 && idx < 66) {
                const Uint8* bmp = FONT5X7_CYRILLIC[idx];
                set_color(r, col);
                for (int row = 0; row < 7; row++) {
                    for (int bit = 4; bit >= 0; bit--) {
                        if (bmp[row] & (1 << bit)) {
                            SDL_FRect px = {
                                (float)(cx + (4 - bit) * scale),
                                (float)(cy + row * scale),
                                (float)scale, (float)scale
                            };
                            SDL_RenderFillRect(r, &px);
                        }
                    }
                }
            }
            *pt = t + 2;
        }
        else {
            /* неизвестный символ – пропускаем */
            *pt = t + 1;
        }
    }
}

static void draw_text(SDL_Renderer* r, int x, int y,
    const char* text, Color col, int scale)
{
    int cx = x;
    const unsigned char* p = (const unsigned char*)text;
    while (*p) {
        draw_char_utf8(r, cx, y, &p, col, scale);
        cx += 6 * scale;
    }
}

/* Подсчёт количества глифов (символов) в UTF-8 строке.
   Каждый байт вида 0xxxxxxx (ASCII) или 11xxxxxx (начало многобайтового)
   считается одним глифом; байты вида 10xxxxxx (продолжение) пропускаются. */
static int utf8_glyph_count(const char* s)
{
    int n = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++)
        if ((*p & 0xC0) != 0x80) n++;
    return n;
}

static void draw_text_centered(SDL_Renderer* r, int cx, int y,
    const char* text, Color col, int scale)
{
    int w = utf8_glyph_count(text) * 6 * scale;
    draw_text(r, cx - w / 2, y, text, col, scale);
}

static int text_width(const char* s, int scale)
{
    return utf8_glyph_count(s) * 6 * scale;
}

static void hline(SDL_Renderer* r, int x, int y, int w, Color c)
{
    set_color(r, c);
    SDL_RenderLine(r, (float)x, (float)y, (float)(x + w), (float)y);
}

/* ═══════════════════════════════════════════════════════════════════
 *  МЕНЮ
 * ═══════════════════════════════════════════════════════════════════ */

static void SDLCALL file_dialog_callback(void* userdata, const char* const* filelist, int filter)
{
    (void)filter;
    App* app = (App*)userdata;
    if (filelist && filelist[0]) {
        strncpy(app->selected_file, filelist[0], sizeof(app->selected_file) - 1);
        app->selected_file[sizeof(app->selected_file) - 1] = '\0';
        snprintf(app->status_msg, sizeof(app->status_msg), "%s: %s",
            loc(app->lang, "Selected", "\xd0\x92\xd1\x8b\xd0\xb1\xd1\x80\xd0\xb0\xd0\xbd"), app->selected_file);
    }
}

static void render_menu(App* app)
{
    SDL_Renderer* r = app->renderer;

    fill_rect(r, 0, 0, WIN_W, WIN_H, C_BG);

    fill_rect(r, 0, 0, WIN_W, 80, C_SURFACE);
    hline(r, 0, 80, WIN_W, C_DIVIDER);
    draw_text_centered(r, WIN_W / 2, 20, "COCKTAIL SHAKER SORT", C_TEXT, 2);
    draw_text_centered(r, WIN_W / 2, 45,
        loc(app->lang, "Algorithm Visualization", "\xd0\x92\xd0\xb8\xd0\xb7\xd1\x83\xd0\xb0\xd0\xbb\xd0\xb8\xd0\xb7\xd0\xb0\xd1\x86\xd0\xb8\xd1\x8f \xd0\xb0\xd0\xbb\xd0\xb3\xd0\xbe\xd1\x80\xd0\xb8\xd1\x82\xd0\xbc\xd0\xb0"), C_MUTED, 1);

    int bw = 220, bh = 50;
    int bx = WIN_W / 2 - bw / 2;
    int by_start = 180;
    int by_select = 250;
    int by_exit = 320;

    /* START */
    Color btn_col = (app->menu_button_hover == 0) ? C_BUTTON_HOVER : C_BUTTON;
    fill_rect(r, bx, by_start, bw, bh, btn_col);
    draw_rect(r, bx, by_start, bw, bh, C_DIVIDER);
    draw_text_centered(r, WIN_W / 2, by_start + bh / 2 - 8,
        loc(app->lang, "START", "\xd0\xa1\xd0\xa2\xd0\x90\xd0\xa0\xd0\xa2"), C_TEXT, 2);

    if (app->selected_file[0]) {
        draw_text_centered(r, WIN_W / 2, by_start + bh + 10,
            app->selected_file, C_MUTED, 1);
    }

    /* SELECT FILE */
    btn_col = (app->menu_button_hover == 1) ? C_BUTTON_HOVER : C_BUTTON;
    fill_rect(r, bx, by_select, bw, bh, btn_col);
    draw_rect(r, bx, by_select, bw, bh, C_DIVIDER);
    draw_text_centered(r, WIN_W / 2, by_select + bh / 2 - 8,
        loc(app->lang, "SELECT FILE", "\xd0\x92\xd0\xab\xd0\x91\xd0\x9e\xd0\xa0 \xd0\xa4\xd0\x90\xd0\x99\xd0\x9b\xd0\x90"), C_TEXT, 2);

    /* EXIT */
    btn_col = (app->menu_button_hover == 2) ? C_BUTTON_HOVER : C_BUTTON;
    fill_rect(r, bx, by_exit, bw, bh, btn_col);
    draw_rect(r, bx, by_exit, bw, bh, C_DIVIDER);
    draw_text_centered(r, WIN_W / 2, by_exit + bh / 2 - 8,
        loc(app->lang, "EXIT", "\xd0\x92\xd0\xab\xd0\xa5\xd0\x9e\xd0\x94"), C_TEXT, 2);

    /* Кнопка языка (правый нижний угол) */
    int lang_w = 70, lang_h = 30;
    int lang_x = WIN_W - lang_w - 20, lang_y = WIN_H - lang_h - 20;
    Color lang_col = app->lang_btn_hover ? C_BUTTON_HOVER : C_BUTTON;
    fill_rect(r, lang_x, lang_y, lang_w, lang_h, lang_col);
    draw_rect(r, lang_x, lang_y, lang_w, lang_h, C_DIVIDER);
    draw_text_centered(r, lang_x + lang_w / 2, lang_y + lang_h / 2 - 5,
        loc(app->lang, "EN", "RU"), C_TEXT, 1);

    if (app->status_msg[0]) {
        draw_text_centered(r, WIN_W / 2, WIN_H - 40, app->status_msg, C_MUTED, 1);
    }

    SDL_RenderPresent(r);
}

static int menu_button_at(int x, int y)
{
    int bw = 220, bh = 50;
    int bx = WIN_W / 2 - bw / 2;
    if (x >= bx && x <= bx + bw) {
        if (y >= 180 && y <= 180 + bh) return 0;
        if (y >= 250 && y <= 250 + bh) return 1;
        if (y >= 320 && y <= 320 + bh) return 2;
    }
    return -1;
}

static int lang_button_at(int x, int y) {
    int lang_w = 70, lang_h = 30;
    int lang_x = WIN_W - lang_w - 20, lang_y = WIN_H - lang_h - 20;
    if (x >= lang_x && x <= lang_x + lang_w &&
        y >= lang_y && y <= lang_y + lang_h) return 1;
    return 0;
}

static void handle_menu_event(App* app, SDL_Event* ev)
{
    switch (ev->type) {
    case SDL_EVENT_MOUSE_MOTION: {
        int mx = (int)ev->motion.x, my = (int)ev->motion.y;
        app->menu_button_hover = menu_button_at(mx, my);
        app->lang_btn_hover = lang_button_at(mx, my);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        int mx = (int)ev->button.x, my = (int)ev->button.y;
        int btn = menu_button_at(mx, my);
        if (lang_button_at(mx, my)) {
            app->lang ^= 1;
            snprintf(app->status_msg, sizeof(app->status_msg), "%s",
                loc(app->lang, "Language: English", "\xd0\xaf\xd0\xb7\xd1\x8b\xd0\xba: \xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9"));
        }
        else if (btn == 0) {
            if (load_file(app, app->selected_file)) {
                snprintf(app->status_msg, sizeof(app->status_msg),
                    loc(app->lang,
                        "Loaded %d elements. Press ENTER to sort.",
                        "\xd0\x97\xd0\xb0\xd0\xb3\xd1\x80\xd1\x83\xd0\xb6\xd0\xb5\xd0\xbd\xd0\xbe %d \xd1\x8d\xd0\xbb\xd0\xb5\xd0\xbc\xd0\xb5\xd0\xbd\xd1\x82\xd0\xbe\xd0\xb2. \xd0\x9d\xd0\xb0\xd0\xb6\xd0\xbc\xd0\xb8\xd1\x82\xd0\xb5 ENTER \xd0\xb4\xd0\xbb\xd1\x8f \xd1\x81\xd0\xbe\xd1\x80\xd1\x82\xd0\xb8\xd1\x80\xd0\xbe\xd0\xb2\xd0\xba\xd0\xb8."),
                    app->n);
                app->state = STATE_IDLE;
            }
            else {
                snprintf(app->status_msg, sizeof(app->status_msg), "%s",
                    loc(app->lang, "Error: cannot load %s", "\xd0\x9e\xd1\x88\xd0\xb8\xd0\xb1\xd0\xba\xd0\xb0: \xd0\xbd\xd0\xb5 \xd0\xbc\xd0\xbe\xd0\xb3\xd1\x83 \xd0\xb7\xd0\xb0\xd0\xb3\xd1\x80\xd1\x83\xd0\xb7\xd0\xb8\xd1\x82\xd1\x8c %s"),
                    app->selected_file);
            }
        }
        else if (btn == 1) {
            SDL_ShowOpenFileDialog(file_dialog_callback, app, app->window,
                NULL, 0, NULL, 0);
        }
        else if (btn == 2) {
            SDL_Event quit_ev;
            quit_ev.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit_ev);
        }
        break;
    }
    case SDL_EVENT_KEY_DOWN: {
        SDL_Keycode k = ev->key.key;
        if (k == SDLK_RETURN) {
            if (load_file(app, app->selected_file)) {
                snprintf(app->status_msg, sizeof(app->status_msg),
                    loc(app->lang,
                        "Loaded %d elements. Press ENTER to sort.",
                        "\xd0\x97\xd0\xb0\xd0\xb3\xd1\x80\xd1\x83\xd0\xb6\xd0\xb5\xd0\xbd\xd0\xbe %d \xd1\x8d\xd0\xbb\xd0\xb5\xd0\xbc\xd0\xb5\xd0\xbd\xd1\x82\xd0\xbe\xd0\xb2. \xd0\x9d\xd0\xb0\xd0\xb6\xd0\xbc\xd0\xb8\xd1\x82\xd0\xb5 ENTER \xd0\xb4\xd0\xbb\xd1\x8f \xd1\x81\xd0\xbe\xd1\x80\xd1\x82\xd0\xb8\xd1\x80\xd0\xbe\xd0\xb2\xd0\xba\xd0\xb8."),
                    app->n);
                app->state = STATE_IDLE;
            }
            else {
                snprintf(app->status_msg, sizeof(app->status_msg), "%s",
                    loc(app->lang, "Error: cannot load %s", "\xd0\x9e\xd1\x88\xd0\xb8\xd0\xb1\xd0\xba\xd0\xb0: \xd0\xbd\xd0\xb5 \xd0\xbc\xd0\xbe\xd0\xb3\xd1\x83 \xd0\xb7\xd0\xb0\xd0\xb3\xd1\x80\xd1\x83\xd0\xb7\xd0\xb8\xd1\x82\xd1\x8c %s"),
                    app->selected_file);
            }
        }
        else if (k == SDLK_F) {
            SDL_ShowOpenFileDialog(file_dialog_callback, app, app->window,
                NULL, 0, NULL, 0);
        }
        else if (k == SDLK_ESCAPE) {
            SDL_Event quit_ev;
            quit_ev.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit_ev);
        }
        else if (k == SDLK_L) {
            app->lang ^= 1;
            snprintf(app->status_msg, sizeof(app->status_msg), "%s",
                loc(app->lang, "Language: English", "\xd0\xaf\xd0\xb7\xd1\x8b\xd0\xba: \xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9"));
        }
        break;
    }
    default: break;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  ОТРИСОВКА ОСНОВНОГО ЭКРАНА (сортировка)
 * ═══════════════════════════════════════════════════════════════════ */

static void render_main(App* app)
{
    SDL_Renderer* r = app->renderer;

    fill_rect(r, 0, 0, WIN_W, WIN_H, C_BG);

    fill_rect(r, 0, 0, WIN_W, 82, C_SURFACE);
    hline(r, 0, 82, WIN_W, C_DIVIDER);
    draw_text(r, 14, 10, "COCKTAIL SHAKER SORT", C_TEXT, 2);
    draw_text(r, 14, 32,
        loc(app->lang, "(Shakernaya sortirovka)", "(\xd0\xa8\xd0\xb5\xd0\xb9\xd0\xba\xd0\xb5\xd1\x80\xd0\xbd\xd0\xb0\xd1\x8f \xd1\x81\xd0\xbe\xd1\x80\xd1\x82\xd0\xb8\xd1\x80\xd0\xbe\xd0\xb2\xd0\xba\xd0\xb0)"), C_MUTED, 1);

    {
        char sp[64];
        snprintf(sp, sizeof(sp), "%s: %d/10",
            loc(app->lang, "Speed", "\xd0\xa1\xd0\xba\xd0\xbe\xd1\x80\xd0\xbe\xd1\x81\xd1\x82\xd1\x8c"), app->speed);
        draw_text(r, WIN_W - 130, 15, sp, C_MUTED, 1);
        draw_text(r, WIN_W - 130, 28,
            loc(app->lang, "< >: change", "< >: \xd0\xb8\xd0\xb7\xd0\xbc\xd0\xb5\xd0\xbd\xd0\xb8\xd1\x82\xd1\x8c"), C_MUTED, 1);
    }

    draw_text(r, 14, 54,
        loc(app->lang,
            "ENTER:sort  SPACE:pause  S:save  ESC:menu",
            "ENTER:\xd1\x81\xd0\xbe\xd1\x80\xd1\x82  SPACE:\xd0\xbf\xd0\xb0\xd1\x83\xd0\xb7\xd0\xb0  S:\xd1\x81\xd0\xbe\xd1\x85\xd1\x80  ESC:\xd0\xbc\xd0\xb5\xd0\xbd\xd1\x8e"),
        C_MUTED, 1);

    {
        const char* state_str;
        Color sc = C_MUTED;
        if (app->state == STATE_SORTING) {
            state_str = loc(app->lang, "SORTING", "\xd0\xa1\xd0\x9e\xd0\xa0\xd0\xa2\xd0\x98\xd0\xa0\xd0\x9e\xd0\x92\xd0\x9a\xd0\x90");
            sc = C_BAR_CMP;
        }
        else if (app->state == STATE_PAUSED) {
            state_str = loc(app->lang, "PAUSED", "\xd0\x9f\xd0\x90\xd0\xa3\xd0\x97\xd0\x90");
            sc = C_BAR_RIGHT;
        }
        else if (app->state == STATE_DONE) {
            state_str = loc(app->lang, "DONE", "\xd0\x93\xd0\x9e\xd0\xa2\xd0\x9e\xd0\x92\xd0\x9e");
            sc = C_BAR_DONE;
        }
        else {
            state_str = loc(app->lang, "IDLE", "\xd0\x9e\xd0\x96\xd0\x98\xd0\x94\xd0\x90\xd0\x9d\xd0\x98\xd0\x95");
        }
        int sw = text_width(state_str, 2);
        draw_text(r, WIN_W - sw - 14, 54, state_str, sc, 2);
    }

    fill_rect(r, BAR_AREA_X - 4, BAR_AREA_Y - 4,
        BAR_AREA_W + 8, BAR_AREA_H + 8, C_SURFACE);
    draw_rect(r, BAR_AREA_X - 4, BAR_AREA_Y - 4,
        BAR_AREA_W + 8, BAR_AREA_H + 8, C_DIVIDER);

    Step* cur = NULL;
    if (app->step_count > 0) {
        int idx = app->step_idx;
        if (idx >= app->step_count) idx = app->step_count - 1;
        cur = &app->steps[idx];
    }

    if (cur && app->n > 0) {
        int n = cur->n;
        int mn = cur->arr[0], mx = cur->arr[0];
        for (int i = 1; i < n; i++) {
            if (cur->arr[i] < mn) mn = cur->arr[i];
            if (cur->arr[i] > mx) mx = cur->arr[i];
        }
        int range = mx - mn; if (range == 0) range = 1;

        int bw = (BAR_AREA_W - (n - 1) * 2) / n;
        if (bw < 2) bw = 2;
        int gap = (n > 1) ? (BAR_AREA_W - bw * n) / (n - 1) : 0;

        for (int i = 0; i < n; i++) {
            int bh = (int)((double)(cur->arr[i] - mn) / range
                * (BAR_AREA_H - 20)) + 4;
            int bx = BAR_AREA_X + i * (bw + gap);
            int by = BAR_AREA_Y + BAR_AREA_H - bh;

            Color bc = C_BAR_NORMAL;
            if (cur->kind == STEP_DONE) {
                bc = C_BAR_DONE;
            }
            else if (app->state != STATE_IDLE) {
                if (i < cur->left || i > cur->right) bc = C_BAR_DONE;
                if ((cur->kind == STEP_COMPARE || cur->kind == STEP_SWAP) &&
                    (i == cur->i || i == cur->j))
                    bc = (cur->kind == STEP_SWAP) ? C_BAR_SWAP : C_BAR_CMP;
                if (i == cur->left && cur->kind == STEP_PASS_END) bc = C_BAR_LEFT;
                if (i == cur->right && cur->kind == STEP_PASS_END) bc = C_BAR_RIGHT;
            }

            fill_rect(r, bx, by, bw, bh, bc);

            if (bw >= 10 && n <= 60) {
                char buf[16]; snprintf(buf, sizeof(buf), "%d", cur->arr[i]);
                int tw = text_width(buf, 1);
                draw_text(r, bx + bw / 2 - tw / 2, by - 11, buf, C_MUTED, 1);
            }
        }

        if (cur->kind != STEP_DONE && app->state != STATE_IDLE) {
            int lx = BAR_AREA_X + cur->left * (bw + gap) + bw / 2;
            int rx = BAR_AREA_X + cur->right * (bw + gap) + bw / 2;
            int my = BAR_AREA_Y + BAR_AREA_H + 4;
            set_color(r, C_BAR_LEFT);
            SDL_RenderLine(r, (float)lx, (float)my, (float)lx, (float)(my + 6));
            draw_text(r, lx - 4, my + 8, "L", C_BAR_LEFT, 1);
            set_color(r, C_BAR_RIGHT);
            SDL_RenderLine(r, (float)rx, (float)my, (float)rx, (float)(my + 6));
            draw_text(r, rx - 4, my + 8, "R", C_BAR_RIGHT, 1);
        }
    }
    else if (app->state == STATE_IDLE && app->n > 0) {
        int n = app->n;
        int mn = app->original[0], mx = app->original[0];
        for (int i = 1; i < n; i++) {
            if (app->original[i] < mn) mn = app->original[i];
            if (app->original[i] > mx) mx = app->original[i];
        }
        int range = mx - mn; if (range == 0) range = 1;
        int bw = (BAR_AREA_W - (n - 1) * 2) / n;
        if (bw < 2) bw = 2;
        int gap = (n > 1) ? (BAR_AREA_W - bw * n) / (n - 1) : 0;
        for (int i = 0; i < n; i++) {
            int bh = (int)((double)(app->original[i] - mn) / range
                * (BAR_AREA_H - 20)) + 4;
            int bx = BAR_AREA_X + i * (bw + gap);
            int by = BAR_AREA_Y + BAR_AREA_H - bh;
            fill_rect(r, bx, by, bw, bh, C_BAR_NORMAL);
            if (bw >= 10 && n <= 60) {
                char buf[16]; snprintf(buf, sizeof(buf), "%d", app->original[i]);
                int tw = text_width(buf, 1);
                draw_text(r, bx + bw / 2 - tw / 2, by - 11, buf, C_MUTED, 1);
            }
        }
    }

    fill_rect(r, 0, PANEL_Y, WIN_W, WIN_H - PANEL_Y, C_PANEL);
    hline(r, 0, PANEL_Y, WIN_W, C_DIVIDER);

    {
        Step* s = cur;
        int cmp_v = s ? s->cmp_total : 0;
        int swap_v = s ? s->swap_total : 0;
        int pass_v = s ? (s->pass > 0 ? s->pass : 1) : 0;
        if (app->state == STATE_DONE) {
            cmp_v = app->total_cmp;
            swap_v = app->total_swaps;
            pass_v = app->total_passes;
        }

        struct { const char* lbl; int val; } stats[] = {
            {"N",    app->n},
            {"CMP",  cmp_v},
            {"SWAP", swap_v},
            {"PASS", pass_v},
        };
        int sx = 14;
        for (int i = 0; i < 4; i++) {
            fill_rect(r, sx, PANEL_Y + 8, 90, 54, C_SURFACE);
            draw_rect(r, sx, PANEL_Y + 8, 90, 54, C_DIVIDER);
            char buf[16]; snprintf(buf, sizeof(buf), "%d", stats[i].val);
            int nw = text_width(buf, 2);
            draw_text(r, sx + 45 - nw / 2, PANEL_Y + 14, buf, C_ACCENT, 2);
            int lw = text_width(stats[i].lbl, 1);
            draw_text(r, sx + 45 - lw / 2, PANEL_Y + 40, stats[i].lbl, C_MUTED, 1);
            sx += 98;
        }

        if (app->step_count > 0) {
            int total_w = 400;
            int prog_x = 420;
            int prog_y = PANEL_Y + 12;
            int prog = (int)((app->step_idx * (Uint64)total_w) / app->step_count);
            fill_rect(r, prog_x, prog_y, total_w, 10, C_SURFACE);
            draw_rect(r, prog_x, prog_y, total_w, 10, C_DIVIDER);
            if (prog > 0) fill_rect(r, prog_x, prog_y, prog, 10, C_ACCENT);

            char pct[64];
            snprintf(pct, sizeof(pct), "%s %d / %d",
                loc(app->lang, "Step", "\xd0\xa8\xd0\xb0\xd0\xb3"), app->step_idx, app->step_count);
            draw_text(r, prog_x, prog_y + 16, pct, C_MUTED, 1);

            if (cur && cur->kind != STEP_DONE) {
                const char* dir_str;
                Color dc;
                if (cur->dir == 1) {
                    dir_str = loc(app->lang, "Pass ^ UP", "\xd0\x9f\xd1\x80\xd0\xbe\xd1\x85\xd0\xbe\xd0\xb4 ^ \xd0\x92\xd0\x92\xd0\x95\xd0\xa0\xd0\xa5");
                    dc = C_BAR_LEFT;
                }
                else {
                    dir_str = loc(app->lang, "Pass v DOWN", "\xd0\x9f\xd1\x80\xd0\xbe\xd1\x85\xd0\xbe\xd0\xb4 v \xd0\x92\xd0\x9d\xd0\x98\xd0\x97");
                    dc = C_BAR_RIGHT;
                }
                draw_text(r, prog_x, prog_y + 30, dir_str, dc, 1);
            }
        }
    }

    {
        int ty = PANEL_Y + 74;
        hline(r, 14, ty - 4, WIN_W - 28, C_DIVIDER);

        int show = app->trace_count > 4 ? 4 : app->trace_count;
        int start = app->trace_count - show;
        for (int i = 0; i < show; i++) {
            Color tc = (i == show - 1) ? C_TEXT : C_MUTED;
            draw_text(r, 14, ty + i * 14, app->trace[start + i], tc, 1);
        }

        if (app->status_msg[0]) {
            Color sc = app->result_saved ? C_BAR_DONE : C_BAR_CMP;
            draw_text(r, 14, WIN_H - 16, app->status_msg, sc, 1);
        }
    }

    {
        struct { Color c; const char* lbl; } leg[] = {
            {C_BAR_NORMAL, loc(app->lang, "Normal", "\xd0\x9e\xd0\xb1\xd1\x8b\xd1\x87\xd0\xbd\xd1\x8b\xd0\xb9")},
            {C_BAR_CMP,    loc(app->lang, "Compare", "\xd0\xa1\xd1\x80\xd0\xb0\xd0\xb2\xd0\xbd\xd0\xb5\xd0\xbd\xd0\xb8\xd0\xb5")},
            {C_BAR_SWAP,   loc(app->lang, "Swap", "\xd0\x9e\xd0\xb1\xd0\xbc\xd0\xb5\xd0\xbd")},
            {C_BAR_LEFT,   loc(app->lang, "Left bound", "\xd0\x9b\xd0\xb5\xd0\xb2. \xd0\xb3\xd1\x80\xd0\xb0\xd0\xbd\xd0\xb8\xd1\x86\xd0\xb0")},
            {C_BAR_RIGHT,  loc(app->lang, "Right bound", "\xd0\x9f\xd1\x80\xd0\xb0\xd0\xb2. \xd0\xb3\xd1\x80\xd0\xb0\xd0\xbd\xd0\xb8\xd1\x86\xd0\xb0")},
            {C_BAR_DONE,   loc(app->lang, "Sorted", "\xd0\x9e\xd1\x82\xd1\x81\xd0\xbe\xd1\x80\xd1\x82\xd0\xb8\xd1\x80\xd0\xbe\xd0\xb2\xd0\xb0\xd0\xbd\xd0\xbe")},
        };
        int lx = WIN_W - 160;
        int ly = PANEL_Y + 10;
        for (int i = 0; i < 6; i++) {
            fill_rect(r, lx, ly + i * 16, 10, 8, leg[i].c);
            draw_text(r, lx + 14, ly + i * 16, leg[i].lbl, C_MUTED, 1);
        }
    }

    SDL_RenderPresent(r);
}

/* ═══════════════════════════════════════════════════════════════════
 *  ГЛАВНЫЙ ЦИКЛ
 * ═══════════════════════════════════════════════════════════════════ */

static void start_sort(App* app)
{
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
            "\xd0\xa1\xd0\xbe\xd1\x80\xd1\x82\xd0\xb8\xd1\x80\xd0\xbe\xd0\xb2\xd0\xba\xd0\xb0 %d \xd1\x8d\xd0\xbb\xd0\xb5\xd0\xbc\xd0\xb5\xd0\xbd\xd1\x82\xd0\xbe\xd0\xb2..."),
        app->n);
}

int main(int argc, char* argv[])
{
    (void)argc; (void)argv;
    srand((unsigned)time(NULL));

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    App* app = (App*)calloc(1, sizeof(App));
    if (!app) {
        fprintf(stderr, "Out of memory\n");
        SDL_Quit();
        return 1;
    }
    app->speed = 5;
    app->lang = 0;
    app->lang_btn_hover = 0;
    strncpy(app->selected_file, INPUT_FILE, sizeof(app->selected_file) - 1);
    app->selected_file[sizeof(app->selected_file) - 1] = '\0';
    snprintf(app->status_msg, sizeof(app->status_msg), "%s: %s",
        loc(app->lang, "Default file", "\xd0\xa4\xd0\xb0\xd0\xb9\xd0\xbb \xd0\xbf\xd0\xbe \xd1\x83\xd0\xbc\xd0\xbe\xd0\xbb\xd1\x87\xd0\xb0\xd0\xbd\xd0\xb8\xd1\x8e"), INPUT_FILE);

    app->window = SDL_CreateWindow(
        "Cocktail Shaker Sort \xe2\x80\x94 SDL3",
        WIN_W, WIN_H, SDL_WINDOW_HIDDEN);
    if (!app->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        free(app);
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowPosition(app->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(app->window);

    app->renderer = SDL_CreateRenderer(app->window, NULL);
    if (!app->renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(app->window);
        free(app);
        SDL_Quit();
        return 1;
    }

    app->state = STATE_MENU;
    app->menu_button_hover = -1;
    Uint64 frame_ms = 1000 / FPS;

    int running = 1;
    while (running) {
        Uint64 frame_start = SDL_GetTicks();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) { running = 0; break; }

            if (app->state == STATE_MENU) {
                handle_menu_event(app, &ev);
            }
            else {
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    SDL_Keycode k = ev.key.key;
                    if (k == SDLK_ESCAPE) {
                        app->state = STATE_MENU;
                        app->menu_button_hover = -1;
                        app->step_count = 0;
                        app->step_idx = 0;
                        app->trace_count = 0;
                        app->result_saved = 0;
                        snprintf(app->status_msg, sizeof(app->status_msg), "%s",
                            loc(app->lang, "Returned to menu.", "\xd0\x92\xd0\xbe\xd0\xb7\xd0\xb2\xd1\x80\xd0\xb0\xd1\x82 \xd0\xb2 \xd0\xbc\xd0\xb5\xd0\xbd\xd1\x8e."));
                    }
                    else if (k == SDLK_RETURN && app->state == STATE_IDLE) {
                        start_sort(app);
                    }
                    else if (k == SDLK_SPACE) {
                        if (app->state == STATE_SORTING) {
                            app->state = STATE_PAUSED;
                            snprintf(app->status_msg, sizeof(app->status_msg), "%s",
                                loc(app->lang, "PAUSED. SPACE to resume.", "\xd0\x9f\xd0\x90\xd0\xa3\xd0\x97\xd0\x90. SPACE \xd0\xb4\xd0\xbb\xd1\x8f \xd0\xbf\xd1\x80\xd0\xbe\xd0\xb4\xd0\xbe\xd0\xbb\xd0\xb6\xd0\xb5\xd0\xbd\xd0\xb8\xd1\x8f."));
                        }
                        else if (app->state == STATE_PAUSED) {
                            app->state = STATE_SORTING;
                            app->last_tick = SDL_GetTicks();
                            snprintf(app->status_msg, sizeof(app->status_msg), "%s",
                                loc(app->lang, "Sorting...", "\xd0\xa1\xd0\xbe\xd1\x80\xd1\x82\xd0\xb8\xd1\x80\xd0\xbe\xd0\xb2\xd0\xba\xd0\xb0..."));
                        }
                    }
                    else if (k == SDLK_S && app->state == STATE_DONE) {
                        save_result(app);
                    }
                    else if (k == SDLK_RIGHT) {
                        if (app->speed < 10) app->speed++;
                        snprintf(app->status_msg, sizeof(app->status_msg),
                            "%s: %d/10", loc(app->lang, "Speed", "\xd0\xa1\xd0\xba\xd0\xbe\xd1\x80\xd0\xbe\xd1\x81\xd1\x82\xd1\x8c"), app->speed);
                    }
                    else if (k == SDLK_LEFT) {
                        if (app->speed > 1) app->speed--;
                        snprintf(app->status_msg, sizeof(app->status_msg),
                            "%s: %d/10", loc(app->lang, "Speed", "\xd0\xa1\xd0\xba\xd0\xbe\xd1\x80\xd0\xbe\xd1\x81\xd1\x82\xd1\x8c"), app->speed);
                    }
                }
            }
        }

        if (app->state == STATE_MENU) {
            render_menu(app);
        }
        else {
            if (app->state == STATE_SORTING) {
                Uint64 now = SDL_GetTicks();
                Uint64 delay = (Uint64)(1000 / app->speed);
                while (now - app->last_tick >= delay) {
                    if (app->step_idx < app->step_count) {
                        Step* s = &app->steps[app->step_idx];
                        if (app->speed >= 8 && s->kind == STEP_COMPARE) {
                            app->step_idx++;
                            continue;
                        }
                        app->step_idx++;
                        if (s->kind == STEP_DONE) {
                            app->state = STATE_DONE;
                            snprintf(app->status_msg, sizeof(app->status_msg),
                                loc(app->lang,
                                    "DONE! %d elem, %d cmp, %d swaps, %.3f ms. S=save",
                                    "\xd0\x93\xd0\x9e\xd0\xa2\xd0\x9e\xd0\x92\xd0\x9e! %d \xd1\x8d\xd0\xbb, %d \xd1\x81\xd1\x80\xd0\xb0\xd0\xb2\xd0\xbd, %d \xd0\xbe\xd0\xb1\xd0\xbc\xd0\xb5\xd0\xbd\xd0\xbe\xd0\xb2, %.3f \xd0\xbc\xd1\x81. S=\xd1\x81\xd0\xbe\xd1\x85\xd1\x80"),
                                app->n, app->total_cmp, app->total_swaps, app->elapsed_ms);
                            break;
                        }
                    }
                    else {
                        app->state = STATE_DONE;
                        break;
                    }
                    app->last_tick += delay;
                }
            }
            render_main(app);
        }

        Uint64 elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < frame_ms)
            SDL_Delay((Uint32)(frame_ms - elapsed));
    }

    SDL_Renderer* rend = app->renderer;
    SDL_Window* win = app->window;
    free(app->steps);
    free(app);
    SDL_DestroyRenderer(rend);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}