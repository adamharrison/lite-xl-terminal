-- mod-version:3
local core = require "core"
local config = require "core.config"
local command = require "core.command"
local style = require "core.style"
local common = require "core.common"
local View = require "core.view"
local keymap = require "core.keymap"
local StatusView = require "core.statusview"

local terminal_native = require "plugins.terminal.libterminal"


local default_shell = os.getenv("SHELL") or (PLATFORM == "Windows" and "c:\\windows\\system32\\cmd.exe" or "sh")
config.plugins.terminal = common.merge({
  debug = false,
  term = "xterm-256color",
  shell = default_shell,
  -- We set ICRNL, so that this is translated to `\n` at input time... but this seems to be necessary. `micro`
  -- doesn't allow you to newline if you don't set it to `\r`.
  newline = ((config.plugins.terminal.shell or default_shell):find("cmd.exe") and "\r\n" or "\r"),
  backspace = "\x7F",
  delete = "\x1B[3~",
  arguments = { },
  scrollback_limit = 10000,
  height = 300,
  font = style.code_font,
  padding = { x = 0, y = 0 },
  background = { common.color "#000000" },
  text = { common.color "#FFFFFF" },
  colors = {
    [  0] = { common.color "#000000" }, [  1] = { common.color "#800000" }, [  2] = { common.color "#008000" }, [  3] = { common.color "#808000" }, [  4] = { common.color "#000080" },
    [  5] = { common.color "#800080" }, [  6] = { common.color "#008080" }, [  7] = { common.color "#c0c0c0" }, [  8] = { common.color "#808080" }, [  9] = { common.color "#ff0000" },
    [ 10] = { common.color "#00ff00" }, [ 11] = { common.color "#ffff00" }, [ 12] = { common.color "#0000ff" }, [ 13] = { common.color "#ff00ff" }, [ 14] = { common.color "#00ffff" },
    [ 15] = { common.color "#ffffff" }, [ 16] = { common.color "#000000" }, [ 17] = { common.color "#00005f" }, [ 18] = { common.color "#000087" }, [ 19] = { common.color "#0000af" },
    [ 20] = { common.color "#0000d7" }, [ 21] = { common.color "#0000ff" }, [ 22] = { common.color "#005f00" }, [ 23] = { common.color "#005f5f" }, [ 24] = { common.color "#005f87" },
    [ 25] = { common.color "#005faf" }, [ 26] = { common.color "#005fd7" }, [ 27] = { common.color "#005fff" }, [ 28] = { common.color "#008700" }, [ 29] = { common.color "#00875f" },
    [ 30] = { common.color "#008787" }, [ 31] = { common.color "#0087af" }, [ 32] = { common.color "#0087d7" }, [ 33] = { common.color "#0087ff" }, [ 34] = { common.color "#00af00" },
    [ 35] = { common.color "#00af5f" }, [ 36] = { common.color "#00af87" }, [ 37] = { common.color "#00afaf" }, [ 38] = { common.color "#00afd7" }, [ 39] = { common.color "#00afff" },
    [ 40] = { common.color "#00d700" }, [ 41] = { common.color "#00d75f" }, [ 42] = { common.color "#00d787" }, [ 43] = { common.color "#00d7af" }, [ 44] = { common.color "#00d7d7" },
    [ 45] = { common.color "#00d7ff" }, [ 46] = { common.color "#00ff00" }, [ 47] = { common.color "#00ff5f" }, [ 48] = { common.color "#00ff87" }, [ 49] = { common.color "#00ffaf" },
    [ 50] = { common.color "#00ffd7" }, [ 51] = { common.color "#00ffff" }, [ 52] = { common.color "#5f0000" }, [ 53] = { common.color "#5f005f" }, [ 54] = { common.color "#5f0087" },
    [ 55] = { common.color "#5f00af" }, [ 56] = { common.color "#5f00d7" }, [ 57] = { common.color "#5f00ff" }, [ 58] = { common.color "#5f5f00" }, [ 59] = { common.color "#5f5f5f" },
    [ 60] = { common.color "#5f5f87" }, [ 61] = { common.color "#5f5faf" }, [ 62] = { common.color "#5f5fd7" }, [ 63] = { common.color "#5f5fff" }, [ 64] = { common.color "#5f8700" },
    [ 65] = { common.color "#5f875f" }, [ 66] = { common.color "#5f8787" }, [ 67] = { common.color "#5f87af" }, [ 68] = { common.color "#5f87d7" }, [ 69] = { common.color "#5f87ff" },
    [ 70] = { common.color "#5faf00" }, [ 71] = { common.color "#5faf5f" }, [ 72] = { common.color "#5faf87" }, [ 73] = { common.color "#5fafaf" }, [ 74] = { common.color "#5fafd7" },
    [ 75] = { common.color "#5fafff" }, [ 76] = { common.color "#5fd700" }, [ 77] = { common.color "#5fd75f" }, [ 78] = { common.color "#5fd787" }, [ 79] = { common.color "#5fd7af" },
    [ 80] = { common.color "#5fd7d7" }, [ 81] = { common.color "#5fd7ff" }, [ 82] = { common.color "#5fff00" }, [ 83] = { common.color "#5fff5f" }, [ 84] = { common.color "#5fff87" },
    [ 85] = { common.color "#5fffaf" }, [ 86] = { common.color "#5fffd7" }, [ 87] = { common.color "#5fffff" }, [ 88] = { common.color "#870000" }, [ 89] = { common.color "#87005f" },
    [ 90] = { common.color "#870087" }, [ 91] = { common.color "#8700af" }, [ 92] = { common.color "#8700d7" }, [ 93] = { common.color "#8700ff" }, [ 94] = { common.color "#875f00" },
    [ 95] = { common.color "#875f5f" }, [ 96] = { common.color "#875f87" }, [ 97] = { common.color "#875faf" }, [ 98] = { common.color "#875fd7" }, [ 99] = { common.color "#875fff" },
    [100] = { common.color "#878700" }, [101] = { common.color "#87875f" }, [102] = { common.color "#878787" }, [103] = { common.color "#8787af" }, [104] = { common.color "#8787d7" },
    [105] = { common.color "#8787ff" }, [106] = { common.color "#87af00" }, [107] = { common.color "#87af5f" }, [108] = { common.color "#87af87" }, [109] = { common.color "#87afaf" },
    [110] = { common.color "#87afd7" }, [111] = { common.color "#87afff" }, [112] = { common.color "#87d700" }, [113] = { common.color "#87d75f" }, [114] = { common.color "#87d787" },
    [115] = { common.color "#87d7af" }, [116] = { common.color "#87d7d7" }, [117] = { common.color "#87d7ff" }, [118] = { common.color "#87ff00" }, [119] = { common.color "#87ff5f" },
    [120] = { common.color "#87ff87" }, [121] = { common.color "#87ffaf" }, [122] = { common.color "#87ffd7" }, [123] = { common.color "#87ffff" }, [124] = { common.color "#af0000" },
    [125] = { common.color "#af005f" }, [126] = { common.color "#af0087" }, [127] = { common.color "#af00af" }, [128] = { common.color "#af00d7" }, [129] = { common.color "#af00ff" },
    [130] = { common.color "#af5f00" }, [131] = { common.color "#af5f5f" }, [132] = { common.color "#af5f87" }, [133] = { common.color "#af5faf" }, [134] = { common.color "#af5fd7" },
    [135] = { common.color "#af5fff" }, [136] = { common.color "#af8700" }, [137] = { common.color "#af875f" }, [138] = { common.color "#af8787" }, [139] = { common.color "#af87af" },
    [140] = { common.color "#af87d7" }, [141] = { common.color "#af87ff" }, [142] = { common.color "#afaf00" }, [143] = { common.color "#afaf5f" }, [144] = { common.color "#afaf87" },
    [145] = { common.color "#afafaf" }, [146] = { common.color "#afafd7" }, [147] = { common.color "#afafff" }, [148] = { common.color "#afd700" }, [149] = { common.color "#afd75f" },
    [150] = { common.color "#afd787" }, [151] = { common.color "#afd7af" }, [152] = { common.color "#afd7d7" }, [153] = { common.color "#afd7ff" }, [154] = { common.color "#afff00" },
    [155] = { common.color "#afff5f" }, [156] = { common.color "#afff87" }, [157] = { common.color "#afffaf" }, [158] = { common.color "#afffd7" }, [159] = { common.color "#afffff" },
    [160] = { common.color "#d70000" }, [161] = { common.color "#d7005f" }, [162] = { common.color "#d70087" }, [163] = { common.color "#d700af" }, [164] = { common.color "#d700d7" },
    [165] = { common.color "#d700ff" }, [166] = { common.color "#d75f00" }, [167] = { common.color "#d75f5f" }, [168] = { common.color "#d75f87" }, [169] = { common.color "#d75faf" },
    [170] = { common.color "#d75fd7" }, [171] = { common.color "#d75fff" }, [172] = { common.color "#d78700" }, [173] = { common.color "#d7875f" }, [174] = { common.color "#d78787" },
    [175] = { common.color "#d787af" }, [176] = { common.color "#d787d7" }, [177] = { common.color "#d787ff" }, [178] = { common.color "#d7af00" }, [179] = { common.color "#d7af5f" },
    [180] = { common.color "#d7af87" }, [181] = { common.color "#d7afaf" }, [182] = { common.color "#d7afd7" }, [183] = { common.color "#d7afff" }, [184] = { common.color "#d7d700" },
    [185] = { common.color "#d7d75f" }, [186] = { common.color "#d7d787" }, [187] = { common.color "#d7d7af" }, [188] = { common.color "#d7d7d7" }, [189] = { common.color "#d7d7ff" },
    [190] = { common.color "#d7ff00" }, [191] = { common.color "#d7ff5f" }, [192] = { common.color "#d7ff87" }, [193] = { common.color "#d7ffaf" }, [194] = { common.color "#d7ffd7" },
    [195] = { common.color "#d7ffff" }, [196] = { common.color "#ff0000" }, [197] = { common.color "#ff005f" }, [198] = { common.color "#ff0087" }, [199] = { common.color "#ff00af" },
    [200] = { common.color "#ff00d7" }, [201] = { common.color "#ff00ff" }, [202] = { common.color "#ff5f00" }, [203] = { common.color "#ff5f5f" }, [204] = { common.color "#ff5f87" },
    [205] = { common.color "#ff5faf" }, [206] = { common.color "#ff5fd7" }, [207] = { common.color "#ff5fff" }, [208] = { common.color "#ff8700" }, [209] = { common.color "#ff875f" },
    [210] = { common.color "#ff8787" }, [211] = { common.color "#ff87af" }, [212] = { common.color "#ff87d7" }, [213] = { common.color "#ff87ff" }, [214] = { common.color "#ffaf00" },
    [215] = { common.color "#ffaf5f" }, [216] = { common.color "#ffaf87" }, [217] = { common.color "#ffafaf" }, [218] = { common.color "#ffafd7" }, [219] = { common.color "#ffafff" },
    [220] = { common.color "#ffd700" }, [221] = { common.color "#ffd75f" }, [222] = { common.color "#ffd787" }, [223] = { common.color "#ffd7af" }, [224] = { common.color "#ffd7d7" },
    [225] = { common.color "#ffd7ff" }, [226] = { common.color "#ffff00" }, [227] = { common.color "#ffff5f" }, [228] = { common.color "#ffff87" }, [229] = { common.color "#ffffaf" },
    [230] = { common.color "#ffffd7" }, [231] = { common.color "#ffffff" }, [232] = { common.color "#080808" }, [233] = { common.color "#121212" }, [234] = { common.color "#1c1c1c" },
    [235] = { common.color "#262626" }, [236] = { common.color "#303030" }, [237] = { common.color "#3a3a3a" }, [238] = { common.color "#444444" }, [239] = { common.color "#4e4e4e" },
    [240] = { common.color "#585858" }, [241] = { common.color "#626262" }, [242] = { common.color "#6c6c6c" }, [243] = { common.color "#767676" }, [244] = { common.color "#808080" },
    [245] = { common.color "#8a8a8a" }, [246] = { common.color "#949494" }, [247] = { common.color "#9e9e9e" }, [248] = { common.color "#a8a8a8" }, [249] = { common.color "#b2b2b2" },
    [250] = { common.color "#bcbcbc" }, [251] = { common.color "#c6c6c6" }, [252] = { common.color "#d0d0d0" }, [253] = { common.color "#dadada" }, [254] = { common.color "#e4e4e4" },
    [255] = { common.color "#eeeeee" }
  }
}, config.plugins.terminal)
if not config.plugins.terminal.bold_font then config.plugins.terminal.bold_font = style.code_font:copy(style.code_font:get_size(), { smoothing = true }) end


