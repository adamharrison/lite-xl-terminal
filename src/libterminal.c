#if _WIN32
  // https://devblogs.microsoft.com/commandline/windows-command-line-introducing-the-windows-pseudo-console-conpty/
  #if __MINGW32__ || __MINGW64__ // https://stackoverflow.com/questions/66419746/is-there-support-for-winpty-in-mingw-w64
    #define NTDDI_VERSION 0x0A000006 //NTDDI_WIN10_RS5
    #undef _WIN32_WINNT
    #define _WIN32_WINNT 0x0A00 // _WIN32_WINNT_WIN10
  #endif
  #include <windows.h>
  #include <wincon.h>
#else
  #include <pthread.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/ioctl.h>
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <signal.h>
  #if __APPLE__
    #include <util.h>
  #else
    #include <pty.h>
  #endif
#endif
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#ifdef LIBTERMINAL_STANDALONE
  #include <lua.h>
  #include <lauxlib.h>
  #include <lualib.h>
#else
  #define LITE_XL_PLUGIN_ENTRYPOINT
  #include <lite_xl_plugin_api.h>
#endif

#ifndef min
  static int min(int a, int b) { return a < b ? a : b; }
  static int max(int a, int b) { return a > b ? a : b; }
#endif

#define LIBTERMINAL_BACKBUFFER_PAGE_LINES 200
#define LIBTERMINAL_CHUNK_SIZE 4096
#define LIBTERMINAL_MAX_LINE_WIDTH 1024
#define LIBTERMINAL_NAME_MAX 256
#define LIBTERMINAL_DEFAULT_TAB_SIZE 8

typedef enum attributes_e {
  // Colors
  ATTRIBUTE_UNSET_COLOR = 0,
  ATTRIBUTE_INVERSE_COLOR = 1,
  ATTRIBUTE_INDEX_COLOR = 2,
  ATTRIBUTE_RGB_COLOR = 3,
  ATTRIBUTE_UNTARGETED_COLOR = 4,
  // Attributes
  ATTRIBUTE_BOLD = 8,
  ATTRIBUTE_ITALIC = 16,
  ATTRIBUTE_UNDERLINE = 32
} attributes_e;

typedef struct color_t {
  union {
    struct {
      uint8_t attributes;
      union {
        struct {
          uint8_t r;
          uint8_t g;
          uint8_t b;
        };
        uint8_t index;
      };
    };
    uint32_t value;
  };
} color_t;
static color_t indexed_color(uint8_t index) { return (color_t) { .attributes = ATTRIBUTE_INDEX_COLOR, .index = index }; }
static color_t rgb_color(uint8_t r, uint8_t g, uint8_t b) { return (color_t) { .attributes = ATTRIBUTE_RGB_COLOR, .r = r, .g = g, .b = b }; }
static color_t UNSET_COLOR = { .attributes = ATTRIBUTE_UNSET_COLOR, .index = 0 };
static color_t INVERSE_COLOR = { .attributes = ATTRIBUTE_INVERSE_COLOR, .index = 0 };
static color_t UNTARGETED_COLOR = { .attributes = ATTRIBUTE_UNTARGETED_COLOR, .index = 0 };

#define LIBTERMINAL_NO_STYLING (buffer_styling_t) { UNSET_COLOR, UNSET_COLOR }

typedef struct buffer_styling_t {
  union {
    struct {
      color_t foreground;
      color_t background;
    };
    uint64_t value;
  };
} buffer_styling_t;

typedef struct buffer_char_t {
  buffer_styling_t styling;
  uint32_t codepoint;
} buffer_char_t;

typedef struct backbuffer_page_t {
  struct backbuffer_page_t* prev;
  struct backbuffer_page_t* next;
  int columns, lines, line;
  buffer_char_t buffer[];
} backbuffer_page_t;

typedef enum view_e {
  VIEW_NORMAL_BUFFER = 0,
  VIEW_ALTERNATE_BUFFER = 1,
  VIEW_MAX = 2
} view_e;

typedef enum cursor_mode_e {
  CURSOR_SOLID         = 0,
  CURSOR_HIDDEN        = 1,
  CURSOR_BLINKING      = 2,
} cursor_mode_e;

typedef enum paste_mode_e {
  PASTE_NORMAL,
  PASTE_BRACKETED
} paste_mode_e;

typedef enum keys_mode_e {
  KEYS_MODE_NORMAL,
  KEYS_MODE_APPLICATION
} keys_mode_e;

typedef enum mouse_tracking_mode_e {
  MOUSE_TRACKING_NONE,
  MOUSE_TRACKING_X10,
  MOUSE_TRACKING_NORMAL,
  MOUSE_TRACKING_SGR
} mouse_tracking_mode_e;

typedef enum charset_e {
  CHARSET_US,
  CHARSET_DEC,
  CHARSET_OTHER
} charset_e;

typedef struct view_t {
  buffer_char_t* buffer;
  int cursor_x, cursor_y;
  int cursor_styling_inversed;
  buffer_styling_t cursor_styling; // What characters are currently being emitted as.
  cursor_mode_e cursor_mode;
  keys_mode_e cursor_keys_mode;
  keys_mode_e keypad_keys_mode;
  mouse_tracking_mode_e mouse_tracking_mode;
  color_t palette[256];                // Custom palette as per the ^][4;#;rgb:24/04/3C command. The fuck?
  charset_e charset;
  int tab_size;
  // The index of where the scrolling region starts/ends.
  // If enabled, disables shuffling of text to the scrollback
  // buffer. When applied, shifts cursor to the top, and allows you to write
  // text wherever. However, if you hit newline, at the bottom of the scroll region
  // shifts only that region up.
  int scrolling_region_start, scrolling_region_end;
} view_t;


typedef struct {
  int debug;                                         // If true, dumps output to working directory in a file called `terminal.log`.
  backbuffer_page_t* scrollback_buffer_end;          // End of the linked list.
  backbuffer_page_t* scrollback_buffer_start;        // Beginning of linked list.
  backbuffer_page_t* scrollback_target;              // Target based on scrollback_position.
  int scrollback_target_top_offset;                  // The offset that the top of the scrollback_target page is from the start of the buffer.
  int scrollback_total_lines;                        // Cached total amount of lines we can scroll bcak.
  int scrollback_position;                           // Canonical amount of lines we've scrolled back.
  int scrollback_limit;                              // The amount of lines we'll hold in memory maximum.
  int columns, lines;
  view_e current_view;
  view_t views[VIEW_MAX];                            // Normally just two buffers, normal, and alternate.
  paste_mode_e paste_mode;
  int reporting_focus;                               // Enables/disbles reporting focus.
  char name[LIBTERMINAL_NAME_MAX];                   // Window name, set with OS command.
  char buffered_sequence[LIBTERMINAL_CHUNK_SIZE];
  #if _WIN32
    PROCESS_INFORMATION process_information;
    HPCON hpcon;
    HANDLE topty;
    HANDLE frompty;
    char nonblocking_buffer[LIBTERMINAL_CHUNK_SIZE];   // Oh my god, I hate windows so much.
    int nonblocking_buffer_length;
    HANDLE nonblocking_buffer_mutex;
    HANDLE nonblocking_thread;
  #else
    int master;                                        // FD for pty.
    pid_t pid;                                         // pid for shell.
  #endif
} terminal_t;


