#if _WIN32
  #include <direct.h>
  #include <windows.h>
  #define usleep(x) Sleep((x)/1000)
#else
  #include <pthread.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/ioctl.h>
  #include <pty.h>
  #include <sys/types.h>
  #include <sys/wait.h>
#endif
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

static char* dup_string(const char* str) {
  char* dup = malloc(strlen(str)+1);
  strcpy(dup, str);
  return dup;
}


enum EAttributes {
  ATTRIBUTE_NONE = 0,
  ATTRIBUTE_BOLD = 1,
  ATTRIBUTE_ITALIC = 2,
  ATTRIBUTE_UNDERLINE = 4
};

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

typedef enum {
  VIEW_NORMAL_BUFFER = 0,
  VIEW_ALTERNATE_BUFFER = 1,
  VIEW_MAX = 2
} EView;


typedef struct {
  backbuffer_page_t* scrollback_buffer;    // Beginning of linked list.
  backbuffer_page_t* scrollback_target;    // Target based on scrollback_position.
  int scrollback_target_top_offset;        // The offset that the top of the scrollback_target page is from the start of the buffer..
  int total_scrollback_lines;              // Cached total amount of lines we can scroll bcak.
  int scrollback_position;                 // Canonical amount of lines we've scrolled back.
  int scrollback_limit;                    // The amount of lines we'll hold in memory maximum.
  int columns, lines;
  buffer_char_t* buffers[VIEW_MAX];     // Normally just two buffers, normal, and alternate.
  EView view;
  int cursor_x, cursor_y;
  buffer_styling_t cursor_styling;      // What characters are currently being emitted as.
  int master;                           // FD for pty.
  pid_t pid;                            // pid for shell.
  char buffered_sequence[LIBTERMINAL_CHUNK_SIZE];
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

static int terminal_scrollback(terminal_t* terminal, int target) {
  if (!terminal->scrollback_buffer || target <= 0) {
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
      current = terminal->scrollback_buffer;
      current_top_offset = current->line;
    } else {
      if (!current->prev) {
        terminal->scrollback_target = current;
        terminal->scrollback_position = current_top_offset;
        return current_top_offset;
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
  write(terminal->master, str, len);
}

static void terminal_clear_scrollback_buffer(terminal_t* terminal) {
  backbuffer_page_t* scrollback_buffer = terminal->scrollback_buffer;
  while (scrollback_buffer) {
      backbuffer_page_t* prev = scrollback_buffer->prev;
      free(scrollback_buffer);
      scrollback_buffer = prev;
  }
  terminal->scrollback_buffer = NULL;
  terminal->scrollback_target = NULL;
  terminal->scrollback_target_top_offset = 0;
}

static void terminal_shift_buffer(terminal_t* terminal) {
  assert(terminal->view == VIEW_NORMAL_BUFFER);
  if (!terminal->scrollback_buffer || terminal->scrollback_buffer->columns != terminal->columns || terminal->scrollback_buffer->lines != terminal->lines || terminal->scrollback_buffer->line >= terminal->scrollback_buffer->lines) {
    backbuffer_page_t* page = calloc(sizeof(backbuffer_page_t) + LIBTERMINAL_BACKBUFFER_PAGE_LINES*terminal->columns*sizeof(buffer_char_t), 1);
    backbuffer_page_t* prev = terminal->scrollback_buffer;
    page->prev = prev;
    if (prev)
      prev->next = page;
    terminal->scrollback_buffer = page;
    page->lines = terminal->lines;
    page->columns = terminal->columns;
    page->line = 0;
  }
  memcpy(&terminal->scrollback_buffer->buffer[terminal->scrollback_buffer->line * terminal->columns], &terminal->buffers[VIEW_NORMAL_BUFFER][0], sizeof(buffer_char_t) * terminal->columns);
  memmove(&terminal->buffers[VIEW_NORMAL_BUFFER][0], &terminal->buffers[VIEW_NORMAL_BUFFER][terminal->columns], sizeof(buffer_char_t) * terminal->columns * (terminal->lines - 1));
  memset(&terminal->buffers[VIEW_NORMAL_BUFFER][terminal->columns * (terminal->lines - 1)], 0, sizeof(buffer_char_t) * terminal->columns);
  terminal->scrollback_buffer->line++;
}

static void terminal_switch_buffer(terminal_t* terminal, EView view) {
  terminal->view = view;
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
  ESCAPE_TYPE_OS
} terminal_escape_type_e;

static int terminal_escape_sequence(terminal_t* terminal, terminal_escape_type_e type, const char* seq) {
  #ifdef LIBTERMINAL_DEBUG_ESCAPE
  fprintf(stderr, "ESC");
  for (int i = 1; i < strlen(seq); ++i) {
    fprintf(stderr, "%c", seq[i]);
  }
  fprintf(stderr, "\n");
  #endif
  if (type == ESCAPE_TYPE_CSI) {
    int seq_end = strlen(seq) - 1;
    switch (seq[seq_end]) {
      case 'A': terminal->cursor_y = max(terminal->cursor_y - parse_number(&seq[2], 1), 0);     break;
      case 'B': terminal->cursor_y = min(terminal->cursor_y + parse_number(&seq[2], 1), terminal->lines - 1); break;
      case 'C': terminal->cursor_x = min(terminal->cursor_x + parse_number(&seq[2], 1), terminal->columns - 1); break;
      case 'D': terminal->cursor_x = max(terminal->cursor_x - parse_number(&seq[2], 1), 0); break;
      case 'E': terminal->cursor_y = min(terminal->cursor_y + parse_number(&seq[2], 1), terminal->lines - 1); terminal->cursor_x = 0; break;
      case 'F': terminal->cursor_y = min(terminal->cursor_y - parse_number(&seq[2], 1), 0); terminal->cursor_x = 0; break;
      case 'G': terminal->cursor_x = parse_number(&seq[2], 1); break;
      case 'H':
        int semicolon = -1;
        for (semicolon = 2; semicolon < seq_end && seq[semicolon] != ';'; ++semicolon);
        if (seq[semicolon] != ';') {
          terminal->cursor_x = 0;
          terminal->cursor_y = 0;
        } else {
          terminal->cursor_x = parse_number(&seq[2], 1) - 1;
          terminal->cursor_y = parse_number(&seq[semicolon+1], 1) - 1;
        }
      break;
      case 'J':  {
        switch (seq[2]) {
          case '1':
            for (int y = 0; y < terminal->cursor_y; ++y) {
              int w = y == terminal->cursor_y - 1 ? terminal->cursor_x : terminal->columns;
              memset(&terminal->buffers[terminal->view][terminal->columns * y], 0, sizeof(buffer_char_t) * w);
            }
          break;
          case '3':
            terminal_clear_scrollback_buffer(terminal);
            // intentional fallthrough
          case '2':
            memset(terminal->buffers[terminal->view], 0, sizeof(buffer_char_t) * (terminal->columns * terminal->lines));
            terminal->cursor_x = 0;
            terminal->cursor_y = 0;
          break;
          default:
            for (int y = terminal->cursor_y; y < terminal->lines; ++y) {
              int x = y == terminal->cursor_y ? terminal->cursor_x : 0;
              memset(&terminal->buffers[terminal->view][terminal->columns * y + x], 0, sizeof(buffer_char_t) * (terminal->columns - x));
            }
          break;
        }
      } break;
      case 'K': {
        switch (seq[2]) {
          case '1': memset(&terminal->buffers[terminal->view]  [terminal->cursor_y * terminal->columns], 0, sizeof(buffer_char_t) * terminal->cursor_x);     break;
          case '2': memset(&terminal->buffers[terminal->view][terminal->cursor_y * terminal->columns], 0, sizeof(buffer_char_t) * terminal->columns);      break;
          default:
            memset(&terminal->buffers[terminal->view][terminal->cursor_y * terminal->columns + terminal->cursor_x], 0, sizeof(buffer_char_t) * (terminal->columns - terminal->cursor_x));
          break;
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
                case 0  : terminal->cursor_styling = (buffer_styling_t) { 0, 0, 0 }; break;
                case 1  : terminal->cursor_styling.attributes |= ATTRIBUTE_BOLD; break;
                case 3  : terminal->cursor_styling.attributes |= ATTRIBUTE_ITALIC; break;
                case 4  : terminal->cursor_styling.attributes |= ATTRIBUTE_UNDERLINE; break;
                case 30 : terminal->cursor_styling.foreground = 0; break;
                case 31 : terminal->cursor_styling.foreground = 1; break;
                case 32 : terminal->cursor_styling.foreground = 2; break;
                case 33 : terminal->cursor_styling.foreground = 3; break;
                case 34 : terminal->cursor_styling.foreground = 4; break;
                case 35 : terminal->cursor_styling.foreground = 5; break;
                case 36 : terminal->cursor_styling.foreground = 6; break;
                case 37 : terminal->cursor_styling.foreground = 7; break;
                case 38 : terminal->cursor_styling.foreground = 0; break;
                case 39 : state = DISPLAY_STATE_FOREGROUND_COLOR_MODE; break;
                case 40 : terminal->cursor_styling.background = 0; break;
                case 41 : terminal->cursor_styling.background = 1; break;
                case 42 : terminal->cursor_styling.background = 2; break;
                case 43 : terminal->cursor_styling.background = 3; break;
                case 44 : terminal->cursor_styling.background = 4; break;
                case 45 : terminal->cursor_styling.background = 5; break;
                case 46 : terminal->cursor_styling.background = 6; break;
                case 47 : terminal->cursor_styling.background = 7; break;
                case 48 : terminal->cursor_styling.background = 0; break;
                case 49 : state = DISPLAY_STATE_BACKGROUND_COLOR_VALUE; break;
                case 90 : terminal->cursor_styling.foreground = 251; break;
                case 91 : terminal->cursor_styling.foreground = 160; break;
                case 92 : terminal->cursor_styling.foreground = 119; break;
                case 93 : terminal->cursor_styling.foreground = 226; break;
                case 94 : terminal->cursor_styling.foreground = 81; break;
                case 95 : terminal->cursor_styling.foreground = 201; break;
                case 96 : terminal->cursor_styling.foreground = 51; break;
                case 97 : terminal->cursor_styling.foreground = 231; break;
                case 100: terminal->cursor_styling.background = 251; break;
                case 101: terminal->cursor_styling.background = 160; break;
                case 102: terminal->cursor_styling.background = 119; break;
                case 103: terminal->cursor_styling.background = 226; break;
                case 104: terminal->cursor_styling.background = 81; break;
                case 105: terminal->cursor_styling.background = 201; break;
                case 106: terminal->cursor_styling.background = 51; break;
                case 107: terminal->cursor_styling.background = 231; break;
              }
            break;
            case DISPLAY_STATE_FOREGROUND_COLOR_MODE: state = DISPLAY_STATE_FOREGROUND_COLOR_VALUE; break;
            case DISPLAY_STATE_BACKGROUND_COLOR_MODE: state = DISPLAY_STATE_BACKGROUND_COLOR_VALUE; break;
            case DISPLAY_STATE_FOREGROUND_COLOR_VALUE: terminal->cursor_styling.foreground = parse_number(&seq[offset], 0); break;
            case DISPLAY_STATE_BACKGROUND_COLOR_VALUE: terminal->cursor_styling.background = parse_number(&seq[offset], 0); break;
          }
          char* next = strchr(&seq[offset], ';');
          if (!next)
            break;
          offset = (next - seq) + 1;
        }
        return 0;
      } break;
      case 'h': {
        if (seq[2] == '?' && parse_number(&seq[3], 0) == 1049)
          terminal_switch_buffer(terminal, VIEW_ALTERNATE_BUFFER);
      } break;
      case 'l': {
        if (seq[2] == '?' && parse_number(&seq[3], 0) == 1049)
          terminal_switch_buffer(terminal, VIEW_NORMAL_BUFFER);
      } break;
      default:
        return -1;
      break;
    }
  }
  return 0;
}


static terminal_escape_type_e get_terminal_escape_type(char a) {
  switch (a) {
    case '[': return ESCAPE_TYPE_CSI;
    case ']': return ESCAPE_TYPE_OS;
  }
  return ESCAPE_TYPE_NONE;
}

static void terminal_output(terminal_t* terminal, const char* str, int len) {
  unsigned int codepoint;
  int offset = 0;
  int buffered_sequence_index = strlen(terminal->buffered_sequence);
  terminal_escape_type_e escape_type = ESCAPE_TYPE_NONE;
  if (buffered_sequence_index) {
    if (buffered_sequence_index == 1)
      escape_type = ESCAPE_TYPE_OPEN;
    else
      escape_type = get_terminal_escape_type(terminal->buffered_sequence[1]);
  }
  while (offset < len) {
    if (buffered_sequence_index) {
      terminal->buffered_sequence[buffered_sequence_index++] = str[offset];
      if (escape_type == ESCAPE_TYPE_OPEN)
        escape_type = get_terminal_escape_type(str[offset]);
      else if (
        (escape_type == ESCAPE_TYPE_CSI && str[offset] >= 0x40 && str[offset] <= 0x7E) ||
        (escape_type == ESCAPE_TYPE_OS && (str[offset] == '\0' || str[offset] == '\a'))
      ) {
        terminal->buffered_sequence[buffered_sequence_index++] = 0;
        terminal_escape_sequence(terminal, escape_type, terminal->buffered_sequence);
        buffered_sequence_index = 0;
        terminal->buffered_sequence[0] = 0;
        escape_type = ESCAPE_TYPE_NONE;
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
          if (terminal->cursor_x)
            --terminal->cursor_x;
        } break;
        case 0x09:
        break;
        case '\n': {
          terminal->cursor_x = 0;
          if (terminal->cursor_y < terminal->lines - 1)
            ++terminal->cursor_y;
          else
            terminal_shift_buffer(terminal);
        } break;
        case 0x0B:
        case 0x0C:
        break;
        case '\r': {
          terminal->cursor_x = 0;
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
          terminal->buffers[terminal->view][terminal->cursor_y * terminal->columns + terminal->cursor_x] = (buffer_char_t){ terminal->cursor_styling, codepoint };
          if (terminal->cursor_x++ > terminal->columns) {
            terminal->cursor_x = 0;
            if (terminal->cursor_y < terminal->lines - 1)
              ++terminal->cursor_y;
            else if (terminal->view == VIEW_ALTERNATE_BUFFER)
              terminal_shift_buffer(terminal);
          }
        break;
      }
    }
  }
}

static int terminal_update(terminal_t* terminal) {
  char chunk[LIBTERMINAL_CHUNK_SIZE];
  int len, at_least_one = 0;
  do {
    len = read(terminal->master, chunk, sizeof(chunk));
    if (len == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return at_least_one;
    }
    terminal_output(terminal, chunk, len);
    at_least_one = 1;
  } while (len > 0);
  return -1;
}

static void terminal_free(terminal_t* terminal) {
  terminal_clear_scrollback_buffer(terminal);
  for (int i = 0; i < VIEW_MAX; ++i) {
    if (terminal->buffers[i])
      free(terminal->buffers[i]);
  }
  free(terminal);
}

static void terminal_resize(terminal_t* terminal, int columns, int lines) {
  struct winsize size = { .ws_row = lines, .ws_col = columns, .ws_xpixel = 0, .ws_ypixel = 0 };
  ioctl(terminal->master, TIOCSWINSZ, &size);
  terminal->columns = columns;
  terminal->lines = lines;
  for (int i = 0; i < VIEW_MAX; ++i) {
    terminal->buffers[i] = realloc(terminal->buffers[i], sizeof(buffer_char_t) * terminal->columns * terminal->lines);
    memset(terminal->buffers[i], 0, sizeof(buffer_char_t) * terminal->columns * terminal->lines);
  }
}

static terminal_t* terminal_new(int columns, int lines, int scrollback_limit, const char* pathname, const char** argv) {
  terminal_t* terminal = calloc(sizeof(terminal_t), 1);
  struct termios term = {0};
  term.c_cc[VINTR] = 127;
  term.c_cc[VSUSP] = 26;
  term.c_lflag |= ECHO | ISIG;
  terminal->pid = forkpty(&terminal->master, NULL, &term, NULL);
  terminal->scrollback_limit = scrollback_limit;
  if (terminal->pid == -1) {
    free(terminal);
    return NULL;
  }
  if (!terminal->pid) {
    setenv("TERM", "xterm-256color", 1);
    setsid();
    execvp(pathname,  (char** const)argv);
    exit(-1);
    return NULL;
  }
  int flags = fcntl(terminal->master, F_GETFD, 0);
  fcntl(terminal->master, F_SETFL, flags | O_NONBLOCK);
  terminal_resize(terminal, columns, lines);
  return terminal;
}


static int output_line(lua_State* L, buffer_char_t* start, buffer_char_t* end) {
  lua_newtable(L);
  int block_size = 0;
  int group = 0;
  char text_buffer[LIBTERMINAL_MAX_LINE_WIDTH] = {0};
  buffer_styling_t style = start->styling;
  while (1) {
    if (start->styling.value != style.value || start >= end || !start->codepoint) {
      lua_pushinteger(L, style.foreground << 16 | style.background << 8 | style.attributes);
      lua_rawseti(L, -2, ++group);
      lua_pushlstring(L, text_buffer, block_size);
      lua_rawseti(L, -2, ++group);
      block_size = 0;
      style = start->styling;
      if (start >= end || !start->codepoint)
        break;
    }
    text_buffer[block_size++] = (char)start->codepoint;
    ++start;
  }
}


static int f_terminal_lines(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  if (terminal_update(terminal) == -1)
    return 0;
  lua_newtable(L);

  int total_lines = 0;
  int remaining_lines = terminal->lines;
  if (terminal->view == VIEW_NORMAL_BUFFER && terminal->scrollback_target) {
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
      output_line(L, &terminal->buffers[terminal->view][y * terminal->columns], &terminal->buffers[terminal->view][(y+1) * terminal->columns]);
      lua_rawseti(L, -2, ++total_lines);
    }
  }
  return 1;
}