local TerminalView = View:extend()
local view

function TerminalView:get_name() return self.terminal and self.terminal:name() or "Terminal" end
function TerminalView:supports_text_input() return true end

function TerminalView:new(options)
  TerminalView.super.new(self)
  self.size.y = options.height
  self.options = options
  self.last_size = { x = self.size.x, y = self.size.y }
  self.focused = false
end

function TerminalView:spawn()
  self.terminal = terminal_native.new(self.columns, self.lines, self.options.scrollback_limit, self.options.term, self.options.shell, self.options.arguments, self.options.debug)
  self.routine = self.routine or core.add_thread(function()
    while self.terminal do
      if self.terminal:update() then
        core.redraw = true
      end
      coroutine.yield(1 / config.fps)
    end
  end)
end

function TerminalView:update()
  if not self.terminal or self.last_size.x ~= self.size.x or self.last_size.y ~= self.size.y then
    self.columns = math.max(math.floor((self.size.x - self.options.padding.x*2) / self.options.font:get_width("W")), 1)
    self.lines = math.max(math.floor((self.size.y - self.options.padding.y*2) / self.options.font:get_height()), 1)
    if self.lines > 0 and self.columns > 0 then
      if not self.terminal then
        self:spawn()
      else
        self.terminal:size(self.columns, self.lines)
        self.last_size = { x = self.size.x, y = self.size.y }
      end
    end
  end
  if self.terminal then
    if not self.terminal:exited() then
      if (core.active_view == self and not self.focused) or (core.active_view ~= self and self.focused) then
        self.focused = core.active_view == self
        self.terminal:focused(self.focused)
      end
      local x, y, mode = self.terminal:cursor()
      if mode == "blinking" then
        local T = config.blink_period
        local ta, tb = core.blink_timer, system.get_time()
        if ((tb - t0) % T < T / 2) ~= ((ta - t0) % T < T / 2) then
          core.redraw = true
        end
        core.blink_timer = tb
      end
    else
      command.perform("terminal:toggle")
      self.terminal = nil
      self.routine = nil
    end
  end
  TerminalView.super.update(self)