static int utf8_to_codepoint(const char *p, unsigned *dst) {
  const unsigned char *up = (unsigned char*)p;
  unsigned res, n;
  switch (*p & 0xf0) {
    case 0xf0 :  res = *up & 0x07;  n = 3;  break;
    case 0xe0 :  res = *up & 0x0f;  n = 2;  break;
    case 0xd0 :
    case 0xc0 :  res = *up & 0x1f;  n = 1;  break;
    default   :  res = *up;         n = 0;  break;
  }
  while (n--) {
    res = (res << 6) | (*(++up) & 0x3f);
  }
  *dst = res;
  return ((const char*)up + 1) - p;
}

static int codepoint_to_utf8(unsigned int codepoint, char* target) {
  if (codepoint < 128) {
    *(target++) = codepoint;
    return 1;
  } else if (codepoint < 2048) {
    *(target++) = 0xC0 | (codepoint >> 6);
    *(target++) = 0x80 | ((codepoint >> 0) & 0x3F);
    return 2;
  } else if (codepoint < 65536) {
    *(target++) = 0xE0 | (codepoint >> 12);
    *(target++) = 0x80 | ((codepoint >> 6) & 0x3F);
    *(target++) = 0x80 | ((codepoint >> 0) & 0x3F);
    return 3;
  }
  *(target++) = 0xF0 | (codepoint >> 18);
  *(target++) = 0x80 | ((codepoint >> 12) & 0x3F);
  *(target++) = 0x80 | ((codepoint >> 6) & 0x3F);
  *(target++) = 0x80 | ((codepoint >> 0) & 0x3F);
  return 4;
}

static int terminal_scrollback(terminal_t* terminal, int target) {
  if (!terminal->scrollback_buffer_start || target <= 0) {
    terminal->scrollback_target = NULL;
    terminal->scrollback_position = 0;
    terminal->scrollback_target_top_offset = 0;
    return 0;
  }
  backbuffer_page_t* current = NULL;
  int current_top_offset = 0;
  if (terminal->scrollback_target) {
    current = terminal->scrollback_target;
    current_top_offset = terminal->scrollback_target_top_offset;
  }
  while (target > current_top_offset) {
    if (!current) {
      current = terminal->scrollback_buffer_start;
      current_top_offset = current->line;
    } else {
      if (!current->prev) {
        terminal->scrollback_target = current;
        terminal->scrollback_position = current_top_offset;
        terminal->scrollback_target_top_offset = current_top_offset;
        return terminal->scrollback_position;
      }
      current = current->prev;
      current_top_offset += current->line;
    }
  }
  while (target < (current_top_offset - current->line)) {
    if (!current->next) {
      terminal->scrollback_target = NULL;
      terminal->scrollback_position = 0;
      terminal->scrollback_target_top_offset = 0;
      return 0;
    }
    current_top_offset -= current->line;
    current = current->next;
  }
  terminal->scrollback_target = current;

  terminal->scrollback_target_top_offset = current_top_offset;
  terminal->scrollback_position = target;
  assert(terminal->scrollback_position >= 0);
  return terminal->scrollback_position;
}

static void terminal_input(terminal_t* terminal, const char* str, int len) {
  #ifdef _WIN32
    WriteFile(terminal->topty, str, len, NULL, NULL);
  #else
    write(terminal->master, str, len);
  #endif
}

static void terminal_clear_scrollback_buffer(terminal_t* terminal) {
  backbuffer_page_t* scrollback_buffer = terminal->scrollback_buffer_start;
  while (scrollback_buffer) {
      backbuffer_page_t* prev = scrollback_buffer->prev;
      free(scrollback_buffer);
      scrollback_buffer = prev;
  }
  terminal->scrollback_buffer_start = NULL;
  terminal->scrollback_buffer_end = NULL;
  terminal->scrollback_target = NULL;
  terminal->scrollback_target_top_offset = 0;
}

static void terminal_shift_buffer(terminal_t* terminal) {
  view_t* view = &terminal->views[terminal->current_view];

  if (view->scrolling_region_start != -1 && view->scrolling_region_end != -1) {
    // We perform this song and dance in case of Guldoman levels of resizing.
    int start = min(view->scrolling_region_start, terminal->lines - 1);
    int start_plus_1 = min((view->scrolling_region_start + 1), terminal->lines - 1);
    int end = min(view->scrolling_region_end, terminal->lines);
    if (start_plus_1 != start)
      memmove(&view->buffer[terminal->columns * start], &view->buffer[terminal->columns * start_plus_1], sizeof(buffer_char_t) * terminal->columns * (end - start));
    memset(&view->buffer[terminal->columns * max(end - 1 , 0)], 0, sizeof(buffer_char_t) * terminal->columns);
    return;
  }
  if (terminal->current_view == VIEW_NORMAL_BUFFER) {
    if (terminal->scrollback_total_lines++ > terminal->scrollback_limit) {
      backbuffer_page_t* page = terminal->scrollback_buffer_end;
      if (page->next)
        page->next->prev = NULL;
      terminal->scrollback_buffer_end = page->next;
      terminal->scrollback_total_lines -= page->line;
      free(page);
    }
    if (!terminal->scrollback_buffer_start || terminal->scrollback_buffer_start->columns != terminal->columns || terminal->scrollback_buffer_start->line >= terminal->scrollback_buffer_start->lines) {
      backbuffer_page_t* page = calloc(sizeof(backbuffer_page_t) + LIBTERMINAL_BACKBUFFER_PAGE_LINES*terminal->columns*sizeof(buffer_char_t), 1);
      backbuffer_page_t* prev = terminal->scrollback_buffer_start;
      page->prev = prev;
      if (prev)
        prev->next = page;
      terminal->scrollback_buffer_start = page;
      page->lines = LIBTERMINAL_BACKBUFFER_PAGE_LINES;
      page->columns = terminal->columns;
      page->line = 0;
    }
    memcpy(&terminal->scrollback_buffer_start->buffer[terminal->scrollback_buffer_start->line * terminal->columns], &view->buffer[0], sizeof(buffer_char_t) * terminal->columns);
    terminal->scrollback_buffer_start->line++;
  }
  memmove(&view->buffer[0], &view->buffer[terminal->columns], sizeof(buffer_char_t) * terminal->columns * (terminal->lines - 1));
  memset(&view->buffer[terminal->columns * (terminal->lines - 1)], 0, sizeof(buffer_char_t) * terminal->columns);
}

static void terminal_switch_buffer(terminal_t* terminal, view_e view) {
  terminal->current_view = view;
  if (view == VIEW_ALTERNATE_BUFFER) {
    memset(terminal->views[VIEW_ALTERNATE_BUFFER].buffer, 0, sizeof(buffer_char_t) * terminal->columns * terminal->lines);
    terminal->views[VIEW_ALTERNATE_BUFFER].cursor_x = 0;
    terminal->views[VIEW_ALTERNATE_BUFFER].cursor_y = 0;
    terminal->views[VIEW_ALTERNATE_BUFFER].cursor_styling = LIBTERMINAL_NO_STYLING;
    terminal->views[VIEW_ALTERNATE_BUFFER].cursor_styling_inversed = 0;
    terminal->views[VIEW_ALTERNATE_BUFFER].scrolling_region_end = -1;
    terminal->views[VIEW_ALTERNATE_BUFFER].scrolling_region_start = -1;
    for (int i = 0; i < 256; ++i)
      terminal->views[VIEW_ALTERNATE_BUFFER].palette[i] = indexed_color(i);
  }
}

