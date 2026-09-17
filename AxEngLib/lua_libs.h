#pragma once

#include <cstdint>

namespace ax::lua::libs
{
    // todo: replace these with #export when supported

    constexpr std::uint8_t init[] =
        R"(
--
-- Change some built-ins
--

-- use spdlog lib to print
local __print = print
print = function(...)
    local arg={...}
    local res = "<Lua> "
    for i,v in ipairs(arg) do
        if v == nil then
            v = "nil"
        end
        res = res .. tostring(v)
    end
    log.info(res)
end

-- disable require
local __require = require
require = function()
    log.error("Please use import instead.")
end

)";
    constexpr std::uint8_t std_lib[] =
		R"(-- AxEng Std Lua Library
-- Automatically runs before any other script to help setup the environment

ax = {}

ax.get_table_size = function(tbl)
    local c = 0
    for _ in pairs(tbl) do
        c = c + 1
    end
    return c
end

ax.print_table = function(tbl, lvl)
    if lvl == nil then lvl = 0 end
    local indent_str = string.rep("   ", lvl)

    for k, v in pairs(tbl) do
        if type(v) == "table" then
            print(indent_str, k, " = {")
            ax.print_table(v, lvl + 1)
            print(indent_str, "}")
        else
            print(indent_str, k, " = ", tostring(v))
        end
    end
end

ax.rstrip = function(str)
    return (str:gsub("%s+$", ""))
end

ax.assert_success = function(err, msg)
    if (err ~= error_code.Success) then
        log.error(msg)
    end
    assert(err == error_code.Success)
end
 
ax.import = function(script_name)
    if script_name == nil or #script_name == 0 then
        log.error("No name provided for import")
        return error_code.InvalidScript
    end
    
    if app.imported == nil then
        app.imported = {}
    end

    if app.imported[script_name] then
        return app.imported[script_name]
    end

    local script = app.res.get_script(script_name)

    if (not script.valid) then
        log.error("Unable to load script: '" .. script_name .. "'.")
        return error_code.AssetNotFound
    end

    local res = app.run_in_this_environment(script)
    if res == nil then
        log.error("Failed to run script '" .. script_name .. "'.")
        return error_code.Lua
    end

    app.imported[script_name] = res
    return res
end

ax.template_replace = function(str, data)
    local ret = ""
    local pos = 0
    local idx = str:find("%%")
    local expect_end = false

    while idx ~= nil do
        local idx_e = str:find("%%", idx + 2, true)
        local key = str:sub(idx+2, idx_e-1)

        local used_key = false
        if #key > 6 and key:sub(1,1) == "?" then
            local inner_key = key:sub(5)
            local test_check = inner_key:sub(1,1) == "~"
            if test_check then
                inner_key = inner_key:sub(2)
            end
            local idx_end_start = str:find("%END%", idx_e + 2, true)
            print("inner key value: ", data[inner_key])
            if tostring(data[inner_key]) == tostring(test_check) then
                used_key = true
                ret = ret .. str:sub(pos, idx - 1)
                idx_e = idx_end_start + 5
                pos = idx_e
            else
                used_key = true
                ret = ret .. str:sub(pos, idx - 1)
                pos = idx_e + 2
                expect_end = true
            end
        end

        if not used_key then
            if data[key] == nil then
                log.error("Template key '"..key.."' not found in data.")
            else
                ret = ret .. str:sub(pos, idx - 1) .. tostring(data[key])
                pos = idx_e + 2
            end
        end

        idx = str:find("%%", idx_e + 2, true)

        if expect_end then
            idx_end = str:find("%END%", idx_e + 2, true)
            if idx > idx_end then
                ret = ret .. str:sub(pos, idx_end - 1) 
                pos = idx_end + 5
                expect_end = false
            end
        end
    end

    ret = ret .. str:sub(pos, -1)

    return ret
end

-- todo: generate this
ax.key_map = {
    esc = 256,
    w = 87,
    a = 65,
    s = 83,
    d = 68,
}

