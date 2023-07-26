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
  #define min(x, y) ((x) < (y) ? (x) : (y))
#endif
#ifndef max
  #define max(x, y) ((x) > (y) ? (x) : (y))
#endif

#define LIBTERMINAL_BACKBUFFER_PAGE_LINES 200
#define LIBTERMINAL_CHUNK_SIZE 4096
#define LIBTERMINAL_MAX_LINE_WIDTH 1024
#define LIBTERMINAL_NAME_MAX 256


typedef enum attributes_e {
  ATTRIBUTE_NONE = 0,
  ATTRIBUTE_BOLD = 1,
  ATTRIBUTE_ITALIC = 2,
  ATTRIBUTE_UNDERLINE = 4
} attributes_e;

typedef struct buffer_styling_t {
  union {
    struct {
      unsigned int foreground : 8;
      unsigned int background : 8;
      unsigned int attributes : 4;
    };
    unsigned int value;
  };
} buffer_styling_t;

typedef struct buffer_char_t {
  buffer_styling_t styling;
  unsigned int codepoint;
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

typedef struct view_t {
  buffer_char_t* buffer;
  int cursor_x, cursor_y;
  buffer_styling_t cursor_styling; // What characters are currently being emitted as.
  cursor_mode_e mode;
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
    // fprintf(stderr, "WAT %d %d\n", view->scrolling_region_start, view->scrolling_region_end);
    memmove(&view->buffer[terminal->columns * view->scrolling_region_start], &view->buffer[terminal->columns * (view->scrolling_region_end - 1)], sizeof(buffer_char_t) * terminal->columns * (view->scrolling_region_end - view->scrolling_region_start - 1));
    memset(&view->buffer[terminal->columns * (view->scrolling_region_end - 1)], 0, sizeof(buffer_char_t) * terminal->columns);
  } else if (terminal->current_view == VIEW_NORMAL_BUFFER) {
    if (terminal->scrollback_total_lines++ > terminal->scrollback_limit) {
      backbuffer_page_t* page = terminal->scrollback_buffer_end;
      if (page->next)
        page->next->prev = NULL;
      terminal->scrollback_buffer_end = page->next;
      terminal->scrollback_total_lines -= page->line;
      free(page);
    }
    if (!terminal->scrollback_buffer_start || terminal->scrollback_buffer_start->columns != terminal->columns || terminal->scrollback_buffer_start->lines != terminal->lines || terminal->scrollback_buffer_start->line >= terminal->scrollback_buffer_start->lines) {
      backbuffer_page_t* page = calloc(sizeof(backbuffer_page_t) + LIBTERMINAL_BACKBUFFER_PAGE_LINES*terminal->columns*sizeof(buffer_char_t), 1);
      backbuffer_page_t* prev = terminal->scrollback_buffer_start;
      page->prev = prev;
      if (prev)
        prev->next = page;
      terminal->scrollback_buffer_start = page;
      page->lines = terminal->lines;
      page->columns = terminal->columns;
      page->line = 0;
    }
    memcpy(&terminal->scrollback_buffer_start->buffer[terminal->scrollback_buffer_start->line * terminal->columns], &view->buffer[0], sizeof(buffer_char_t) * terminal->columns);
    memmove(&view->buffer[0], &view->buffer[terminal->columns], sizeof(buffer_char_t) * terminal->columns * (terminal->lines - 1));
    memset(&view->buffer[terminal->columns * (terminal->lines - 1)], 0, sizeof(buffer_char_t) * terminal->columns);
    terminal->scrollback_buffer_start->line++;
  }
}