static int parse_number(const char* seq, int def) {
  if (seq[0] >= '0' && seq[0] <= '9')
    return atoi(seq);
  return def;
}

typedef enum terminal_escape_type_e {
  ESCAPE_TYPE_NONE,
  ESCAPE_TYPE_OPEN,
  ESCAPE_TYPE_CSI,
  ESCAPE_TYPE_OS,
  ESCAPE_TYPE_FIXED_WIDTH,
  ESCAPE_TYPE_UNKNOWN
} terminal_escape_type_e;


static int terminal_escape_sequence(terminal_t* terminal, terminal_escape_type_e type, const char* seq) {
  #ifdef LIBTERMINAL_DEBUG_ESCAPE
  fprintf(stderr, "ESC");
  for (int i = 1; i < strlen(seq); ++i) {
    fprintf(stderr, "%c", seq[i]);
  }
  fprintf(stderr, "\n");
  #endif
  view_t* view = &terminal->views[terminal->current_view];
  int unhandled = 0;
  int end = (view->scrolling_region_end == -1 ? terminal->lines : view->scrolling_region_end);
  if (type == ESCAPE_TYPE_CSI) {
    int seq_end = strlen(seq) - 1;
    switch (seq[seq_end]) {
      case '@': {
        int length = parse_number(&seq[2], 1);
        memmove(&view->buffer[terminal->columns * view->cursor_y + view->cursor_x + length], &view->buffer[terminal->columns * view->cursor_y + view->cursor_x], sizeof(buffer_char_t) * max(terminal->columns - (view->cursor_x + length), 0));
        for (int i = view->cursor_x; i < min(view->cursor_x + length, terminal->columns); ++i)
          view->buffer[terminal->columns * view->cursor_y + i].codepoint = ' ';
      } break;
      case 'A': view->cursor_y = max(view->cursor_y - parse_number(&seq[2], 1), 0);     break;
      case 'B': view->cursor_y = min(view->cursor_y + parse_number(&seq[2], 1), terminal->lines - 1); break;
      case 'C': view->cursor_x = min(view->cursor_x + parse_number(&seq[2], 1), terminal->columns - 1); break;
      case 'D': view->cursor_x = max(view->cursor_x - parse_number(&seq[2], 1), 0); break;
      case 'E': view->cursor_y = min(view->cursor_y + parse_number(&seq[2], 1), terminal->lines - 1); view->cursor_x = 0; break;
      case 'F': view->cursor_y = min(view->cursor_y - parse_number(&seq[2], 1), 0); view->cursor_x = 0; break;
      case 'G': view->cursor_x = min(max(parse_number(&seq[2], 1) - 1, 0), terminal->columns - 1); break;
      case 'f':
      case 'H': {
        int semicolon = -1;
        for (semicolon = 2; semicolon < seq_end && seq[semicolon] != ';'; ++semicolon);
        if (seq[semicolon] != ';') {
          view->cursor_x = 0;
          view->cursor_y = 0;
        } else {
          view->cursor_y = max(min(parse_number(&seq[2], 1) - 1, terminal->lines - 1), 0);
          view->cursor_x = max(min(parse_number(&seq[semicolon+1], 1) - 1, terminal->columns - 1), 0);
        }
      } break;
      case 'J':  {
        switch (seq[2]) {
          case '1':
            for (int y = 0; y < view->cursor_y; ++y) {
              int w = y == view->cursor_y - 1 ? view->cursor_x : terminal->columns;
              memset(&view->buffer[terminal->columns * y], 0, sizeof(buffer_char_t) * w);
            }
          break;
          case '3':
            terminal_clear_scrollback_buffer(terminal);
            // intentional fallthrough
          case '2':
            memset(view->buffer, 0, sizeof(buffer_char_t) * (terminal->columns * terminal->lines));
            view->cursor_x = 0;
            view->cursor_y = 0;
          break;
          default:
            for (int y = view->cursor_y; y < terminal->lines; ++y) {
              int x = y == view->cursor_y ? view->cursor_x : 0;
              memset(&view->buffer[terminal->columns * y + x], 0, sizeof(buffer_char_t) * (terminal->columns - x));
            }
          break;
        }
      } break;
      case 'K': {
        int s, e;
        switch (seq[2]) {
          case '1': s = 0; e = view->cursor_x + 1; break;
          case '2': s = 0; e = terminal->columns; break;
          default: s = view->cursor_x; e = terminal->columns; break;
        }
        for (int i = s; i < e; ++i)
          view->buffer[view->cursor_y * terminal->columns + i] = (buffer_char_t){ view->cursor_styling, ' ' };
      } break;
      case 'L': {
        int length = parse_number(&seq[2], 1);
        memmove(&view->buffer[terminal->columns * (view->cursor_y + length)], &view->buffer[terminal->columns * view->cursor_y], sizeof(buffer_char_t) * terminal->columns * max(end - (view->cursor_y + length), 0));
        for (int y = view->cursor_y; y < min(view->cursor_y + length, end); ++y) {
          for (int i = 0; i < terminal->columns; ++i)
            view->buffer[terminal->columns * y + i].codepoint = ' ';
        }
      } break;
      case 'M': {
        int length = parse_number(&seq[2], 1);
        memmove(&view->buffer[terminal->columns * view->cursor_y], &view->buffer[terminal->columns * (view->cursor_y + length)], sizeof(buffer_char_t) * terminal->columns * max(end - (view->cursor_y + length), 0));
        for (int y = end - length; y < end; ++y) {
          for (int i = 0; i < terminal->columns; ++i)
            view->buffer[terminal->columns * y + i].codepoint = ' ';
        }
      } break;
      case 'P': {
        int length = parse_number(&seq[2], 1);
        for (int i = view->cursor_x; i < terminal->columns; ++i) {
          if (i + length < terminal->columns)
            view->buffer[view->cursor_y * terminal->columns + i] = view->buffer[view->cursor_y * terminal->columns + i + length];
          else
            view->buffer[view->cursor_y * terminal->columns + i].codepoint = ' ';
        }
      } break;
      case 'X': {
        int length = parse_number(&seq[2], 1);
        for (int i = view->cursor_x; i < view->cursor_x + length && i < terminal->columns; ++i)
          view->buffer[view->cursor_y * terminal->columns + i].codepoint = ' ';
      } break;
      case 'b': {
        int length = parse_number(&seq[2], 1);
        if (view->cursor_x > 0) {
          for (int i = view->cursor_x - 1; i < min(view->cursor_x + length, terminal->columns); ++i)
            view->buffer[view->cursor_y * terminal->columns + i].codepoint = view->buffer[view->cursor_y * terminal->columns + view->cursor_x].codepoint;
        }
      } break;
      case 'd': view->cursor_y = min(max(parse_number(&seq[2], 1) - 1, 0), terminal->lines - 1); break;
      case 'h': {
        if (seq[2] == '?') {
          const char* next = &seq[3];
          while (next) {
            switch (parse_number(next, 0)) {
              case 1: view->cursor_keys_mode = KEYS_MODE_APPLICATION; break;
              case 9: view->mouse_tracking_mode = MOUSE_TRACKING_X10; break;
              case 12: view->cursor_mode = CURSOR_BLINKING; break;
              case 25: view->cursor_mode = CURSOR_SOLID; break;
              case 1000: if (view->mouse_tracking_mode != MOUSE_TRACKING_SGR) view->mouse_tracking_mode = MOUSE_TRACKING_NORMAL; break;
              case 1006: view->mouse_tracking_mode = MOUSE_TRACKING_SGR; break;
              case 1004: terminal->reporting_focus = 1; break;
              case 1047: terminal_switch_buffer(terminal, VIEW_ALTERNATE_BUFFER); break;
              case 1049: terminal_switch_buffer(terminal, VIEW_ALTERNATE_BUFFER); break;
              case 2004: terminal->paste_mode = PASTE_BRACKETED; break;
              default: unhandled = 1; break;
            }
            if (next = strstr(next, ";"))
              next++;
          }
        }
      } break;
      case 'l': {
        if (seq[2] == '?') {
          const char* next = &seq[3];
          while (next) {
            switch (parse_number(next, 0)) {
              case 1: view->cursor_keys_mode = KEYS_MODE_NORMAL; break;
              case 9: view->mouse_tracking_mode = MOUSE_TRACKING_NONE; break;
              case 12: view->cursor_mode = CURSOR_SOLID; break;
              case 25: view->cursor_mode = CURSOR_HIDDEN; break;
              case 1000: view->mouse_tracking_mode = MOUSE_TRACKING_NONE; break;
              case 1006: view->mouse_tracking_mode = MOUSE_TRACKING_NONE; break;
              case 1004: terminal->reporting_focus = 0; break;
              case 1047: terminal_switch_buffer(terminal, VIEW_NORMAL_BUFFER); break;
              case 1049: terminal_switch_buffer(terminal, VIEW_NORMAL_BUFFER); break;
              case 2004: terminal->paste_mode = PASTE_NORMAL; break;
              default: unhandled = 1; break;
            }
            if (next = strstr(next, ";"))
              next++;
          }
        }
      } break;
      case 'm': {
        int offset = 2;
        enum DisplayState {
          DISPLAY_STATE_NONE,
          DISPLAY_STATE_COLOR_MODE,
          DISPLAY_STATE_COLOR_VALUE_IDX,
          DISPLAY_STATE_COLOR_VALUE_R,
          DISPLAY_STATE_COLOR_VALUE_G,
          DISPLAY_STATE_COLOR_VALUE_B
        };
        enum DisplayState state = DISPLAY_STATE_NONE;
        uint8_t r = 0,g = 0,b = 0;
        int foreground = 0;
        while (1) {
          color_t target_color = UNTARGETED_COLOR;
          int target_foreground = 0;
          switch (state) {
            case DISPLAY_STATE_NONE:
              switch (parse_number(&seq[offset], 0)) {
                case 0  : view->cursor_styling = LIBTERMINAL_NO_STYLING; break;
                case 1  : view->cursor_styling.foreground.attributes |= ATTRIBUTE_BOLD; break;
                case 3  : view->cursor_styling.foreground.attributes |= ATTRIBUTE_ITALIC; break;
                case 4  : view->cursor_styling.foreground.attributes |= ATTRIBUTE_UNDERLINE; break;
                case 7  : {
                  view->cursor_styling_inversed = 1;
                  color_t background = view->cursor_styling.background;
                  if (view->cursor_styling.foreground.value == UNSET_COLOR.value)
                    view->cursor_styling.background = INVERSE_COLOR;
                  else if (view->cursor_styling.foreground.value == INVERSE_COLOR.value)
                    view->cursor_styling.background = UNSET_COLOR;
                  else
                    view->cursor_styling.background = view->cursor_styling.foreground;
                  if (background.value == UNSET_COLOR.value)
                    view->cursor_styling.foreground = INVERSE_COLOR;
                  else if (background.value == INVERSE_COLOR.value)
                    view->cursor_styling.foreground = UNSET_COLOR;
                  else
                    view->cursor_styling.foreground = view->cursor_styling.background;
                } break;
                case 30 : target_foreground = 1; target_color = view->palette[0]; break;
                case 31 : target_foreground = 1; target_color = view->palette[1]; break;
                case 32 : target_foreground = 1; target_color = view->palette[2]; break;
                case 33 : target_foreground = 1; target_color = view->palette[3]; break;
                case 34 : target_foreground = 1; target_color = view->palette[4]; break;
                case 35 : target_foreground = 1; target_color = view->palette[5]; break;
                case 36 : target_foreground = 1; target_color = view->palette[6]; break;
                case 37 : target_foreground = 1; target_color = view->palette[7]; break;
                case 38 : state = DISPLAY_STATE_COLOR_MODE; foreground = 1; break;
                case 39 : target_foreground = 1; target_color = UNSET_COLOR; break;
                case 40 : target_foreground = 0; target_color = view->palette[0]; break;
                case 41 : target_foreground = 0; target_color = view->palette[1]; break;
                case 42 : target_foreground = 0; target_color = view->palette[2]; break;
                case 43 : target_foreground = 0; target_color = view->palette[3]; break;
                case 44 : target_foreground = 0; target_color = view->palette[4]; break;
                case 45 : target_foreground = 0; target_color = view->palette[5]; break;
                case 46 : target_foreground = 0; target_color = view->palette[6]; break;
                case 47 : target_foreground = 0; target_color = view->palette[7]; break;
                case 48 : state = DISPLAY_STATE_COLOR_MODE; foreground = 0; break;
                case 49 : target_foreground = 0; target_color = UNSET_COLOR; break;
                case 90 : target_foreground = 1; target_color = view->palette[251]; break;
                case 91 : target_foreground = 1; target_color = view->palette[160]; break;
                case 92 : target_foreground = 1; target_color = view->palette[119]; break;
                case 93 : target_foreground = 1; target_color = view->palette[226]; break;
                case 94 : target_foreground = 1; target_color = view->palette[81]; break;
                case 95 : target_foreground = 1; target_color = view->palette[201]; break;
                case 96 : target_foreground = 1; target_color = view->palette[51]; break;
                case 97 : target_foreground = 1; target_color = view->palette[231]; break;
                case 100: target_foreground = 0; target_color = view->palette[251]; break;
                case 101: target_foreground = 0; target_color = view->palette[160]; break;
                case 102: target_foreground = 0; target_color = view->palette[119]; break;
                case 103: target_foreground = 0; target_color = view->palette[226]; break;
                case 104: target_foreground = 0; target_color = view->palette[81]; break;
                case 105: target_foreground = 0; target_color = view->palette[201]; break;
                case 106: target_foreground = 0; target_color = view->palette[51]; break;
                case 107: target_foreground = 0; target_color = view->palette[231]; break;
                default: unhandled = 1; break;
              }
            break;
            case DISPLAY_STATE_COLOR_MODE: state = parse_number(&seq[offset], 0) != 5 ? DISPLAY_STATE_COLOR_VALUE_R : DISPLAY_STATE_COLOR_VALUE_IDX; break;
            case DISPLAY_STATE_COLOR_VALUE_IDX:
              target_foreground = foreground;
              target_color = view->palette[(parse_number(&seq[offset], 0) & 0xFF)];
              state = DISPLAY_STATE_NONE;
            break;
            case DISPLAY_STATE_COLOR_VALUE_R: r = (parse_number(&seq[offset], 0) & 0xFF); state = DISPLAY_STATE_COLOR_VALUE_G; break;
            case DISPLAY_STATE_COLOR_VALUE_G: g = (parse_number(&seq[offset], 0) & 0xFF); state = DISPLAY_STATE_COLOR_VALUE_B; break;
            case DISPLAY_STATE_COLOR_VALUE_B: {
              target_foreground = foreground;
              b = parse_number(&seq[offset], 0) & 0xFF;
              target_color = rgb_color(r, g, b);
              state = DISPLAY_STATE_NONE;
            } break;
          }
          if (target_color.value != UNTARGETED_COLOR.value) {
            if (view->cursor_styling_inversed)
              target_foreground = !target_foreground;
            if (target_foreground)
              view->cursor_styling.foreground = target_color;
            else
              view->cursor_styling.background = target_color;
          }
          char* next = strchr(&seq[offset], ';');
          if (!next)
            break;
          offset = (next - seq) + 1;
        }
        return 0;
      } break;
      case 'n': {
        if (parse_number(&seq[2], 0) == 6) {
          char buffer[8];
          int length = snprintf(buffer, sizeof(buffer), "\x1B[%d;%d", view->cursor_y, view->cursor_x);
          terminal_input(terminal, buffer, length);
        } else
          unhandled = 1;
      } break;
      case 'r': {
        int semicolon = -1;
        for (semicolon = 2; semicolon < seq_end && seq[semicolon] != ';'; ++semicolon);
        view->cursor_x = 0;
        view->cursor_y = 0;
        if (seq[semicolon] == ';') {
          view->scrolling_region_start = min(max(parse_number(&seq[2], 1) - 1, 0), terminal->lines - 1);
          view->scrolling_region_end = min(max(parse_number(&seq[semicolon+1], 1), 0), terminal->lines);
        }
      } break;
      default: unhandled = 1; break;
    }
  } else if (type == ESCAPE_TYPE_OS) {
    switch (seq[2]) {
      case '0':
        if (strlen(seq) >= 5 && seq[3] == ';')
          strncpy(terminal->name, &seq[4], min(sizeof(terminal->name) - 1, strlen(seq) - 5));
      break;
      case '4':
        int idx, r,g,b;
        if (sscanf(&seq[3], ";%d;rgb:%x/%x/%x", &idx, &r, &g, &b) == 4) {
          view->palette[idx] = rgb_color(r, g, b);
        } else
          unhandled = 1;
      break;
      default: unhandled = 1; break;
    }
  } else if (type == ESCAPE_TYPE_FIXED_WIDTH) {
    switch (seq[1]) {
      case '(':
        switch (seq[2]) {
          case '0': view->charset = CHARSET_DEC; break;
          case 'B': view->charset = CHARSET_US; break;
          default: view->charset = CHARSET_OTHER; break;
        }
      break;
      case '=': view->keypad_keys_mode = KEYS_MODE_APPLICATION; break;
      case '>': view->keypad_keys_mode = KEYS_MODE_NORMAL; break;
      case 'M':
        if (view->cursor_y == 0) {
          memmove(&view->buffer[terminal->columns], &view->buffer[0], sizeof(buffer_char_t)*terminal->columns*(terminal->lines-1));
          memset(&view->buffer[0], 0, sizeof(buffer_char_t)*terminal->columns);
        } else {
          --view->cursor_y;
          view->cursor_x = 0;
        }
      break;
      default: unhandled = 1; break;
    }
  }

  if (unhandled) {
    #ifdef LIBTERMINAL_DEBUG_ESCAPE
      fprintf(stderr, "UNKNOWN ESCAPE SEQUENCE\n");
    #endif
    return -1;
  }
  return 0;
}