return ax
)";
	constexpr std::uint8_t json[] = R"(
-- json.lua
--
-- Copyright (c) 2020 rxi
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy of
-- this software and associated documentation files (the "Software"), to deal in
-- the Software without restriction, including without limitation the rights to
-- use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
-- of the Software, and to permit persons to whom the Software is furnished to do
-- so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in all
-- copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
-- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
-- SOFTWARE.
--

local json = { _version = "0.1.2" }

-------------------------------------------------------------------------------
-- Encode
-------------------------------------------------------------------------------

local encode

local escape_char_map = {
  [ "\\" ] = "\\",
  [ "\"" ] = "\"",
  [ "\b" ] = "b",
  [ "\f" ] = "f",
  [ "\n" ] = "n",
  [ "\r" ] = "r",
  [ "\t" ] = "t",
}

local escape_char_map_inv = { [ "/" ] = "/" }
for k, v in pairs(escape_char_map) do
  escape_char_map_inv[v] = k
end


local function escape_char(c)
  return "\\" .. (escape_char_map[c] or string.format("u%04x", c:byte()))
end

local function concat_indent(tbl, indent)
  local prefix_str = string.rep("   ", indent)
  local join = ",\n"
  local ret = {}
  for k, v in pairs(tbl) do
    ret[k] = prefix_str .. v
  end
  return table.concat(ret, join)
end

local function encode_nil(val, indent)
  return "null"
end

local function encode_table(val, indent, stack)
  local res = {}
  stack = stack or {}
  local indent = indent or 0
  local prefix_str = string.rep("   ", indent)

  -- Circular reference?
  if stack[val] then error("circular reference") end

  stack[val] = true

  if rawget(val, 1) ~= nil or next(val) == nil then
    -- Treat as array -- check keys are valid and it is not sparse
    local n = 0
    for k in pairs(val) do
      if type(k) ~= "number" then
        error("invalid table: mixed or invalid key types")
      end
      n = n + 1
    end
    if n ~= #val then
      error("invalid table: sparse array")
    end
    -- Encode
    for i, v in ipairs(val) do
      table.insert(res, encode(v, indent + 1, stack))
    end
    stack[val] = nil
    return "[\n" .. concat_indent(res, indent + 1) .. "\n" .. prefix_str .. "]"

  else
    -- Treat as an object
    for k, v in pairs(val) do
      if type(k) ~= "string" then
        error("invalid table: mixed or invalid key types")
      end
      table.insert(res, encode(k, indent + 1, stack) .. ":" .. encode(v, indent + 1, stack))
    end
    stack[val] = nil
    return "{\n" .. concat_indent(res, indent + 1) .. "\n" .. prefix_str .. "}"
  end
end


local function encode_string(val, indent)
  return '"' .. val:gsub('[%z\1-\31\\"]', escape_char) .. '"'
end


local function encode_number(val, indent)
  -- Check for NaN, -inf and inf
  if val ~= val or val <= -math.huge or val >= math.huge then
    error("unexpected number value '" .. tostring(val) .. "'")
  end
  return string.format("%.14g", val)
end

local function encode_bool(val, indent)
  return tostring(val)
end

local type_func_map = {
  [ "nil"     ] = encode_nil,
  [ "table"   ] = encode_table,
  [ "string"  ] = encode_string,
  [ "number"  ] = encode_number,
  [ "boolean" ] = encode_bool,
}


encode = function(val, indent, stack)
  local t = type(val)
  local f = type_func_map[t]
  if f then
    return f(val, indent, stack)
  end
  error("unexpected type '" .. t .. "'")
end


function json.encode(val)
  return ( encode(val) )
end


-------------------------------------------------------------------------------
-- Decode
-------------------------------------------------------------------------------

local parse

local function create_set(...)
  local res = {}
  for i = 1, select("#", ...) do
    res[ select(i, ...) ] = true
  end
  return res
end