static int f_terminal_input(lua_State* L) {
  size_t len;
  const char* str = luaL_checklstring(L, 2, &len);
  terminal_input(*(terminal_t**)lua_touserdata(L, 1), str, (int)len);
  return 0;
}


static int f_terminal_new(lua_State* L) {
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int scrollback_limit = luaL_checkinteger(L, 3);
  const char* path = luaL_checkstring(L, 4);
  char* arguments[256] = {0};
  if (lua_type(L, 4) == LUA_TTABLE) {
    for (int i = 0; i < 256; ++i) {
      lua_rawgeti(L, 4, i+1);
      if (!lua_isnil(L, -1)) {
        const char* str = luaL_checkstring(L, -1);
        arguments[i] = dup_string(str);
        lua_pop(L, 1);
      } else {
        lua_pop(L, 1);
        break;
      }
    }
  }
  terminal_t* terminal = terminal_new(x, y, scrollback_limit, path, (const char**)arguments);
  terminal_t** pointer = lua_newuserdata(L, sizeof(terminal_t*));
  *pointer = terminal;
  luaL_setmetatable(L, "libterminal");
  for (int i = 0; i < 256 && arguments[i]; ++i)
    free(arguments[i]);
  return 1;
}

static int f_terminal_gc(lua_State* L) {
  terminal_free(*(terminal_t**)lua_touserdata(L, 1));
}