static terminal_escape_type_e get_terminal_escape_type(char a, int* fixed_width) {
  switch (a) {
    case '[': return ESCAPE_TYPE_CSI;
    case ']': return ESCAPE_TYPE_OS;
    case 'D':
    case 'E':
    case 'H':
    case 'M':
    case 'Z':
    case 'F':
    case '>':
    case '=':
    case '7':
    case '8':
    case 'c':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
    case '|':
    case '}':
    case '~':
      *fixed_width = 2;
      return ESCAPE_TYPE_FIXED_WIDTH;
    case ' ':
    case '#':
    case '%':
    case '(':
    case ')':
    case '*':
    case '+':
      *fixed_width = 3;
      return ESCAPE_TYPE_FIXED_WIDTH;
  }
  return ESCAPE_TYPE_UNKNOWN;
}

static terminal_escape_type_e parse_partial_sequence(const char* seq, int len, int* fixed_width) {
  if (len == 0)
    return ESCAPE_TYPE_NONE;
  if (len == 1)
    return ESCAPE_TYPE_OPEN;
  return get_terminal_escape_type(seq[1], fixed_width);
}

static int translate_charset(charset_e charset, int codepoint) {
  if (charset == CHARSET_DEC) {
    switch (codepoint) {
      case 0x5F: codepoint = ' '; break;
      case 0x60: codepoint = 0x25C6; break;
      case 0x61: codepoint = 0x2592; break;
      case 0x62: codepoint = '\t'; break;
      case 0x63: codepoint = '\f'; break;
      case 0x64: codepoint = '\r'; break;
      case 0x65: codepoint = '\n'; break;
      case 0x66: codepoint = 0xB0; break;
      case 0x67: codepoint = 0xB1; break;
      case 0x68: codepoint = '\n'; break;
      case 0x69: codepoint = '\v'; break;
      case 0x6A: codepoint = 0x2518; break;
      case 0x6B: codepoint = 0x2510; break;
      case 0x6C: codepoint = 0x250C; break;
      case 0x6D: codepoint = 0x2514; break;
      case 0x6E: codepoint = 0x253C; break;
      case 0x70: codepoint = 0x23BB; break;
      case 0x71: codepoint = 0x2500; break;
      case 0x72: codepoint = 0x23BC; break;
      case 0x73: codepoint = 0x23BD; break;
      case 0x74: codepoint = 0x251C; break;
      case 0x75: codepoint = 0x2524; break;
      case 0x76: codepoint = 0x2534; break;
      case 0x77: codepoint = 0x252C; break;
      case 0x78: codepoint = 0x2502; break;
      case 0x79: codepoint = 0x2264; break;
      case 0x7A: codepoint = 0x2265; break;
      case 0x7B: codepoint = 0x03C0; break;
      case 0x7C: codepoint = 0x2260; break;
      case 0x7D: codepoint = 0x00A3; break;
      case 0x7E: codepoint = 0x00B7; break;
    }
  }
  return codepoint;
}