local space_chars   = create_set(" ", "\t", "\r", "\n")
local delim_chars   = create_set(" ", "\t", "\r", "\n", "]", "}", ",")
local escape_chars  = create_set("\\", "/", '"', "b", "f", "n", "r", "t", "u")
local literals      = create_set("true", "false", "null")

local literal_map = {
  [ "true"  ] = true,
  [ "false" ] = false,
  [ "null"  ] = nil,
}


local function next_char(str, idx, set, negate)
  for i = idx, #str do
    if set[str:sub(i, i)] ~= negate then
      return i
    end
  end
  return #str + 1
end


local function decode_error(str, idx, msg)
  local line_count = 1
  local col_count = 1
  for i = 1, idx - 1 do
    col_count = col_count + 1
    if str:sub(i, i) == "\n" then
      line_count = line_count + 1
      col_count = 1
    end
  end
  error( string.format("%s at line %d col %d", msg, line_count, col_count) )
end


local function codepoint_to_utf8(n)
  -- http://scripts.sil.org/cms/scripts/page.php?site_id=nrsi&id=iws-appendixa
  local f = math.floor
  if n <= 0x7f then
    return string.char(n)
  elseif n <= 0x7ff then
    return string.char(f(n / 64) + 192, n % 64 + 128)
  elseif n <= 0xffff then
    return string.char(f(n / 4096) + 224, f(n % 4096 / 64) + 128, n % 64 + 128)
  elseif n <= 0x10ffff then
    return string.char(f(n / 262144) + 240, f(n % 262144 / 4096) + 128,
                       f(n % 4096 / 64) + 128, n % 64 + 128)
  end
  error( string.format("invalid unicode codepoint '%x'", n) )
end


local function parse_unicode_escape(s)
  local n1 = tonumber( s:sub(1, 4),  16 )
  local n2 = tonumber( s:sub(7, 10), 16 )
   -- Surrogate pair?
  if n2 then
    return codepoint_to_utf8((n1 - 0xd800) * 0x400 + (n2 - 0xdc00) + 0x10000)
  else
    return codepoint_to_utf8(n1)
  end
end


local function parse_string(str, i)
  local res = ""
  local j = i + 1
  local k = j

  while j <= #str do
    local x = str:byte(j)

    if x < 32 then
      decode_error(str, j, "control character in string")

    elseif x == 92 then -- `\`: Escape
      res = res .. str:sub(k, j - 1)
      j = j + 1
      local c = str:sub(j, j)
      if c == "u" then
        local hex = str:match("^[dD][89aAbB]%x%x\\u%x%x%x%x", j + 1)
                 or str:match("^%x%x%x%x", j + 1)
                 or decode_error(str, j - 1, "invalid unicode escape in string")
        res = res .. parse_unicode_escape(hex)
        j = j + #hex
      else
        if not escape_chars[c] then
          decode_error(str, j - 1, "invalid escape char '" .. c .. "' in string")
        end
        res = res .. escape_char_map_inv[c]
      end
      k = j + 1

    elseif x == 34 then -- `"`: End of string
      res = res .. str:sub(k, j - 1)
      return res, j + 1
    end

    j = j + 1
  end

  decode_error(str, i, "expected closing quote for string")
end


local function parse_number(str, i)
  local x = next_char(str, i, delim_chars)
  local s = str:sub(i, x - 1)
  local n = tonumber(s)
  if not n then
    decode_error(str, i, "invalid number '" .. s .. "'")
  end
  return n, x
end


local function parse_literal(str, i)
  local x = next_char(str, i, delim_chars)
  local word = str:sub(i, x - 1)
  if not literals[word] then
    decode_error(str, i, "invalid literal '" .. word .. "'")
  end
  return literal_map[word], x
end


local function parse_array(str, i)
  local res = {}
  local n = 1
  i = i + 1
  while 1 do
    local x
    i = next_char(str, i, space_chars, true)
    -- Empty / end of array?
    if str:sub(i, i) == "]" then
      i = i + 1
      break
    end
    -- Read token
    x, i = parse(str, i)
    res[n] = x
    n = n + 1
    -- Next token
    i = next_char(str, i, space_chars, true)
    local chr = str:sub(i, i)
    i = i + 1
    if chr == "]" then break end
    if chr ~= "," then decode_error(str, i, "expected ']' or ','") end
  end
  return res, i
