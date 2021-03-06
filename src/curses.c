
#include "curses.h"
#include "feeder.h"
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <ncurses.h>

#define CURSES_TEXT_LENGTH 512

/* Global ncurses variables. */
static uint16_t _curses_term_width;
static uint16_t _curses_term_height;
static bool     _curses_term_resized;
static bool     _curses_colors;
static bool     _curses_enabled;

/* The list. */
/* The number of elements in the last update. */
static size_t            _curses_list_nb;
static feeder_iterator_t _curses_list_first;
static feeder_iterator_t _curses_list_sel;
static size_t            _curses_list_offset;
static bool              _curses_list_pager;
static bool              _curses_list_mustdraw;

/* Top and bottom bars. */
static bool  _curses_top_enable;
static char* _curses_top_str;
static bool  _curses_top_mustdraw;

static bool  _curses_bot_enable;
static char* _curses_bot_str;
static bool  _curses_bot_mustdraw;

/* Command line. */
static bool        _curses_cmd_in;
static const char* _curses_cmd_prefix;
static char        _curses_cmd_text[CURSES_TEXT_LENGTH];
static size_t      _curses_cmd_pos;
static bool        _curses_cmd_mustdraw;

/* The colors pairs. */
enum {
    COLOR_TOP = 1,
    COLOR_BOT = 2,
    COLOR_CMD = 3,
    COLOR_SEL = 4,
    COLOR_LST = 5
};

/********************* Generic Ncurses abilities *****************************/
/* Handler for terminal resize. */
static void _curses_term_resize(int sig)
{
    if(sig) { } /* avoid warnings. */
    _curses_term_resized = true;
}

static bool _curses_init_ncurses()
{
    if(!initscr())
        return false;
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    _curses_term_width   = COLS;
    _curses_term_height  = LINES;
    _curses_term_resized = false;
    _curses_enabled      = true;
    return true;
}

bool curses_init()
{
    /* Initialising ncurses. */
    if(!_curses_init_ncurses())
        return false;
    signal(SIGWINCH, _curses_term_resize);

    /* Initialising colors. */
    _curses_colors = has_colors();
    if(_curses_colors)
        start_color();

    /* Initialising the list. */
    _curses_list_nb       = 0;
    _curses_list_offset   = 0;
    _curses_list_pager    = false;
    _curses_list_first    = feeder_begin();
    _curses_list_sel      = feeder_begin();

    /* Initialising the top and bottom bars. */
    _curses_top_enable = false;
    _curses_top_str    = NULL;
    _curses_bot_enable = false;
    _curses_bot_str    = NULL;

    /* Initialising color pairs. */
    init_pair(COLOR_TOP, COLOR_BLACK, COLOR_WHITE);
    init_pair(COLOR_BOT, COLOR_BLACK, COLOR_WHITE);
    init_pair(COLOR_CMD, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_SEL, COLOR_BLACK, COLOR_WHITE);
    init_pair(COLOR_LST, COLOR_WHITE, COLOR_BLACK);

    /* Initialising the command line. */
    _curses_cmd_in     = false;
    _curses_cmd_prefix = "";

    /* Preparing drawing. */
    _curses_list_mustdraw = true;
    _curses_top_mustdraw  = true;
    _curses_bot_mustdraw  = true;
    _curses_cmd_mustdraw  = true;

    return true;
}

bool curses_end()
{
    endwin();
    if(_curses_top_str)
        free(_curses_top_str);
    if(_curses_bot_str)
        free(_curses_bot_str);
    return true;
}

void curses_enable()
{
    if(_curses_enabled)
        return;
    reset_prog_mode();
    refresh();
    _curses_enabled = true;
}

void curses_disable()
{
    if(!_curses_enabled)
        return;
    def_prog_mode();
    endwin();
    _curses_enabled = false;
}

void curses_redraw()
{
    _curses_top_mustdraw  = true;
    _curses_bot_mustdraw  = true;
    _curses_cmd_mustdraw  = true;
    _curses_list_mustdraw = true;
}

static void _curses_draw_line(const char* text, unsigned int y, int cp)
{
    size_t i;
    char* txt;

    /* First fill the the bg with spaces. */
    attron(COLOR_PAIR(cp));
    for(i = 0; i < _curses_term_width; ++i)
        mvaddch(y, i, ' ');

    /* Print the text. */
    if(strlen(text) == 0)
        return;
    txt = strdup(text);
    if(strlen(txt) > _curses_term_width)
        txt[_curses_term_width] = '\0';
    mvaddstr(y, 0, txt);
    free(txt);
}