static void terminal_output(terminal_t* terminal, const char* str, int len) {
  if (terminal->debug)  {
    FILE* file = fopen("terminal.log", "ab");
    fwrite(str, sizeof(char), len, file);
    fclose(file);
  }
  unsigned int codepoint;
  int offset = 0;
  int buffered_sequence_index = strlen(terminal->buffered_sequence);
  view_t* view = &terminal->views[terminal->current_view];
  int fixed_width = -1;
  terminal_escape_type_e escape_type = parse_partial_sequence(terminal->buffered_sequence, buffered_sequence_index, &fixed_width);
  while (offset < len) {
    if (escape_type != ESCAPE_TYPE_NONE) {
      terminal->buffered_sequence[buffered_sequence_index++] = str[offset];
      escape_type = parse_partial_sequence(terminal->buffered_sequence, buffered_sequence_index, &fixed_width);
      if (
        (escape_type == ESCAPE_TYPE_CSI && buffered_sequence_index > 2 && str[offset] >= 0x40 && str[offset] <= 0x7E) ||
        (escape_type == ESCAPE_TYPE_OS && ((offset < len - 1 && (str[offset+1] == '\a') || (offset < len - 2 && str[offset+1] == 0x1B && str[offset+2] == 0x5C)))) ||
        (escape_type == ESCAPE_TYPE_UNKNOWN && str[offset] == 0x1B) ||
        (escape_type == ESCAPE_TYPE_FIXED_WIDTH && buffered_sequence_index == fixed_width || str[offset] == 0x1B)
      ) {
        terminal->buffered_sequence[buffered_sequence_index++] = 0;
        terminal_escape_sequence(terminal, escape_type, terminal->buffered_sequence);
        view = &terminal->views[terminal->current_view];
        buffered_sequence_index = 0;
        terminal->buffered_sequence[0] = 0;
        if (escape_type == ESCAPE_TYPE_OS) {
          escape_type = ESCAPE_TYPE_NONE;
          offset += str[offset+1] == 0x1B ? 2 : 1;
        } else if (str[offset] == 0x1B && (escape_type == ESCAPE_TYPE_UNKNOWN || escape_type == ESCAPE_TYPE_FIXED_WIDTH)) {
          escape_type = ESCAPE_TYPE_OPEN;
          terminal->buffered_sequence[buffered_sequence_index++] = str[offset];
        } else {
          escape_type = ESCAPE_TYPE_NONE;
        }
      }
      ++offset;
    } else {
      int end = (view->scrolling_region_end == -1 ? terminal->lines : view->scrolling_region_end);
      offset += utf8_to_codepoint(&str[offset], &codepoint);
      switch (codepoint) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        break;
        case '\b': {
          if (view->cursor_x)
            --view->cursor_x;
        } break;
        case '\t': {
          view->cursor_x = (view->cursor_x + view->tab_size) - ((view->cursor_x + view->tab_size) % view->tab_size);
        } break;
        case '\n': {
          if (view->cursor_y < (end - 1))
            ++view->cursor_y;
          else {
            terminal_shift_buffer(terminal);
          }
        } break;
        case 0x0B:
        case 0x0C:
        break;
        case '\r': {
          view->cursor_x = 0;
        } break;
        case 0x0E:
        case 0x0F:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1A:
          break;
        case 0x1B: { // escape
          terminal->buffered_sequence[0] = 0x1B;
          escape_type = ESCAPE_TYPE_OPEN;
          buffered_sequence_index = 1;
        } break;
        case 0x1C:
        case 0x1D:
        case 0x1E:
        case 0x1F:
          break;
        default:
          if (view->cursor_x >= terminal->columns) {
            view->cursor_x = 0;
            if (view->cursor_y < (end - 1))
              ++view->cursor_y;
            else
              terminal_shift_buffer(terminal);
          }
          view->buffer[view->cursor_y * terminal->columns + view->cursor_x] = (buffer_char_t){ view->cursor_styling, translate_charset(view->charset, codepoint) };
          view->cursor_x++;
        break;
      }
    }
  }
  terminal->buffered_sequence[buffered_sequence_index] = 0;
}