end


local function parse_object(str, i)
  local res = {}
  i = i + 1
  while 1 do
    local key, val
    i = next_char(str, i, space_chars, true)
    -- Empty / end of object?
    if str:sub(i, i) == "}" then
      i = i + 1
      break
    end
    -- Read key
    if str:sub(i, i) ~= '"' then
      decode_error(str, i, "expected string for key")
    end
    key, i = parse(str, i)
    -- Read ':' delimiter
    i = next_char(str, i, space_chars, true)
    if str:sub(i, i) ~= ":" then
      decode_error(str, i, "expected ':' after key")
    end
    i = next_char(str, i + 1, space_chars, true)
    -- Read value
    val, i = parse(str, i)
    -- Set
    res[key] = val
    -- Next token
    i = next_char(str, i, space_chars, true)
    local chr = str:sub(i, i)
    i = i + 1
    if chr == "}" then break end
    if chr ~= "," then decode_error(str, i, "expected '}' or ','") end
  end
  return res, i
end


local char_func_map = {
  [ '"' ] = parse_string,
  [ "0" ] = parse_number,
  [ "1" ] = parse_number,
  [ "2" ] = parse_number,
  [ "3" ] = parse_number,
  [ "4" ] = parse_number,
  [ "5" ] = parse_number,
  [ "6" ] = parse_number,
  [ "7" ] = parse_number,
  [ "8" ] = parse_number,
  [ "9" ] = parse_number,
  [ "-" ] = parse_number,
  [ "t" ] = parse_literal,
  [ "f" ] = parse_literal,
  [ "n" ] = parse_literal,
  [ "[" ] = parse_array,
  [ "{" ] = parse_object,
}


parse = function(str, idx)
  local chr = str:sub(idx, idx)
  local f = char_func_map[chr]
  if f then
    return f(str, idx)
  end
  decode_error(str, idx, "unexpected character '" .. chr .. "'")
end


function json.decode(str)
  if type(str) ~= "string" then
    error("expected argument of type string, got " .. type(str))
  end
  local res, idx = parse(str, next_char(str, 1, space_chars, true))
  idx = next_char(str, idx, space_chars, true)
  if idx <= #str then
    decode_error(str, idx, "trailing garbage")
  end
  return res
end


return json
)";
      constexpr std::uint8_t compiler_core[] = R"(local json = ax.import("@json")

local comp = {}

local found_error = false
local function key_exists(tbl, key)
    if tbl[key] == nil then
        log.error("Can't find project data: " .. key)
        found_error = true
    end
end

comp.open_project = function(directory)
    local file = io.open(directory .. "/" .. "project.json", "r")
    if file == nil then
        return error_code.IO
    end
    local json_str = file:read("*all")
    file:close()

    local success, ret = pcall(function() return json.decode(json_str) end)

    if not success then
        log.error("Failed to parse project.json: ", ret)
        return nil
    end

    return ret
end

comp.validate_project = function(project)
    found_error = false

    key_exists(project, "name")
    key_exists(project, "entry_point")
    key_exists(project, "scripts")

    if found_error then
        log.error("Invalid project file.")
        return false
    end

    return true
end

comp.check_script = function(script_file)
    local chunk, err_msg = loadfile(script_file)

    -- make sure it loaded
    if chunk == nil then
        return false, err_msg
    end

    return true, chunk
end

comp.get_project_directory_from_args = function()
    local in_pair_state = false
    local ret = ""

    for i in pairs(args) do
        if in_pair_state then
            ret = ax.rstrip(args[i])
            in_pair_state = false
        else
            if args[i] == "--project" then
                in_pair_state = true
            end
        end
    end

    return ret
end

return comp
)";
}