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
  unsigned int foreground;
  unsigned int background;
  unsigned char attributes;
} buffer_styling_t;

typedef struct buffer_char_t {
  buffer_styling_t styling;
  unsigned int codepoint;
} buffer_char_t;

typedef struct backbuffer_page_t {
  struct backbuffer_page_t* prev;
  unsigned int columns, lines;
  int offset;
  buffer_char_t buffer[];
} backbuffer_page_t;

typedef enum {
  VIEW_FRONT_BUFFER,
  VIEW_BACK_BUFFER,
  VIEW_SCROLLBACK_BUFFER
} EView;


typedef struct {
  backbuffer_page_t* scrollback_buffer;
  unsigned int total_scrollback_lines;
  unsigned int columns, lines;
  buffer_char_t* front_buffer;
  buffer_char_t* back_buffer;
  EView view;
  unsigned int cursor_x, cursor_y;
  buffer_styling_t cursor_styling;
  int master;
  pid_t pid;
  char buffered_sequence[16];
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
}

static void terminal_shift_front_buffer(terminal_t* terminal) {

}

static int parse_number(const char* seq, int def) {
  if (seq[0] >= '0' || seq[0] <= '9')
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
  buffer_char_t* buffer = terminal->front_buffer;
  if (type == ESCAPE_TYPE_CSI) {
    int seq_end = strlen(seq) - 1;
    switch (seq[seq_end]) {
      case 'A': terminal->cursor_y = max(terminal->cursor_y - parse_number(&seq[2], 1), 0);     break;
      case 'B': terminal->cursor_y = min(terminal->cursor_y + parse_number(&seq[2], 1), terminal->lines - 1); break;
      case 'C': terminal->cursor_x = min(terminal->cursor_x + parse_number(&seq[2], 1), terminal->columns - 1); break;
      case 'D': terminal->cursor_y = max(terminal->cursor_x - parse_number(&seq[2], 1), 0); break;
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
          terminal->cursor_y = parse_number(&seq[semicolon+1], 1);
        }
      break;
      case 'J':  {
        switch (seq[2]) {
          case '1':
            for (int y = 0; y < terminal->cursor_y; ++y) {
              int w = y == terminal->cursor_y - 1 ? terminal->cursor_x : terminal->columns;
              memset(&buffer[terminal->columns * y], 0, sizeof(buffer_char_t) * w);
            }
          break;
          case '3':
            terminal_clear_scrollback_buffer(terminal);
            // intentional fallthrough
          case '2':
            memset(buffer, 0, sizeof(buffer_char_t) * (terminal->columns * terminal->lines));
            terminal->cursor_x = 0;
            terminal->cursor_y = 0;
          break;
          default:
            for (int y = terminal->cursor_y; y < terminal->lines; ++y) {
              int x = y == terminal->cursor_y ? terminal->cursor_x : 0;
              memset(&buffer[terminal->columns * y + x], 0, sizeof(buffer_char_t) * (terminal->columns - x));
            }
          break;
        }
      } break;
      case 'K': {
        switch (seq[2]) {
          case '1': memset(&buffer[terminal->cursor_y * terminal->columns], 0, sizeof(buffer_char_t) * terminal->cursor_x);     break;
          case '2': memset(&buffer[terminal->cursor_y * terminal->columns], 0, sizeof(buffer_char_t) * terminal->columns);      break;
          default: memset(&buffer[terminal->cursor_y * terminal->columns + terminal->cursor_x], 0, sizeof(buffer_char_t) * (terminal->columns - terminal->cursor_x));  break;
        }
      } break;
      case 'm': {
        return 0;
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
    fprintf(stderr, "CHR: %d (%c) %d %d\n", str[offset], str[offset], terminal->cursor_x, terminal->cursor_y);
    if (buffered_sequence_index) {
      terminal->buffered_sequence[buffered_sequence_index++] = str[offset];
      if (escape_type == ESCAPE_TYPE_OPEN)
        escape_type = get_terminal_escape_type(str[offset]);
      else if (str[offset] >= 0x40 && str[offset] <= 0x7E) {
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
        case '\b': {
          if (terminal->cursor_x)
            --terminal->cursor_x;
        } break;
        case 0x09:
        case '\n': {
          terminal->cursor_x = 0;
          if (terminal->cursor_y < terminal->lines)
            ++terminal->cursor_y;
          else
            terminal_shift_front_buffer(terminal);
          fprintf(stderr, "SHIFT\n");
        } break;
        case 0x0B:
        case 0x0C:
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
          terminal->front_buffer[terminal->cursor_y * terminal->columns + terminal->cursor_x] = (buffer_char_t){ terminal->cursor_styling, codepoint };
          if (terminal->cursor_x++ > terminal->columns) {
            terminal->cursor_x = 0;
            if (terminal->cursor_y < terminal->lines)
              ++terminal->cursor_y;
            else
              terminal_shift_front_buffer(terminal);
          }
        break;
      }
    }
  }
}

static int terminal_update(terminal_t* terminal) {
  char chunk[4096];
  int len;
  do {
    len = read(terminal->master, chunk, sizeof(chunk));
    if (len == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
      return 0;
    terminal_output(terminal, chunk, len);
  } while (len > 0);
  return -1;
}

static void terminal_free(terminal_t* terminal) {
  terminal_clear_scrollback_buffer(terminal);
  if (terminal->front_buffer)
    free(terminal->front_buffer);
  if (terminal->back_buffer)
    free(terminal->back_buffer);
  free(terminal);
}

static void terminal_resize(terminal_t* terminal, int columns, int lines) {
  struct winsize size = { .ws_row = lines, .ws_col = columns, .ws_xpixel = 0, .ws_ypixel = 0 };
  ioctl(terminal->master, TIOCSWINSZ, &size);
  terminal->columns = columns;
  terminal->lines = lines;
  terminal->front_buffer = realloc(terminal->front_buffer, sizeof(buffer_char_t) * terminal->columns * terminal->lines);
  terminal->back_buffer = realloc(terminal->back_buffer, sizeof(buffer_char_t) * terminal->columns * terminal->lines);
}

static terminal_t* terminal_new(int columns, int lines, const char* pathname, const char** argv) {
  terminal_t* terminal = calloc(sizeof(terminal_t), 1);
  struct termios term = {0};
  term.c_lflag |= ECHO;
  terminal->pid = forkpty(&terminal->master, NULL, &term, NULL);
  if (terminal->pid == -1) {
    free(terminal);
    return NULL;
  }
  if (!terminal->pid) {
    setenv("TERM", "ansi", 1);
    execvp(pathname,  (char** const)argv);
    exit(-1);
    return NULL;
  }
  int flags = fcntl(terminal->master, F_GETFD, 0);
  fcntl(terminal->master, F_SETFL, flags | O_NONBLOCK);
  terminal_resize(terminal, columns, lines);
  return terminal;
}


static int f_terminal_lines(lua_State* L) {
  terminal_t* terminal = *(terminal_t**)lua_touserdata(L, 1);
  sleep(1);
  buffer_char_t* buffer = terminal->view == VIEW_FRONT_BUFFER ? terminal->front_buffer : terminal->back_buffer;
  if (terminal_update(terminal))
    return 0;
  lua_newtable(L);
  switch (terminal->view) {
    case VIEW_FRONT_BUFFER:
    case VIEW_BACK_BUFFER:
      int start_offset = 0;
      buffer_styling_t styling = {0};
      char text_buffer[1024] = {0};
      for (int y = 0; y < terminal->lines; ++y) {
        lua_newtable(L);
        text_buffer[0] = 0;
        int x;
        for (x = 0; x < terminal->columns && buffer[y * terminal->columns + x].codepoint; ++x)
          text_buffer[x] = (char)buffer[y * terminal->columns + x].codepoint;
        text_buffer[x] = 0;
        lua_pushstring(L, text_buffer);
        lua_setfield(L, -2, "text");
        lua_rawseti(L, -2, y + 1);
      }
    break;
    case VIEW_SCROLLBACK_BUFFER:

    break;
  }
  return 1;
}

static int f_terminal_input(lua_State* L) {
  size_t len;
  sleep(1);
  const char* str = luaL_checklstring(L, 2, &len);
  terminal_input(*(terminal_t**)lua_touserdata(L, 1), str, (int)len);
  return 0;
}


static int f_terminal_new(lua_State* L) {
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  const char* path = luaL_checkstring(L, 3);
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
  terminal_t* terminal = terminal_new(x, y, path, (const char**)arguments);
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
  terminal_update(*(terminal_t**)lua_touserdata(L, 1));
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

static const luaL_Reg terminal_api[] = {
  { "__gc",          f_terminal_gc       },
  { "new",           f_terminal_new      },
  { "input",         f_terminal_input    },
  { "lines",         f_terminal_lines    },
  { "resize",        f_terminal_resize   },
  { "update",        f_terminal_update   },
  { "exited",        f_terminal_exited   },
  { NULL,            NULL                }
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