static void terminal_switch_buffer(terminal_t* terminal, view_e view) {
  terminal->current_view = view;
  if (view == VIEW_ALTERNATE_BUFFER) {
    memset(terminal->views[VIEW_ALTERNATE_BUFFER].buffer, 0, sizeof(buffer_char_t) * terminal->columns * terminal->lines);
    terminal->views[VIEW_ALTERNATE_BUFFER].cursor_x = 0;
    terminal->views[VIEW_ALTERNATE_BUFFER].cursor_y = 0;
    terminal->views[VIEW_ALTERNATE_BUFFER].cursor_styling.value = 0;
    terminal->views[VIEW_ALTERNATE_BUFFER].scrolling_region_end = -1;
    terminal->views[VIEW_ALTERNATE_BUFFER].scrolling_region_start = -1;
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
  if (type == ESCAPE_TYPE_CSI) {
    int seq_end = strlen(seq) - 1;
    switch (seq[seq_end]) {
      case 'A': view->cursor_y = max(view->cursor_y - parse_number(&seq[2], 1), 0);     break;
      case 'B': view->cursor_y = min(view->cursor_y + parse_number(&seq[2], 1), terminal->lines - 1); break;
      case 'C': view->cursor_x = min(view->cursor_x + parse_number(&seq[2], 1), terminal->columns - 1); break;
      case 'D': view->cursor_x = max(view->cursor_x - parse_number(&seq[2], 1), 0); break;
      case 'E': view->cursor_y = min(view->cursor_y + parse_number(&seq[2], 1), terminal->lines - 1); view->cursor_x = 0; break;
      case 'F': view->cursor_y = min(view->cursor_y - parse_number(&seq[2], 1), 0); view->cursor_x = 0; break;
      case 'G': view->cursor_x = parse_number(&seq[2], 1); break;
      case 'H': {
        int semicolon = -1;
        for (semicolon = 2; semicolon < seq_end && seq[semicolon] != ';'; ++semicolon);
        if (seq[semicolon] != ';') {
          view->cursor_x = 0;
          view->cursor_y = 0;
        } else {
          view->cursor_y = max(min(parse_number(&seq[2], 1) - 1, terminal->lines - 1), 0);
          view->cursor_x = max(min(parse_number(&seq[semicolon+1], 1) - 1, terminal->columns), 0);
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
        switch (seq[2]) {
          case '1': memset(&view->buffer[view->cursor_y * terminal->columns], 0, sizeof(buffer_char_t) * view->cursor_x);     break;
          case '2': memset(&view->buffer[view->cursor_y * terminal->columns], 0, sizeof(buffer_char_t) * terminal->columns);  break;
          default:
            memset(&view->buffer[view->cursor_y * terminal->columns + view->cursor_x], 0, sizeof(buffer_char_t) * (terminal->columns - view->cursor_x));
          break;
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
      case 'd': view->cursor_y = min(max(parse_number(&seq[2], 1), 0), terminal->lines - 1); break;
      case 'h': {
        if (seq[2] == '?') {
          switch (parse_number(&seq[3], 0)) {
            case 12: view->mode = CURSOR_BLINKING; break;
            case 25: view->mode = CURSOR_SOLID; break;
            case 1004: terminal->reporting_focus = 1; break;
            case 1047: terminal_switch_buffer(terminal, VIEW_ALTERNATE_BUFFER); break;
            case 1049: terminal_switch_buffer(terminal, VIEW_ALTERNATE_BUFFER); break;
            case 2004: terminal->paste_mode = PASTE_BRACKETED; break;
            default: unhandled = 1; break;
          }
        }
      } break;
      case 'l': {
        if (seq[2] == '?') {
          switch (parse_number(&seq[3], 0)) {
            case 12: view->mode = CURSOR_SOLID; break;
            case 25: view->mode = CURSOR_HIDDEN; break;
            case 1004: terminal->reporting_focus = 0; break;
            case 1047: terminal_switch_buffer(terminal, VIEW_NORMAL_BUFFER); break;
            case 1049: terminal_switch_buffer(terminal, VIEW_NORMAL_BUFFER); break;
            case 2004: terminal->paste_mode = PASTE_NORMAL; break;
            default: unhandled = 1; break;
          }
        }
      } break;
      case 'm': {
        int offset = 2;
        enum DisplayState {
          DISPLAY_STATE_NONE,
          DISPLAY_STATE_FOREGROUND_COLOR_MODE,
          DISPLAY_STATE_FOREGROUND_COLOR_VALUE,
          DISPLAY_STATE_BACKGROUND_COLOR_MODE,
          DISPLAY_STATE_BACKGROUND_COLOR_VALUE,
        };
        enum DisplayState state = DISPLAY_STATE_NONE;
        while (1) {
          switch (state) {
            case DISPLAY_STATE_NONE:
              switch (parse_number(&seq[offset], 0)) {
                case 0  : view->cursor_styling = (buffer_styling_t) { 0, 0, 0 }; break;
                case 1  : view->cursor_styling.attributes |= ATTRIBUTE_BOLD; break;
                case 3  : view->cursor_styling.attributes |= ATTRIBUTE_ITALIC; break;
                case 4  : view->cursor_styling.attributes |= ATTRIBUTE_UNDERLINE; break;
                case 30 : view->cursor_styling.foreground = 0; break;
                case 31 : view->cursor_styling.foreground = 1; break;
                case 32 : view->cursor_styling.foreground = 2; break;
                case 33 : view->cursor_styling.foreground = 3; break;
                case 34 : view->cursor_styling.foreground = 4; break;
                case 35 : view->cursor_styling.foreground = 5; break;
                case 36 : view->cursor_styling.foreground = 6; break;
                case 37 : view->cursor_styling.foreground = 7; break;
                case 38 : state = DISPLAY_STATE_FOREGROUND_COLOR_MODE; break;
                case 39 : view->cursor_styling.foreground = 0; break;
                case 40 : view->cursor_styling.background = 0; break;
                case 41 : view->cursor_styling.background = 1; break;
                case 42 : view->cursor_styling.background = 2; break;
                case 43 : view->cursor_styling.background = 3; break;
                case 44 : view->cursor_styling.background = 4; break;
                case 45 : view->cursor_styling.background = 5; break;
                case 46 : view->cursor_styling.background = 6; break;
                case 47 : view->cursor_styling.background = 7; break;
                case 48 : state = DISPLAY_STATE_BACKGROUND_COLOR_MODE; break;
                case 49 : view->cursor_styling.background = 0; break;
                case 90 : view->cursor_styling.foreground = 251; break;
                case 91 : view->cursor_styling.foreground = 160; break;
                case 92 : view->cursor_styling.foreground = 119; break;
                case 93 : view->cursor_styling.foreground = 226; break;
                case 94 : view->cursor_styling.foreground = 81; break;
                case 95 : view->cursor_styling.foreground = 201; break;
                case 96 : view->cursor_styling.foreground = 51; break;
                case 97 : view->cursor_styling.foreground = 231; break;
                case 100: view->cursor_styling.background = 251; break;
                case 101: view->cursor_styling.background = 160; break;
                case 102: view->cursor_styling.background = 119; break;
                case 103: view->cursor_styling.background = 226; break;
                case 104: view->cursor_styling.background = 81; break;
                case 105: view->cursor_styling.background = 201; break;
                case 106: view->cursor_styling.background = 51; break;
                case 107: view->cursor_styling.background = 231; break;
              }
            break;
            case DISPLAY_STATE_FOREGROUND_COLOR_MODE: state = DISPLAY_STATE_FOREGROUND_COLOR_VALUE; break;
            case DISPLAY_STATE_BACKGROUND_COLOR_MODE: state = DISPLAY_STATE_BACKGROUND_COLOR_VALUE; break;
            case DISPLAY_STATE_FOREGROUND_COLOR_VALUE: view->cursor_styling.foreground = parse_number(&seq[offset], 0); break;
            case DISPLAY_STATE_BACKGROUND_COLOR_VALUE: view->cursor_styling.background = parse_number(&seq[offset], 0); break;
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
          //view->scrolling_region_start = parse_number(&seq[2], 1) - 1;
          //view->scrolling_region_end = parse_number(&seq[semicolon+1], 1) - 1;
        }
      } break;
      default: unhandled = 1; break;
    }
  } else if (type == ESCAPE_TYPE_OS) {
    if (strlen(seq) >= 5 && seq[2] == '0' && seq[3] == ';')
      strncpy(terminal->name, &seq[4], min(sizeof(terminal->name) - 1, strlen(seq) - 5));
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
        (escape_type == ESCAPE_TYPE_OS && (str[offset] == '\0' || str[offset] == '\a' || str[offset] == 0x1B)) ||
        (escape_type == ESCAPE_TYPE_UNKNOWN && str[offset] == 0x1B) ||
        (escape_type == ESCAPE_TYPE_FIXED_WIDTH && buffered_sequence_index == fixed_width || str[offset] == 0x1B)
      ) {
        terminal->buffered_sequence[buffered_sequence_index++] = 0;
        terminal_escape_sequence(terminal, escape_type, terminal->buffered_sequence);
        view = &terminal->views[terminal->current_view];
        buffered_sequence_index = 0;
        terminal->buffered_sequence[0] = 0;
        if (str[offset] == 0x1B && (escape_type == ESCAPE_TYPE_UNKNOWN || escape_type == ESCAPE_TYPE_OS || escape_type == ESCAPE_TYPE_FIXED_WIDTH)) {
          escape_type = ESCAPE_TYPE_OPEN;
          terminal->buffered_sequence[buffered_sequence_index++] = str[offset];
        } else {
          escape_type = ESCAPE_TYPE_NONE;
        }
      }
      ++offset;
    } else {
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
          if (view->cursor_x) {
            --view->cursor_x;
            view->buffer[view->cursor_y * terminal->lines + view->cursor_x].codepoint = ' ';
          }
        } break;
        case 0x09:
        break;
        case '\n': {
          view->cursor_x = 0;
          if (view->cursor_y < ((view->scrolling_region_end != -1 ? view->scrolling_region_end : terminal->lines) - 1))
            ++view->cursor_y;
          else
            terminal_shift_buffer(terminal);
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
          view->buffer[view->cursor_y * terminal->columns + view->cursor_x] = (buffer_char_t){ view->cursor_styling, codepoint };
          view->cursor_x++;
          if (view->cursor_x >= terminal->columns) {
            view->cursor_x = 0;
            if (view->cursor_y < ((view->scrolling_region_end != -1 ? view->scrolling_region_end : terminal->lines) - 1))
              ++view->cursor_y;
            else
              terminal_shift_buffer(terminal);
          }
        break;
      }
    }
  }
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
      int max_lines = min(terminal->lines, lines);
      for (int y = 0; y < max_lines; ++y)
        memcpy(&buffer[y*columns], &terminal->views[i].buffer[y*terminal->columns], min(terminal->columns, columns)*sizeof(buffer_char_t));
      free(terminal->views[i].buffer);
    }
    terminal->views[i].buffer = buffer;
    terminal->views[i].cursor_x = min(terminal->views[i].cursor_x, columns - 1);
    terminal->views[i].cursor_y = min(terminal->views[i].cursor_y, lines - 1);
  }
  terminal->columns = columns;
  terminal->lines = lines;
}

