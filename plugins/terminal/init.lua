-- mod-version:3
local core = require "core"
local config = require "core.config"
local command = require "core.command"
local style = require "core.style"
local common = require "core.common"
local View = require "core.view"
local keymap = require "core.keymap"

local terminal_native = require "plugins.terminal.libterminal"


config.plugins.terminal = common.merge({
  shell = "bash"
}, config.plugins.terminal)


local TerminalView = View:extend()
local view

function TerminalView:get_name() return "Terminal" end
function TerminalView:supports_text_input() return true end

function TerminalView:new()
  TerminalView.super.new(self)
  self.size.y = 200
  self.last_size = { x = self.size.x, y = self.size.y }
end


function TerminalView:update()
  if not self.terminal or self.last_size.x ~= self.size.x or self.last_size.y ~= self.size.y then
    local x = math.floor(self.size.x / style.code_font:get_width("W"))
    local y = math.floor(self.size.y / style.code_font:get_height())
    if x > 0 and y > 0 then
      if not self.terminal then
        self.terminal = terminal_native.new(x, y, config.plugins.terminal.shell, {})
      else
        self.terminal:resize(x, y)
      end
    end
  end
  TerminalView.super.update(self)
end


function TerminalView:draw()
  TerminalView.super.draw_background(self, style.background3)
  if self.terminal then
    local cursor_x, cursor_y = self.terminal:cursor()
    local space_width = style.code_font:get_width(" ")

    local y = self.position.y + style.padding.y
    local lh = style.code_font:get_height()
    for i, line in ipairs(self.terminal:lines()) do
      local x = self.position.x + style.padding.x
      if core.active_view == self and i - 1 == cursor_y then
        renderer.draw_rect(x + space_width * cursor_x, y, space_width, lh, style.accent)
      end
      for i, group in ipairs(line) do
        if group.background then
          renderer.draw_rect(x, y, style.code_font:get_width(group.text), lh, group.background)
        end
        x = renderer.draw_text(style.code_font, group.text, x, y, group.color or style.text)
      end
      y = y + lh
    end
  end
end


function TerminalView:on_text_input(text)
  if self.terminal then
    self.terminal:input(text)
    return true
  end
end


view = TerminalView()
local node = core.root_view:get_active_node()
view.node = node:split("down", view, { y = true }, true)

command.add(TerminalView, {
  ["terminal:backspace"] = function() view.terminal:input("\b"); end,
  ["terminal:return"] = function() view.terminal:input("\n"); end
});

keymap.add {
  ["return"] = "terminal:return",
  ["backspace"] = "terminal:backspace"
}

return {
  view = view,
  class = TerminalView
}