#ifdef _WIN32
  static DWORD windows_nonblocking_thread_callback(void* data) {
    terminal_t* terminal = (terminal_t*)data;
    char chunk_buffer[LIBTERMINAL_CHUNK_SIZE];
    while (1) {
      DWORD bytes_read;
      if (sizeof(chunk_buffer) - terminal->nonblocking_buffer_length > 0) {
        if (!ReadFile(terminal->frompty, chunk_buffer, sizeof(chunk_buffer) - terminal->nonblocking_buffer_length, &bytes_read, NULL))
          break;
        if (bytes_read > 0) {
          WaitForSingleObject(terminal->nonblocking_buffer_mutex, INFINITE);
          memcpy(&terminal->nonblocking_buffer[terminal->nonblocking_buffer_length], chunk_buffer, bytes_read);
          terminal->nonblocking_buffer_length += bytes_read;
          ReleaseMutex(terminal->nonblocking_buffer_mutex);
        }
      }
      Sleep(1);
    }
  }
#endif

static int terminal_update(terminal_t* terminal) {
  char chunk[LIBTERMINAL_CHUNK_SIZE];
  int len, at_least_one = 0;
  #ifdef _WIN32
    WaitForSingleObject(terminal->nonblocking_buffer_mutex, INFINITE);
    if (terminal->nonblocking_buffer_length > 0) {
      terminal_output(terminal, terminal->nonblocking_buffer, terminal->nonblocking_buffer_length);
      at_least_one = 1;
    }
    terminal->nonblocking_buffer_length = 0;
    ReleaseMutex(terminal->nonblocking_buffer_mutex);
    return at_least_one;
  #else
    do {
      len = read(terminal->master, chunk, sizeof(chunk));
      if (len == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return at_least_one;
      terminal_output(terminal, chunk, len);
      at_least_one = 1;
    } while (len > 0);
  #endif
  return -1;
}

static int terminal_close(terminal_t* terminal) {
  terminal_clear_scrollback_buffer(terminal);
  for (int i = 0; i < VIEW_MAX; ++i) {
    if (terminal->views[i].buffer)
      free(terminal->views[i].buffer);
    terminal->views[i].buffer = NULL;
  }
  #if _WIN32
    if (terminal->nonblocking_thread)
      TerminateThread(terminal->nonblocking_thread, 0);
    if (terminal->topty)
      CloseHandle(terminal->topty);
    if (terminal->frompty)
      CloseHandle(terminal->frompty);
    if (terminal->hpcon)
      ClosePseudoConsole(terminal->hpcon);
    if (terminal->nonblocking_buffer_mutex)
      CloseHandle(terminal->nonblocking_buffer_mutex);
    if (terminal->process_information.hProcess)
      TerminateProcess(terminal->process_information.hProcess, 1);
  #else
    if (terminal->pid) {
      close(terminal->master);
      kill(terminal->pid, SIGHUP);
      int status;
      if (waitpid(terminal->pid, &status, WNOHANG))
        terminal->pid = 0;
      else
        return -1;
    }
  #endif
  return 0;
}

static void terminal_free(terminal_t* terminal) {
  terminal_close(terminal);
  #ifdef _WIN32
  #else
    if (terminal->pid)
      kill(terminal->pid, SIGKILL);
  #endif
  free(terminal);
}

static void terminal_resize(terminal_t* terminal, int columns, int lines) {
  if (terminal->columns == columns && terminal->lines == lines)
    return;
  #ifdef _WIN32
    COORD size = { columns, lines };
    ResizePseudoConsole(terminal->hpcon, size);
  #else
    struct winsize size = { .ws_row = lines, .ws_col = columns, .ws_xpixel = 0, .ws_ypixel = 0 };
    ioctl(terminal->master, TIOCSWINSZ, &size);
  #endif
  for (int i = 0; i < VIEW_MAX; ++i) {
    buffer_char_t* buffer = malloc(sizeof(buffer_char_t) * columns * lines);
    memset(buffer, 0, sizeof(buffer_char_t) * columns * lines);
    if (terminal->views[i].buffer) {
      if (lines < terminal->lines && i == VIEW_NORMAL_BUFFER) {
        for (int j = 0; j < max(0, (terminal->views[i].cursor_y+1) - lines); ++j)
          terminal_shift_buffer(terminal);
      }
      int max_lines = min(terminal->lines, lines);
      for (int y = 0; y < max_lines; ++y)
        memcpy(&buffer[y*columns], &terminal->views[i].buffer[y*terminal->columns], min(terminal->columns, columns)*sizeof(buffer_char_t));
      free(terminal->views[i].buffer);
    }
    terminal->views[i].buffer = buffer;
    terminal->views[i].cursor_x = min(terminal->views[i].cursor_x, columns - 1);
    terminal->views[i].cursor_y = min(terminal->views[i].cursor_y, lines - 1);
    if (terminal->views[i].scrolling_region_end != -1 || terminal->views[i].scrolling_region_end != -1) {
      terminal->views[i].scrolling_region_start = min(terminal->views[i].scrolling_region_start, lines - 1);
      terminal->views[i].scrolling_region_end = min(terminal->views[i].scrolling_region_end, lines);
    }
  }
  terminal->columns = columns;
  terminal->lines = lines;
}

static terminal_t* terminal_new(int columns, int lines, int scrollback_limit, const char* term_env, const char* pathname, const char** argv) {
  terminal_t* terminal = calloc(sizeof(terminal_t), 1);
  for (int i = 0; i < VIEW_MAX; ++i) {
    for (int j = 0; j < 256; ++j)
      terminal->views[i].palette[j] = indexed_color(j);
    terminal->views[i].scrolling_region_end = -1;
    terminal->views[i].scrolling_region_start = -1;
    terminal->views[i].cursor_styling = LIBTERMINAL_NO_STYLING;
    terminal->views[i].tab_size = LIBTERMINAL_DEFAULT_TAB_SIZE;
  }
  terminal->scrollback_limit = scrollback_limit;
  #ifdef _WIN32
    SECURITY_ATTRIBUTES no_sec = { .nLength = sizeof(SECURITY_ATTRIBUTES), .bInheritHandle = TRUE, .lpSecurityDescriptor = NULL };
    HANDLE out_pipe_pseudo_console_side, in_pipe_pseudo_console_side;
    COORD size = { columns, lines };
    BOOL success =
      CreatePipe(&in_pipe_pseudo_console_side, &terminal->topty, &no_sec, 0) &&
      CreatePipe(&terminal->frompty, &out_pipe_pseudo_console_side, &no_sec, 0) &&
      !FAILED(CreatePseudoConsole(size, in_pipe_pseudo_console_side, out_pipe_pseudo_console_side, 0, &terminal->hpcon));
    if (success) {
      terminal->nonblocking_buffer_mutex = CreateMutex(NULL, FALSE, NULL);

      HANDLE handles_to_inherit[] = { in_pipe_pseudo_console_side, out_pipe_pseudo_console_side };
      STARTUPINFOEXW si_ex = {0};
      si_ex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
      si_ex.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
      si_ex.StartupInfo.hStdInput = NULL;
      si_ex.StartupInfo.hStdOutput = NULL;
      si_ex.StartupInfo.hStdError = NULL;
      size_t list_size;
      // Create the appropriately sized thread attribute list
      InitializeProcThreadAttributeList(NULL, 2, 0, &list_size);
      si_ex.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(list_size);
      success = InitializeProcThreadAttributeList(si_ex.lpAttributeList, 2, 0, (PSIZE_T)&list_size) &&
        UpdateProcThreadAttribute(si_ex.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, terminal->hpcon, sizeof(HPCON), NULL, NULL);
        UpdateProcThreadAttribute(si_ex.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles_to_inherit, sizeof(handles_to_inherit), NULL, NULL);
      free(si_ex.lpAttributeList);

      if (success) {
        int len = MultiByteToWideChar(CP_UTF8, 0, pathname, -1, NULL, 0);
        wchar_t* commandline = malloc(sizeof(wchar_t)*(len+1));
        len = MultiByteToWideChar(CP_UTF8, 0, pathname, -1, commandline, len);
        success = CreateProcessW(NULL, commandline, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &si_ex.StartupInfo, &terminal->process_information);
        free(commandline);
        if (success)
          terminal->nonblocking_thread = CreateThread(NULL, 0, windows_nonblocking_thread_callback, terminal, 0, NULL);
      }
    }
    if (!success) {
      free(terminal);
      return NULL;
    }
  #else
    struct termios term = {0};
    term.c_cc[VINTR] = 3;
    term.c_cc[VSTART] = '\x13';
    term.c_cc[VSTOP] = '\x11';
    term.c_cc[VSUSP] = 26;
    term.c_cc[VERASE] = '\x7F';
    term.c_cc[VEOL] = 0;
    term.c_cc[VEOF] = 4;
    term.c_lflag |= ISIG | ECHO | ICANON | IEXTEN | ECHOE | ECHOK | ECHOCTL | ECHOKE;
    term.c_cflag |= CS8 | CREAD;
    term.c_iflag |= IUTF8 | ICRNL | IXON;
    term.c_oflag |= OPOST | ONLCR | NL0 | CR0 | TAB0 | BS0 | VT0 | FF0;
    terminal->pid = forkpty(&terminal->master, NULL, &term, NULL);
    if (terminal->pid == -1) {
      free(terminal);
      return NULL;
    }
    if (!terminal->pid) {
      setenv("TERM", term_env, 1);
      execvp(pathname,  (char** const)argv);
      exit(-1);
      return NULL;
    }
    int flags = fcntl(terminal->master, F_GETFD, 0);
    fcntl(terminal->master, F_SETFL, flags | O_NONBLOCK);
  #endif
  terminal_resize(terminal, columns, lines);
  return terminal;
}


static void output_line(lua_State* L, buffer_char_t* start, buffer_char_t* end) {
  lua_newtable(L);
  int block_size = 0;
  int group = 0;
  char text_buffer[LIBTERMINAL_MAX_LINE_WIDTH] = {0};
  buffer_styling_t style = start->styling;
  while (1) {
    if (start >= end || start->styling.value != style.value) {
      uint64_t packed = (
        ((uint64_t)style.foreground.attributes << 56) |
        ((uint64_t)style.foreground.r << 48) |
        ((uint64_t)style.foreground.g << 40) |
        ((uint64_t)style.foreground.b << 32) |
        ((uint64_t)style.background.attributes << 24) |
        ((uint64_t)style.background.r << 16) |
        ((uint64_t)style.background.g << 8) |
        ((uint64_t)style.background.b << 0)
      );
      lua_pushinteger(L, packed);
      lua_rawseti(L, -2, ++group);
      lua_pushlstring(L, text_buffer, block_size);
      lua_rawseti(L, -2, ++group);
      block_size = 0;
      if (start >= end)
        break;
      style = start->styling;
    }
    block_size += codepoint_to_utf8(start->codepoint != 0 ? start->codepoint : ' ', &text_buffer[block_size]);
    ++start;
  }
}


static int f_terminal_lines(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  lua_newtable(L);

  int total_lines = 0;
  int remaining_lines = terminal->lines;
  view_t* view = &terminal->views[terminal->current_view];
  if (terminal->current_view == VIEW_NORMAL_BUFFER && terminal->scrollback_target) {
    backbuffer_page_t* current_backbuffer = terminal->scrollback_target;
    int lines_into_buffer = terminal->scrollback_target_top_offset - terminal->scrollback_position;
    while (current_backbuffer) {
      for (int y = lines_into_buffer; y < current_backbuffer->line; ++y) {
        output_line(L, &current_backbuffer->buffer[y * current_backbuffer->columns], &current_backbuffer->buffer[(y+1) * current_backbuffer->columns]);
        lua_rawseti(L, -2, ++total_lines);
        if (remaining_lines == 0)
          break;
      }
      current_backbuffer = current_backbuffer->next;
      lines_into_buffer = 0;
    }
  }
  if (remaining_lines > 0) {
    for (int y = 0; y < remaining_lines; ++y) {
      output_line(L, &view->buffer[y * terminal->columns], &view->buffer[(y+1) * terminal->columns]);
      lua_rawseti(L, -2, ++total_lines);
    }
  }
  return 1;
}



static int f_terminal_new(lua_State* L) {
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int scrollback_limit = luaL_checkinteger(L, 3);
  const char* term_env = luaL_checkstring(L, 4);
  const char* path = luaL_checkstring(L, 5);
  char* arguments[256];
  arguments[0] = (char*)path;
  arguments[1] = NULL;
  if (lua_type(L, 6) == LUA_TTABLE) {
    for (int i = 0; i < 255; ++i) {
      lua_rawgeti(L, 6, i+1);
      if (!lua_isnil(L, -1)) {
        const char* str = luaL_checkstring(L, -1);
        arguments[i+1] = strdup(str);
        lua_pop(L, 1);
      } else {
        lua_pop(L, 1);
        arguments[i+1] = NULL;
        break;
      }
    }
  }
  int debug = lua_toboolean(L, 7);
  terminal_t* terminal = terminal_new(x, y, scrollback_limit, term_env, path, (const char**)arguments);
  terminal->debug = debug;
  for (int i = 1; i < 256 && arguments[i]; ++i)
    free(arguments[i]);
  if (!terminal)
    return luaL_error(L, "error creating terminal");
  terminal_t** pointer = lua_newuserdata(L, sizeof(terminal_t*));
  *pointer = terminal;
  luaL_setmetatable(L, "libterminal");
  return 1;
}

static int f_terminal_gc(lua_State* L) {
  terminal_free(*(terminal_t**)lua_touserdata(L, 1));
  return 0;
}

static int f_terminal_close(lua_State* L) {
  lua_pushinteger(L, terminal_close(*(terminal_t**)lua_touserdata(L, 1)));
  return 1;
}

static int f_terminal_update(lua_State* L){
  int status = terminal_update(*(terminal_t**)lua_touserdata(L, 1));
  lua_pushboolean(L, status != 0);
  return 1;
}

static int f_terminal_input(lua_State* L) {
  size_t len;
  const char* str = luaL_checklstring(L, 2, &len);
  terminal_input(*(terminal_t**)lua_touserdata(L, 1), str, (int)len);
  return f_terminal_update(L);
}

static int f_terminal_size(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  if (lua_gettop(L) > 1) {
    int x = luaL_checkinteger(L, 2), y = luaL_checkinteger(L, 3);
    terminal_resize(terminal, x, y);
  }
  lua_pushinteger(L, terminal->columns);
  lua_pushinteger(L, terminal->lines);
  return 2;
}


static int f_terminal_exited(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  #if _WIN32
    DWORD exit_code;
    if (GetExitCodeProcess(terminal->process_information.hProcess, &exit_code) && exit_code != STILL_ACTIVE) {
      lua_pushinteger(L, exit_code);
    } else
      lua_pushboolean(L, 0);
  #else
    int status;
    if (waitpid(terminal->pid, &status, WNOHANG) > 0) {
      lua_pushinteger(L, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    } else
      lua_pushboolean(L, 0);
  #endif
  return 1;
}

static int f_terminal_cursor(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  lua_pushinteger(L, terminal->views[terminal->current_view].cursor_x);
  lua_pushinteger(L, terminal->views[terminal->current_view].cursor_y);
  switch (terminal->views[terminal->current_view].cursor_mode) {
    case CURSOR_SOLID: lua_pushliteral(L, "solid"); break;
    case CURSOR_HIDDEN: lua_pushliteral(L, "hidden"); break;
    case CURSOR_BLINKING: lua_pushliteral(L, "blinking"); break;
  }
  return 3;
}

static int f_terminal_cursor_keys_mode(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  switch (terminal->views[terminal->current_view].cursor_keys_mode) {
    case KEYS_MODE_NORMAL: lua_pushliteral(L, "normal"); break;
    case KEYS_MODE_APPLICATION: lua_pushliteral(L, "application"); break;
  }
  return 1;
}

static int f_terminal_keypad_keys_mode(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  switch (terminal->views[terminal->current_view].keypad_keys_mode) {
    case KEYS_MODE_NORMAL: lua_pushliteral(L, "normal"); break;
    case KEYS_MODE_APPLICATION: lua_pushliteral(L, "application"); break;
  }
  return 1;
}

static int f_terminal_scrollback(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  if (lua_gettop(L) >= 2)
    terminal_scrollback(terminal, luaL_checkinteger(L, 2));
  lua_pushinteger(L, terminal->scrollback_position);
  return 1;
}

static int f_terminal_focused(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  if (terminal->reporting_focus)
    terminal_input(terminal, lua_toboolean(L, 2) ? "\x1B[" : "\x1B[O", 3);
  return 0;
}

static int f_terminal_pastemode(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  lua_pushstring(L, terminal->paste_mode == PASTE_BRACKETED ? "bracketed" : "normal");
  return 1;
}

static int f_terminal_name(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  if (terminal->name[0])
    lua_pushstring(L, terminal->name);
  else
    lua_pushnil(L);
  return 1;
}

static int f_terminal_mouse_tracking_mode(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  switch (terminal->views[terminal->current_view].mouse_tracking_mode) {
    case MOUSE_TRACKING_NONE: lua_pushnil(L); break;
    case MOUSE_TRACKING_X10: lua_pushliteral(L, "x10"); break;
    case MOUSE_TRACKING_NORMAL: lua_pushliteral(L, "normal"); break;
    case MOUSE_TRACKING_SGR: lua_pushliteral(L, "sgr"); break;
  }
  return 1;
}

static const luaL_Reg terminal_api[] = {
  { "__gc",                f_terminal_gc                     },
  { "new",                 f_terminal_new                    },
  { "close",               f_terminal_close                  },
  { "input",               f_terminal_input                  },
  { "lines",               f_terminal_lines                  },
  { "size",                f_terminal_size                   },
  { "update",              f_terminal_update                 },
  { "exited",              f_terminal_exited                 },
  { "cursor",              f_terminal_cursor                 },
  { "focused",             f_terminal_focused                },
  { "mouse_tracking_mode", f_terminal_mouse_tracking_mode    },
  { "cursor_keys_mode",    f_terminal_cursor_keys_mode       },
  { "keypad_keys_mode",    f_terminal_keypad_keys_mode       },
  { "pastemode",           f_terminal_pastemode              },
  { "scrollback",          f_terminal_scrollback             },
  { "name",                f_terminal_name                   },
  { NULL,                  NULL                              }
};


#ifndef LIBTERMINAL_VERSION
  #define LIBTERMINAL_VERSION "unknown"
#endif

#ifndef LIBTERMINAL_STANDALONE
int luaopen_lite_xl_libterminal(lua_State* L, void* XL) {
  lite_xl_plugin_init(XL);
#else
int luaopen_libterminal(lua_State* L) {
#endif
  luaL_newmetatable(L, "libterminal");
  luaL_setfuncs(L, terminal_api, 0);
  lua_pushliteral(L, LIBTERMINAL_VERSION);
  lua_setfield(L, -2, "version");
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  return 1;
}