end


function TerminalView:set_target_size(axis, value)
  if axis == "y" then
    self.size.y = value
    return true
  end
end

function TerminalView:convert_color(int, target)
  local attributes = int >> 24
  local type = (attributes & 0x7)
  if type == 0 then
    if target == "foreground" then return self.options.text, attributes end
    return self.options.background, attributes
  elseif type == 1 then
    if target == "foreground" then return self.options.background, attributes end
    return self.options.text, attributes
  elseif type == 2 then
    local index = (int >> 16) & 0xFF
    return self.options.colors[index], attributes
  elseif type == 3 then
    return { ((int >> 16) & 0xFF), ((int >> 8) & 0xFF), ((int >> 0) & 0xFF), 255 }, attributes
  end
  return nil
end

function TerminalView:draw()
  TerminalView.super.draw_background(self, self.options.background)
  if self.terminal then
    local cursor_x, cursor_y, mode = self.terminal:cursor()
    local space_width = self.options.font:get_width(" ")

    local y = self.position.y + self.options.padding.y
    local lh = self.options.font:get_height()
    core.redraw = self.terminal:update() or core.redraw
    for line_idx, line in ipairs(self.terminal:lines()) do
      local x = self.position.x + self.options.padding.x
      local should_draw_cursor = false
      if mode ~= "hidden" and core.active_view == self and line_idx - 1 == cursor_y and self.terminal:scrollback() == 0 then
        if mode == "blinking" then
          local T = config.blink_period
          if (core.blink_timer - core.blink_start) % T < T / 2 then
            should_draw_cursor = true
          end
        else
          should_draw_cursor = true
        end
      end
      local offset = 0
      for i = 1, #line, 2 do
        local background, style = self:convert_color(line[i] & 0xFFFFFFFF, "background")
        local text = line[i+1]
        local font = (((style >> 3) & 0x1) ~= 0) and self.options.bold_font or self.options.font
        local width = font:get_width(text)
        if background and background ~= self.options.background then
          renderer.draw_rect(x, y, width, lh, background)
        end
        x = x + width
      end
      x = self.position.x + self.options.padding.x
      if should_draw_cursor then
        renderer.draw_rect(x + cursor_x * space_width, y, space_width, lh, style.accent)
      end
      local idx = line_idx - 1
      if self.selection then
        local terminal_width = self.size.x - self.options.padding.x * 2
        local sorted = { table.unpack(self.selection) }
        if sorted[1] > sorted[3] or (sorted[1] == sorted[3] and sorted[2] > sorted[4]) then
          sorted = { sorted[3], sorted[4], sorted[1], sorted[2] }
        end
        if idx == sorted[2] and idx == sorted[4] then
          renderer.draw_rect(x + sorted[1] * space_width, y, (sorted[3] - sorted[1]) * space_width, lh, style.accent)
        elseif idx == sorted[2] then
          renderer.draw_rect(x + sorted[1] * space_width, y, terminal_width - (sorted[1] * space_width), lh, style.accent)
        elseif idx > sorted[2] and idx < sorted[4] then
          renderer.draw_rect(x, y, terminal_width, lh, style.accent)
        elseif idx == sorted[4] then
          renderer.draw_rect(x, y, space_width * sorted[3], lh, style.accent)
        end
      end
      for i = 1, #line, 2 do
        local foreground, style = self:convert_color(line[i] >> 32, "foreground")
        local text = line[i+1]
        local font = (((style >> 3) & 0x1) ~= 0) and self.options.bold_font or self.options.font
        local length = text:ulen()
        x = renderer.draw_text(font, text, x, y, foreground)
        offset = offset + length
      end
      y = y + lh
    end
  end