static terminal_t* terminal_new(int columns, int lines, int scrollback_limit, const char* term_env, const char* pathname, const char** argv) {
  terminal_t* terminal = calloc(sizeof(terminal_t), 1);
  for (int i = 0; i < VIEW_MAX; ++i) {
    terminal->views[i].scrolling_region_end = -1;
    terminal->views[i].scrolling_region_start = -1;
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
    term.c_cc[VINTR] = 127;
    term.c_cc[VSUSP] = 26;
    term.c_cc[VERASE] = '\b';
    term.c_cc[VEOL] = '\n';
    term.c_cc[VEOF] = 4;
    term.c_lflag |= ISIG | ECHO | ICANON;
    term.c_iflag |= IUTF8;
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
      lua_pushinteger(L, style.foreground << 16 | style.background << 8 | style.attributes);
      lua_rawseti(L, -2, ++group);
      lua_pushlstring(L, text_buffer, block_size);
      lua_rawseti(L, -2, ++group);
      block_size = 0;
      if (start >= end)
        break;
      style = start->styling;
    }
    if (start->codepoint != 0) {
      block_size += codepoint_to_utf8(start->codepoint, &text_buffer[block_size]);
    } else
      text_buffer[block_size++] = ' ';
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

static int f_terminal_resize(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  int x = luaL_checkinteger(L, 2), y = luaL_checkinteger(L, 3);
  terminal_resize(terminal, x, y);
  return 0;
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
  switch (terminal->views[terminal->current_view].mode) {
    case CURSOR_SOLID: lua_pushliteral(L, "solid"); break;
    case CURSOR_HIDDEN: lua_pushliteral(L, "hidden"); break;
    case CURSOR_BLINKING: lua_pushliteral(L, "blinking"); break;
  }
  return 3;
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
  lua_pushstring(L, terminal->name);
  return 1;
}

static const luaL_Reg terminal_api[] = {
  { "__gc",          f_terminal_gc         },
  { "new",           f_terminal_new        },
  { "close",         f_terminal_close      },
  { "input",         f_terminal_input      },
  { "lines",         f_terminal_lines      },
  { "resize",        f_terminal_resize     },
  { "update",        f_terminal_update     },
  { "exited",        f_terminal_exited     },
  { "cursor",        f_terminal_cursor     },
  { "focused",       f_terminal_focused    },
  { "pastemode",     f_terminal_pastemode  },
  { "scrollback",    f_terminal_scrollback },
  { "name",          f_terminal_name       },
  { NULL,            NULL                  }
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