static int f_terminal_update(lua_State* L){
  int status = terminal_update(*(terminal_t**)lua_touserdata(L, 1));
  if (status == -1)
    return luaL_error(L, "error updating terminal");
  lua_pushboolean(L, status > 0);
  return 1;
}

static int f_terminal_resize(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  int x = luaL_checkinteger(L, 2), y = luaL_checkinteger(L, 3);
  terminal_resize(terminal, x, y);
}


static int f_terminal_exited(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  int status;
  lua_pushboolean(L, waitpid(terminal->pid, &status, WNOHANG) <= 0);
  return 1;
}

static int f_terminal_cursor(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  lua_pushinteger(L, terminal->cursor_x);
  lua_pushinteger(L, terminal->cursor_y);
  return 2;
}

static int f_terminal_scrollback(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  if (lua_gettop(L) >= 2)
    terminal_scrollback(terminal, luaL_checkinteger(L, 2));
  lua_pushinteger(L, terminal->scrollback_position);
  return 1;
}


static const luaL_Reg terminal_api[] = {
  { "__gc",          f_terminal_gc         },
  { "new",           f_terminal_new        },
  { "input",         f_terminal_input      },
  { "lines",         f_terminal_lines      },
  { "resize",        f_terminal_resize     },
  { "update",        f_terminal_update     },
  { "exited",        f_terminal_exited     },
  { "cursor",        f_terminal_cursor     },
  { "scrollback",    f_terminal_scrollback },
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
