--[[
    TestJig utilities for the Sifteo Thundercracker simulator's scripting environment

    Micah Elizabeth Scott <micah@misc.name> 
    Copyright <c> 2012 Sifteo, Inc.
   
    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:
    
    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.
    
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
]]--

local bit = require("bit")

require("radio")
require("vram")
require("luaunit")

jig = {}

    function jig:getHWID()
        -- Extract HWID from an ACK packet
        return string.sub(gx.cube:testGetACK(), -8)
    end

    function jig:peekHWID()
        -- Read the HWID from NVM
        local hwid = ""
        for i = 0, 7 do
            hwid = hwid .. string.char(gx.cube:nbPeek(i))
        end
        return hwid
    end

    function jig:getFlashACK()
        -- Return the last flash ACK counter byte received by the testjig
        return string.byte(string.sub(gx.cube:testGetACK(), 9, 9))
    end

    function jig:programFlashAndWait(hexCommands, acks, hexPrefix)
        local commands = packHex(hexCommands)
        local prefix = packHex(hexPrefix or '')
        acks = acks or #commands

        local baseline = self:getFlashACK()

        -- Send the prefix

        if #prefix then
            gx.cube:testWrite(prefix)
            gx.sys:vsleep(0.1)
        end

        -- Break the commands up into chunks that we can send in a single
        -- testjig transaction. The command data is always escaped with 0xFD bytes.

        local chunk = 6

        for i = 1, math.ceil(#commands / chunk) * chunk, chunk do
            local r = {}
            for j = i, math.min(i + chunk - 1, #commands) do
                r[1+#r] = string.char(0xFD)
                r[1+#r] = commands:sub(j,j)
            end
            gx.cube:testWrite(table.concat(r))
            gx.sys:vsleep(0.1)
        end
        
        -- Wait for acknowledgment from the flash state machine
        
        local deadline = gx.sys:vclock() + 1

        repeat
            gx.yield()
            if gx.sys:vclock() > deadline then
                error(string.format(
                    "Timeout while waiting for flash programming. " ..
                    "baseline=%02x acks=%02x current=%02x",
                    baseline, acks, self:getFlashACK()))
            end
        until bit.band(baseline + acks, 0xFF) == self:getFlashACK()
    end

    function jig:flashReset()
        -- Flash reset is acknowledged by toggling bit 0x40 in the 6th byte.

        local ack1 = gx.cube:testGetACK():byte(6)
        gx.cube:testWrite(packHex("fe"))
        gx.sys:vsleep(0.2)
        local ack2 = gx.cube:testGetACK():byte(6)

        assertEquals(bit.bxor(ack1, ack2), 0x40)
    end