static unsigned int _curses_list_height()
{
    return _curses_term_height - 1
        - (_curses_top_enable ? 1 : 0)
        - (_curses_bot_enable ? 1 : 0);
}

static bool _curses_list_isin(feeder_iterator_t it)
{
    return (it.vid >= _curses_list_first.vid
            && it.vid < _curses_list_first.vid + _curses_list_height());
}

static void _curses_list_draw_line(feeder_iterator_t it)
{
    int cp;
    unsigned int y;
    const char* txt;

    if(!_curses_list_isin(it))
        return;

    if(feeder_it_cmp(it, _curses_list_sel) == 0)
        cp = COLOR_SEL;
    else
        cp = COLOR_LST;

    y = it.vid - _curses_list_first.vid + (_curses_top_enable ? 1 : 0);
    txt = feeder_get_it_text(it);
    if(!it.valid || it.vid >= _curses_list_nb
            || strlen(txt) <= _curses_list_offset)
        _curses_draw_line("", y, cp);
    else
        _curses_draw_line(txt + _curses_list_offset, y, cp);
}

static void _curses_list_draw()
{
    size_t i;
    unsigned int lines;
    feeder_iterator_t it;

    lines = _curses_list_height();
    it = _curses_list_first;
    for(i = 0; i < lines; ++i) {
        if(it.valid) {
            _curses_list_draw_line(it);
            feeder_next(&it, 1);
        }
        else
            _curses_draw_line("", i + (_curses_top_enable ? 1 : 0), COLOR_LST);
    }
}

static void _curses_cmd_draw()
{
    char buffer[CURSES_TEXT_LENGTH];
    if(!_curses_cmd_in)
        buffer[0] = '\0';
    else {
        snprintf(buffer, CURSES_TEXT_LENGTH, "%s%s",
                _curses_cmd_prefix, _curses_cmd_text);
    }
    _curses_draw_line(buffer, _curses_term_height - 1, COLOR_CMD);
}

static bool _curses_term_apply_resize()
{
    struct winsize winsz;
    ioctl(0, TIOCGWINSZ, &winsz);

    _curses_term_width  = winsz.ws_col;
    _curses_term_height = winsz.ws_row;
    resizeterm(_curses_term_height, _curses_term_width);

    endwin();
    return _curses_init_ncurses();
}

void curses_draw()
{
    if(_curses_term_resized) {
        _curses_term_apply_resize();
        _curses_list_mustdraw = true;
        _curses_top_mustdraw  = true;
        _curses_bot_mustdraw  = true;
        _curses_cmd_mustdraw  = true;
    }

    if(_curses_top_mustdraw) {
        if(_curses_top_enable)
            _curses_draw_line(_curses_top_str, 0, COLOR_TOP);
        else
            _curses_list_mustdraw = true;
        _curses_top_mustdraw = false;
    }

    if(_curses_bot_mustdraw) {
        if(_curses_bot_enable)
            _curses_draw_line(_curses_bot_str,
                    _curses_term_height - 2, COLOR_BOT);
        else
            _curses_list_mustdraw = true;
        _curses_bot_mustdraw = false;
    }

    if(_curses_list_mustdraw) {
        _curses_list_draw();
        _curses_list_mustdraw = false;
    }

    if(_curses_cmd_mustdraw) {
        _curses_cmd_draw();
        _curses_cmd_mustdraw = false;
    }

    /* Placing the cursor. */
    if(!_curses_cmd_in)
        move(_curses_term_height - 1, 0);
    else {
        move(_curses_term_height - 1,
                strlen(_curses_cmd_prefix) + _curses_cmd_pos);
    }

    refresh();
}

int curses_str_to_color(const char* str)
{
    if(strcmp(str, "black") == 0)
        return COLOR_BLACK;
    else if(strcmp(str, "red") == 0)
        return COLOR_RED;
    else if(strcmp(str, "green") == 0)
        return COLOR_GREEN;
    else if(strcmp(str, "yellow") == 0)
        return COLOR_YELLOW;
    else if(strcmp(str, "blue") == 0)
        return COLOR_BLUE;
    else if(strcmp(str, "magenta") == 0)
        return COLOR_MAGENTA;
    else if(strcmp(str, "cyan") == 0)
        return COLOR_CYAN;
    else if(strcmp(str, "white") == 0)
        return COLOR_WHITE;
    else
        return COLOR_BLACK;
}

