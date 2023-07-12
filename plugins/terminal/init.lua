-- mod-version:3
local core = require "core"
local config = require "core.config"
local command = require "core.command"
local style = require "core.style"
local common = require "core.common"
local View = require "core.view"

local terminal_native = require "plugins.terminal.terminal_native"


config.plugins.terminal = common.merge({
  shell = "bash"
}, config.plugins.terminal)


local TerminalView = View:extends()


function TerminalView:new(self)
  local view = self.super.new(self)
  view.last_size = { x = view.size.x, y = view.size.y }
  return view
end


function TerminalView:update()
  self.super.update(self)
  if not self.terminal or self.last_size.x ~= self.size.x or self.last_size.y ~= self.size.y then
    local x = math.floor(self.size.x / style.code_font:get_width("W"))
    local y = math.floor(self.size.y / style.code_font:get_height())
    if not view.terminal then
      view.terminal = terminal_native.new(x, y)
    else
      view.terminal:resize(x, y)
    end
  end
end


function TerminalView:draw()
  self.super.draw_background(self, style.background3)
  if view.terminal then
    local y = self.position.y
    local lh = style.code_font:get_height()
    for line in ipairs(view.terminal:lines()) do
      local x = self.position.x + style.padding.x
      for group in ipairs(line) do
        if group.background then
          renderer.draw_rect(x, y, style.code_font:get_width(group.text), lh, group.background)
        end
        x = renderer.draw_text(style.code_font, group.text, x, y, group.color)
      end
      y = y + lh
    end
  end
end


function TerminalView:on_key_pressed(key, ...)
  if view.terminal
    view.terminal:input(key)
  end
end


function TerminalView:on_text_input(text)
  if view.terminal
    terminal_native:input(text)
  end
end


local view = TerminalView()
local node = core.root_view:get_active_node()
view.node = node:split("bottom", view, {y = true}, true)

return {
  view = view,
  class = TerminalView
}