end

function TerminalView:convert_coordinates(x, y)
  return math.max(math.floor((x - self.position.x - self.options.padding.x) / self.options.font:get_width(" ")), 0), math.max(math.floor((y - self.position.y - self.options.padding.y) / self.options.font:get_height()), 0)
end

function TerminalView:on_mouse_pressed(button, x, y, clicks)
  if button == "left" then
    local col, row = self:convert_coordinates(x, y)
    if self.terminal:mouse_tracking_mode() == "x10" then
      self.terminal:input("\x1B[M" .. string.char(32) .. string.char(32 + col + 1) .. string.char(32 + row + 1) )
    elseif self.terminal:mouse_tracking_mode() == "normal" then
      self.terminal:input("\x1B[M" .. string.char(32) .. string.char(32 + col + 1) .. string.char(32 + row + 1) )
    elseif self.terminal:mouse_tracking_mode() == "sgr" then
      self.terminal:input("\x1B[<0;" .. (col+1) .. ";" .. (row+1) .. "M" )
    else
      if clicks == 1 then
        self.selection = nil
        self.pressing = true
      elseif clicks == 2 then
        for line_idx, line in ipairs(self.terminal:lines()) do
          if line_idx == row + 1 then
            local text = ""
            for i = 1, #line, 2 do
              text = text .. line[i+1]
            end
            local next_space = text:find("%s", col) or #text
            local last_space = 0
            local idx = text:reverse():find("%s", #text - col)
            if idx then
              last_space = (#text - idx) + 1
            end
            self.selection = { last_space, row, next_space - 1, row }
          end
        end
      elseif clicks == 3 then
        self.selection = { 0, row, 0, row + 1 }
      end
    end
  end
end

function TerminalView:on_mouse_moved(x, y)
  self.mouse_x = x
  self.mouse_y = y
  if self.pressing then
    local col, line = self:convert_coordinates(x, y)
    if not self.selection then self.selection = { col, line } end
    self.selection[3] = col
    self.selection[4] = line
  end
end


function TerminalView:on_mouse_released(button, x, y)
  if button == "left" then
    self.pressing = false
    local col, row = self:convert_coordinates(x, y)
    if self.terminal:mouse_tracking_mode() == "normal" then
      self.terminal:input("\x1B[M" .. string.char(32 + 3) .. string.char(32 + col + 1) .. string.char(32 + row + 1) )
    elseif self.terminal:mouse_tracking_mode() == "sgr" then
      self.terminal:input("\x1B[<0;" .. (col+1) .. ";" .. (row+1) .. "m")
    end
  end
end

function TerminalView:on_text_input(text)
  if self.terminal then
    self.terminal:input(text)
    if self.terminal:scrollback() ~= 0 then self.terminal:scrollback(0) end
    self.terminal:update()
    core.redraw = true
    return true
  end
end

command.add(function()
  local view = core.root_view.overlapping_view or core.active_view
  return view and view:is(TerminalView)
end, {
  ["terminal:scroll"] = function(amount, ...)
    local tv = core.root_view.overlapping_view or core.active_view
    if tv.terminal:mouse_tracking_mode() then
      local col, row = tv:convert_coordinates(tv.mouse_x, tv.mouse_y)
      if tv.terminal:mouse_tracking_mode() == "normal" then
        tv.terminal:input("\x1B[M" .. string.char(amount > 0 and 64 or 65) .. string.char(32 + col + 1) .. string.char(32 + row + 1) )
      elseif tv.terminal:mouse_tracking_mode() == "sgr" then
        tv.terminal:input("\x1B[<" .. (amount > 0 and 64 or 65) .. ";" .. (col+1) .. ";" .. (row+1) .. "M" )
      end
    else
      tv.terminal:scrollback(tv.terminal:scrollback() + (amount or 1))
    end
  end
})
command.add(function()
  return core.active_view:is(TerminalView) and core.active_view.terminal
end, {
  ["terminal:backspace"] = function() core.active_view.terminal:input(core.active_view.options.backspace) end,
  ["terminal:ctrl-backspace"] = function() core.active_view.terminal:input(core.active_view.options.backspace == "\b" and "\x7F" or "\b") end,
  ["terminal:alt-backspace"] = function() core.active_view.terminal:input("\x1B" .. core.active_view.options.backspace) end,
  ["terminal:insert"] = function() core.active_view.terminal:input("\x1B[2~") end,
  ["terminal:delete"] = function() core.active_view.terminal:input(core.active_view.options.delete) end,
  ["terminal:return"] = function() core.active_view.terminal:input(core.active_view.terminal:cursor_keys_mode() == "application" and "\x1BOM" or core.active_view.options.newline) end,
  ["terminal:break"] = function() core.active_view.terminal:input("\x03") end,
  ["terminal:eof"] = function() core.active_view.terminal:input("\x04") end,
  ["terminal:suspend"] = function() core.active_view.terminal:input("\x1A") end,
  ["terminal:tab"] = function() core.active_view.terminal:input("\t") end,
  ["terminal:paste"] = function()
    if core.active_view.terminal:paste_mode() == "bracketed" then
      core.active_view.terminal:input("\x1B[200~" .. system.get_clipboard() .. "\x1B[201~")
    else
      core.active_view.terminal:input(system.get_clipboard())
    end
  end,
  ["terminal:page-up"] = function() core.active_view.terminal:input("\x1B[5~") end,
  ["terminal:page-down"] = function() core.active_view.terminal:input("\x1B[6~") end,
  ["terminal:scroll-up"] = function() view.terminal:scrollback(core.active_view.terminal:scrollback() + core.active_view.lines) end,
  ["terminal:scroll-down"] = function() view.terminal:scrollback(core.active_view.terminal:scrollback() - core.active_view.lines) end,
  ["terminal:scroll-to-end"] = function() view.terminal:scrollback(0) end,
  ["terminal:scroll-to-top"] = function() view.terminal:scrollback(core.active_view.options.scrollback_limit) end,
  ["terminal:up"] = function() core.active_view.terminal:input(core.active_view.terminal:cursor_keys_mode() == "application" and "\x1BOA" or "\x1B[A") end,
  ["terminal:down"] = function() core.active_view.terminal:input(core.active_view.terminal:cursor_keys_mode() == "application" and "\x1BOB" or "\x1B[B") end,
  ["terminal:left"] = function() core.active_view.terminal:input(core.active_view.terminal:cursor_keys_mode() == "application" and "\x1BOD" or "\x1B[D") end,
  ["terminal:right"] = function() core.active_view.terminal:input(core.active_view.terminal:cursor_keys_mode() == "application" and "\x1BOC" or "\x1B[C") end,
  ["terminal:jump-up"] = function() core.active_view.terminal:input("\x1B[1;5A") end,
  ["terminal:jump-down"] = function() core.active_view.terminal:input("\x1B[1;5B") end,
  ["terminal:jump-right"] = function() core.active_view.terminal:input("\x1B[1;5C") end,
  ["terminal:jump-left"] = function() core.active_view.terminal:input("\x1B[1;5D") end,
  ["terminal:home"] = function() core.active_view.terminal:input(core.active_view.terminal:cursor_keys_mode() == "application" and "\x1BOH" or "\x1B[H") end,
  ["terminal:end"] = function() core.active_view.terminal:input(core.active_view.terminal:cursor_keys_mode() == "application" and "\x1BOF" or "\x1B[F") end,
  ["terminal:f1"]  = function() core.active_view.terminal:input("\x1BOP") end,
  ["terminal:f2"]  = function() core.active_view.terminal:input("\x1BOQ") end,
  ["terminal:f3"]  = function() core.active_view.terminal:input("\x1BOR") end,
  ["terminal:f4"]  = function() core.active_view.terminal:input("\x1BOS") end,
  ["terminal:f5"]  = function() core.active_view.terminal:input("\x1B[15~") end,
  ["terminal:f6"]  = function() core.active_view.terminal:input("\x1B[17~") end,
  ["terminal:f7"]  = function() core.active_view.terminal:input("\x1B[18~") end,
  ["terminal:f8"]  = function() core.active_view.terminal:input("\x1B[19~") end,
  ["terminal:f9"]  = function() core.active_view.terminal:input("\x1B[20~") end,
  ["terminal:f10"]  = function() core.active_view.terminal:input("\x1B[21~") end,
  ["terminal:f11"]  = function() core.active_view.terminal:input("\x1B[23~") end,
  ["terminal:f12"]  = function() core.active_view.terminal:input("\x1B[24~") end,
  ["terminal:escape"]  = function() core.active_view.terminal:input("\x1B") end,
  ["terminal:start"] = function() core.active_view.terminal:input("\x13") end,
  ["terminal:stop"] = function() core.active_view.terminal:input("\x11") end,
  ["terminal:cancel"] = function() core.active_view.terminal:input("\x18") end,
  ["terminal:start-of-heading"] = function() core.active_view.terminal:input("\x01") end,
  ["terminal:start-of-text"] = function() core.active_view.terminal:input("\x02") end,
  ["terminal:enquiry"] = function() core.active_view.terminal:input("\x05") end,
  ["terminal:acknowledge"] = function() core.active_view.terminal:input("\x06") end,
  ["terminal:bell"] = function() core.active_view.terminal:input("\x07") end,
  ["terminal:vertical-tab"] = function() core.active_view.terminal:input("\x0B") end,
  ["terminal:redraw"] = function() core.active_view.terminal:input("\0C") end,
  ["terminal:carriage-feed"] = function() core.active_view.terminal:input("\x0D") end,
  ["terminal:shift-out"] = function() core.active_view.terminal:input("\x0E") end,
  ["terminal:shift-in"] = function() core.active_view.terminal:input("\x0F") end,
  ["terminal:data-line-escape"] = function() core.active_view.terminal:input("\x10") end,
  ["terminal:history"] = function() core.active_view.terminal:input("\x12") end,
  ["terminal:transpose"] = function() core.active_view.terminal:input("\x14") end,
  ["terminal:negative-acknowledge"] = function() core.active_view.terminal:input("\x15") end,
  ["terminal:synchronous-idel"] = function() core.active_view.terminal:input("\x16") end,
  ["terminal:end-of-transmission-block"] = function() core.active_view.terminal:input("\x17") end,
  ["terminal:end-of-medium"] = function() core.active_view.terminal:input("\x19") end,
  ["terminal:file-separator"] = function() core.active_view.terminal:input("\x1C") end,
  ["terminal:group-separator"] = function() core.active_view.terminal:input("\x1D") end
});

command.add(function()
  return core.active_view and core.active_view:is(TerminalView) and core.active_view.selection and #core.active_view.selection == 4
end, {
  ["terminal:copy"] = function()
    local tv = core.active_view
    local full_buffer = {}
    for line_idx, line in ipairs(tv.terminal:lines()) do
      local idx = line_idx - 1
      local sorted = { table.unpack(tv.selection) }
      if sorted[1] > sorted[3] or (sorted[1] == sorted[3] and sorted[2] > sorted[4]) then
        sorted = { sorted[3], sorted[4], sorted[1], sorted[2] }
      end
      if idx >= sorted[2] or idx <= sorted[4] then
        local offset = 0
        for i = 1, #line, 2 do
          local text = line[i + 1]
          local length = text:ulen()
          if idx == sorted[2] and idx == sorted[4] then
            if offset + length >= sorted[1] and offset <= sorted[3] then
              local s = math.max(sorted[1] - offset, 0) + 1
              local e = math.min(sorted[3] - offset, length)
              table.insert(full_buffer, text:sub(s, e))
            end
          elseif idx == sorted[2] then
            if offset + length >= sorted[1] then
              local s = math.max(sorted[1] - offset, 0) + 1
              table.insert(full_buffer, text:sub(s, length))
            end
          elseif idx > sorted[2] and idx < sorted[4] then
            table.insert(full_buffer, text)
          elseif idx == sorted[4] then
            if offset <= sorted[3] then
              local e = math.min(sorted[3] - offset, length)
              table.insert(full_buffer, text:sub(1, e))
            end
          end
          offset = offset + length
        end
        if idx < sorted[4] then
          table.insert(full_buffer, "\n")
        end
      end
    end
    local text = table.concat(full_buffer)
    system.set_clipboard(text)
  end
})

command.add(nil, {
  ["terminal:toggle"] = function()
    if not view then
      view = TerminalView(config.plugins.terminal)
      local node = core.root_view:get_active_node()
      view.node = node:split("down", view, { y = true }, true)
      core.set_active_view(view)
    else
      view.node:close_view(core.root_view.root_node, view)
      view.terminal:close()
      view = nil
    end
  end
})

core.status_view:add_item({
  predicate = function()
    return core.active_view:is(TerminalView) and core.active_view.terminal
  end,
  name = "terminal:info",
  alignment = StatusView.Item.RIGHT,
  get_item = function()
    local dv = core.active_view
    local x, y = dv.terminal:size()
    return {
      style.text, style.font, (dv.terminal:name() or config.plugins.terminal.shell),
      style.dim, style.font, core.status_view.separator2,
      style.text, style.font, x .. "x" .. y
    }
  end
})



keymap.add {
  ["return"] = "terminal:return",
  ["backspace"] = "terminal:backspace",
  ["shift+backspace"] = "terminal:backspace",
  ["delete"] = "terminal:delete",
  ["ctrl+backspace"] = "terminal:ctrl-backspace",
  ["alt+backspace"] = "terminal:alt-backspace",
  ["ctrl+shift+c"] = "terminal:copy",
  ["ctrl+shift+v"] = "terminal:paste",
  ["wheel"] = "terminal:scroll",
  ["tab"] = "terminal:tab",
  ["pageup"] = "terminal:page-up",
  ["pagedown"] = "terminal:page-down",
  ["shift+pageup"] = "terminal:scroll-up",
  ["shift+pagedown"] = "terminal:scroll-down",
  ["shift+end"] = "terminal:scroll-to-end",
  ["shift+home"] = "terminal:scroll-to-top",
  ["up"] = "terminal:up",
  ["down"] = "terminal:down",
  ["left"] = "terminal:left",
  ["right"] = "terminal:right",
  ["ctrl+up"] = "terminal:jump-up",
  ["ctrl+down"] = "terminal:jump-down",
  ["ctrl+left"] = "terminal:jump-left",
  ["ctrl+right"] = "terminal:jump-right",
  ["alt+left"] = "terminal:jump-left",
  ["alt+right"] = "terminal:jump-right",
  ["home"] = "terminal:home",
  ["end"] = "terminal:end",
  ["f1"] = "terminal:f1",
  ["f2"] = "terminal:f2",
  ["f3"] = "terminal:f3",
  ["f4"] = "terminal:f4",
  ["f5"] = "terminal:f5",
  ["f6"] = "terminal:f6",
  ["f7"] = "terminal:f7",
  ["f8"] = "terminal:f8",
  ["f9"] = "terminal:f9",
  ["f10"] = "terminal:f10",
  ["f11"] = "terminal:f11",
  ["f12"] = "terminal:f11",
  ["escape"] = "terminal:escape",
  ["ctrl+a"] = "terminal:start-of-heading",
  ["ctrl+b"] = "terminal:start-of-text",
  ["ctrl+c"] = "terminal:break",
  ["ctrl+d"] = "terminal:eof",
  ["ctrl+e"] = "terminal:enquriy",
  ["ctrl+f"] = "terminal:acknowledge",
  ["ctrl+g"] = "terminal:bell",
  ["ctrl+h"] = "terminal:backspace",
  ["ctrl+i"] = "terminal:tab",
  ["ctrl+j"] = "terminal:return",
  ["ctrl+k"] = "terminal:vertical-tab",
  ["ctrl+l"] = "terminal:redraw",
  ["ctrl+m"] = "terminal:carriage-feed",
  ["ctrl+n"] = "terminal:carriage-feed",
  ["ctrl+o"] = "terminal:shift-in",
  ["ctrl+n"] = "terminal:shift-out",
  ["ctrl+p"] = "terminal:data-line-escape",
  ["ctrl+q"] = "terminal:stop",
  ["ctrl+r"] = "terminal:history",
  ["ctrl+s"] = "terminal:start",
  ["ctrl+t"] = { "terminal:transpose", "terminal:toggle" },
  ["ctrl+u"] = "terminal:negative-acknowledge",
  ["ctrl+v"] = "terminal:synchronous-idel",
  ["ctrl+w"] = "terminal:end-of-transmission-block",
  ["ctrl+x"] = "terminal:cancel",
  ["ctrl+y"] = "terminal:end-of-medium",
  ["ctrl+z"] = "terminal:suspend",
  ["ctrl+["] = "terminal:escape",
  ["ctrl+\\"] = "terminal:file-separator",
  ["ctrl+]"] = "terminal:group-separator"
}

return {
  class = TerminalView
}