/********************* List handling abilities *******************************/
void curses_list_colors(int fg, int bg)
{
    init_pair(COLOR_LST, fg, bg);
}

void curses_list_colors_sel(int fg, int bg)
{
    init_pair(COLOR_SEL, fg, bg);
}

void curses_list_changed(bool force)
{
    size_t nb;
    feeder_iterator_t it;

    it = feeder_end();
    nb = it.vid;
    if((nb != _curses_list_nb && nb < _curses_list_height())
            || force)
        _curses_list_mustdraw = true;
    if((_curses_list_nb == 0 && !_curses_list_first.valid)
            || nb < _curses_list_nb) {
        _curses_list_first = feeder_begin();
        _curses_list_sel   = _curses_list_first;
    }

    _curses_list_nb = nb;
}

bool curses_list_down(size_t nb)
{
    feeder_iterator_t savesel = _curses_list_sel;
    bool ret = true;
    feeder_next(&_curses_list_sel, nb);
    if(!_curses_list_sel.valid) {
        _curses_list_sel = feeder_begin();
        if(_curses_list_nb > 0)
            feeder_next(&_curses_list_sel, _curses_list_nb - 1);
        ret = false;
    }

    if(_curses_list_isin(_curses_list_sel)) {
        _curses_list_draw_line(savesel);
        _curses_list_draw_line(_curses_list_sel);
    } else {
        if(_curses_list_height() > _curses_list_sel.vid)
            _curses_list_first = feeder_begin();
        else if(_curses_list_pager)
            _curses_list_first = _curses_list_sel;
        else {
            _curses_list_first = feeder_begin();
            feeder_next(&_curses_list_first,
                    _curses_list_sel.vid - _curses_list_height() + 1);
        }
        _curses_list_mustdraw = true;
    }

    return ret;
}

bool curses_list_up(size_t nb)
{
    feeder_iterator_t savesel = _curses_list_sel;
    bool ret = true;
    feeder_prev(&_curses_list_sel, nb);
    if(!_curses_list_sel.valid) {
        _curses_list_sel = feeder_begin();
        ret = false;
    }

    if(_curses_list_isin(_curses_list_sel)) {
        _curses_list_draw_line(savesel);
        _curses_list_draw_line(_curses_list_sel);
    } else {
        if(_curses_list_pager) {
            _curses_list_first = feeder_begin();
            if(_curses_list_height() <= _curses_list_sel.vid) {
                feeder_next(&_curses_list_first,
                        _curses_list_sel.vid - _curses_list_height() + 1);
            }
        } else
            _curses_list_first = _curses_list_sel;
        _curses_list_mustdraw = true;
    }
    return ret;
}

size_t curses_list_get()
{
    return _curses_list_sel.vid;
}

bool curses_list_set(size_t nb)
{
    size_t height;
    feeder_iterator_t savesel = _curses_list_sel;

    _curses_list_sel = feeder_begin();
    feeder_next(&_curses_list_sel, nb);
    if(!_curses_list_sel.valid) {
        _curses_list_sel = savesel;
        return false;
    }
    
    if(_curses_list_isin(_curses_list_sel)) {
        _curses_list_draw_line(savesel);
        _curses_list_draw_line(_curses_list_sel);
    } else {
        height = _curses_list_height();
        if(_curses_list_sel.vid < height / 2)
            _curses_list_first = feeder_begin();
        else if(_curses_list_nb - _curses_list_sel.vid < height / 2) {
            _curses_list_first = feeder_begin();
            feeder_next(&_curses_list_first, _curses_list_nb - height);
        }
        else {
            _curses_list_first = feeder_begin();
            feeder_next(&_curses_list_first,
                    _curses_list_sel.vid - height / 2);
        }
        _curses_list_mustdraw = true;
    }

    return true;
}

void curses_list_right(size_t nb)
{
    _curses_list_offset  += nb;
    _curses_list_mustdraw = true;
}

bool curses_list_left(size_t nb)
{
    bool ret;
    if(_curses_list_offset < nb) {
        _curses_list_offset = 0;
        ret = false;
    }
    else {
        _curses_list_offset -= nb;
        ret = true;
    }
    _curses_list_mustdraw = true;
    return ret;
}

