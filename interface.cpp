#ifndef SORT_VISUALIZER_H
#define SORT_VISUALIZER_H

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

/* ──────────────────── Прототипы функций ─────────────────────────── */

/* Утилиты рендеринга */
void set_color(SDL_Renderer* r, Color c);
void fill_rect(SDL_Renderer* r, int x, int y, int w, int h, Color c);
void draw_rect(SDL_Renderer* r, int x, int y, int w, int h, Color c);
void draw_text(SDL_Renderer* r, int x, int y, const char* text, Color col, int scale);
void draw_text_centered(SDL_Renderer* r, int cx, int y, const char* text, Color col, int scale);
int text_width(const char* s, int scale);
void hline(SDL_Renderer* r, int x, int y, int w, Color c);

/* Локализация */
const char* loc(int lang, const char* en, const char* ru);

/* Управление шагами */
void push_step(App* app, Step* s);
void collect_steps(App* app);

/* Работа с файлами */
int load_file(App* app, const char* path);
void save_result(App* app);
void randomize(App* app);

/* Отрисовка */
void render_menu(App* app);
void render_main(App* app);

/* Обработка событий */
void handle_menu_event(App* app, SDL_Event* ev);
void start_sort(App* app);

/* Вспомогательные функции */
int cyrillic_index(unsigned char byte1, unsigned char byte2);
void draw_char_utf8(SDL_Renderer* r, int cx, int cy, const unsigned char** pt, Color col, int scale);
int utf8_glyph_count(const char* s);
int menu_button_at(int x, int y);
int lang_button_at(int x, int y);

#endif /* SORT_VISUALIZER_H */