void curses_list_offset_reset()
{
    _curses_list_offset   = 0;
    _curses_list_mustdraw = true;
}

void curses_list_set_mode(bool pager)
{
    _curses_list_pager = pager;
}

bool curses_list_get_mode()
{
    return _curses_list_pager;
}

/********************* Bars abilities ****************************************/
bool curses_top_set(const char* str)
{
    if(_curses_top_str)
        free(_curses_top_str);

    if(str) {
        _curses_top_str = strdup(str);
        _curses_top_mustdraw = true;
        if(!_curses_top_enable) {
            _curses_top_enable = true;
            return true;
        }
    }
    else {
        _curses_top_str = NULL;
        if(_curses_top_enable) {
            _curses_top_enable   = false;
            _curses_top_mustdraw = true;
            return true;
        }
    }
    return false;
}

bool curses_bot_set(const char* str)
{
    if(_curses_bot_str)
        free(_curses_bot_str);

    if(str) {
        _curses_bot_str = strdup(str);
        _curses_bot_mustdraw = true;
        if(!_curses_bot_enable) {
            _curses_bot_enable = true;
            return true;
        }
    }
    else {
        _curses_bot_str = NULL;
        if(_curses_bot_enable) {
            _curses_bot_enable   = false;
            _curses_bot_mustdraw = true;
            return true;
        }
    }
    return false;
}

void curses_top_colors(int fg, int bg)
{
    init_pair(1, fg, bg);
    _curses_top_mustdraw = true;
}

void curses_bot_colors(int fg, int bg)
{
    init_pair(2, fg, bg);
    _curses_bot_mustdraw = true;
}

void curses_command_enter(const char* prefix)
{
    _curses_cmd_in       = true;
    _curses_cmd_prefix   = prefix;
    _curses_cmd_text[0]  = '\0';
    _curses_cmd_pos      = 0;
    _curses_cmd_mustdraw = true;
}

const char* curses_command_leave()
{
    if(!_curses_cmd_in)
        return "";
    _curses_cmd_in       = false;
    _curses_cmd_mustdraw = true;
    return _curses_cmd_text;
}

bool curses_command_parse_event(int c)
{
    size_t pos;
    if(isprint(c)) {
        if(_curses_cmd_pos == strlen(_curses_cmd_text)) {
            pos = strlen(_curses_cmd_text);
            _curses_cmd_text[pos] = (char)c;
            _curses_cmd_text[pos + 1] = '\0';
        }
        else {
            memmove(_curses_cmd_text + _curses_cmd_pos + 1,
                    _curses_cmd_text + _curses_cmd_pos,
                    strlen(_curses_cmd_text) - _curses_cmd_pos + 1);
            _curses_cmd_text[_curses_cmd_pos] = (char)c;
        }
        ++_curses_cmd_pos;
        _curses_cmd_mustdraw = true;
    }

    else if(c == KEY_LEFT) {
        if(_curses_cmd_pos > 0)
            --_curses_cmd_pos;
        _curses_cmd_mustdraw = true;
    }
    else if(c == KEY_RIGHT) {
        if(_curses_cmd_pos < strlen(_curses_cmd_text))
            ++_curses_cmd_pos;
        _curses_cmd_mustdraw = true;
    }
    else if(c == KEY_UP) {
        _curses_cmd_pos = 0;
        _curses_cmd_mustdraw = true;
    }
    else if(c == KEY_DOWN) {
        _curses_cmd_pos = strlen(_curses_cmd_text);
        _curses_cmd_mustdraw = true;
    }
    else if(c == KEY_BACKSPACE) {
        if(_curses_cmd_pos == 0)
            return true;
        if(_curses_cmd_pos == strlen(_curses_cmd_text)) {
            pos = strlen(_curses_cmd_text) - 1;
            _curses_cmd_text[pos] = '\0';
        }
        else {
            memmove(_curses_cmd_text + _curses_cmd_pos - 1,
                    _curses_cmd_text + _curses_cmd_pos,
                    strlen(_curses_cmd_text) - _curses_cmd_pos + 1);
        }
        --_curses_cmd_pos;
        _curses_cmd_mustdraw = true;
    }
    else if(c == '\n')
        return false;
    return true;
}

