// NES 6502 CPU emulator run function

#if 0
/* Define these macros in the source file before #including this file.
- Parameters might be expressions, so they are best evaluated only once,
though they NEVER have side-effects, so multiple evaluation is OK.
- Output parameters might be a multiple-assignment expression like "a=x",
so they must NOT be parenthesized.
- Except where noted, time() and related functions will NOT work
correctly inside a macro. TIME() is always correct, and FLUSH_TIME() and
CACHE_TIME() allow the time changing functions to work.
- Macros "returning" void may use a {} statement block. */

	// 0 <= addr <= 0xFFFF + page_size
	// time functions can be used
	int  READ_MEM(  addr_t );
	void WRITE_MEM( addr_t, int data );
	// 0 <= READ_MEM() <= 0xFF
	
	// 0 <= addr <= 0x1FF
	int  READ_LOW(  addr_t );
	void WRITE_LOW( addr_t, int data );
	// 0 <= READ_LOW() <= 0xFF

	// Often-used instructions attempt these before using a normal memory access.
	// Optional; defaults to READ_MEM() and WRITE_MEM()
	bool CAN_READ_FAST( addr_t ); // if true, uses result of READ_FAST
	void READ_FAST( addr_t, int& out ); // ALWAYS called BEFORE CAN_READ_FAST
	bool CAN_WRITE_FAST( addr_t ); // if true, uses WRITE_FAST instead of WRITE_MEM
	void WRITE_FAST( addr_t, int data );

	// Used by instructions most often used to access the NES PPU (LDA abs and BIT abs).
	// Optional; defaults to READ_MEM.
	void READ_PPU(  addr_t, int& out );
	// 0 <= out <= 0xFF

// The following can be used within macros:
	
	// Current time
	time_t TIME();
	
	// Allows use of time functions
	void FLUSH_TIME();
	
	// Must be used before end of macro if FLUSH_TIME() was used earlier
	void CACHE_TIME();

// Configuration (optional; commented behavior if defined)
	
	// Emulates dummy reads for indexed instructions
	#define NES_CPU_DUMMY_READS 1
	
	// Optimizes as if map_code( 0, 0x10000 + cpu_padding, FLAT_MEM ) is always in effect
	#define FLAT_MEM my_mem_array
	
	// Expanded just before beginning of code, to help debugger
	#define CPU_BEGIN void my_run_cpu() {
	
#endif

/* Copyright (C) 2003-2008 Shay Green. This module is free software; you
can redistribute it and/or modify it under the terms of the GNU Lesser
General Public License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version. This
module is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
details. You should have received a copy of the GNU Lesser General Public
License along with this module; if not, write to the Free Software Foundation,
Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA */

// Time
#define TIME()          (s_time + s.base)
#define FLUSH_TIME()    {s.time = s_time - time_offset;}
#define CACHE_TIME()    {s_time = s.time + time_offset;}

// Defaults
#ifndef CAN_WRITE_FAST
	#define CAN_WRITE_FAST( addr )      0
	#define WRITE_FAST( addr, data )
#endif

#ifndef CAN_READ_FAST
	#define CAN_READ_FAST( addr )       0
	#define READ_FAST( addr, out )
#endif

#ifndef READ_PPU
	#define READ_PPU( addr, out )\
	{\
		FLUSH_TIME();\
		out = READ_MEM( addr );\
		CACHE_TIME();\
	}
#endif

#define READ_STACK  READ_LOW
#define WRITE_STACK WRITE_LOW

// Code
#ifdef FLAT_MEM
#define CODE_PAGE(   addr ) (FLAT_MEM)
#define CODE_OFFSET( addr ) (addr)
#else
#define CODE_PAGE( addr )   (s.code_map [NES_CPU_PAGE( addr )])
#define CODE_OFFSET( addr ) NES_CPU_OFFSET( addr )
#endif
#define READ_CODE( addr )   (CODE_PAGE( addr ) [CODE_OFFSET( addr )])

#include "Nes_Cpu_Impl.h"

// Allows MWCW debugger to step through code properly
#ifdef CPU_BEGIN
	CPU_BEGIN
#endif

{
	int const time_offset = 0;
	
	// Local state
	Nes_Cpu::cpu_state_t s;
	#ifdef FLAT_MEM
		s.base = CPU.cpu_state_.base;
	#else
		s = CPU.cpu_state_;
	#endif
	CPU.cpu_state = &s;
	int s_time = CPU.cpu_state_.time; // helps even on x86

	Nes_Cpu_Impl mcpu;

	mcpu.PC = CPU.r.pc;
	mcpu.A = CPU.r.a;
	mcpu.X = CPU.r.x;
	mcpu.Y = CPU.r.y;
	mcpu.S = CPU.r.sp;

	mcpu.P.raw = CPU.r.flags;
	
	auto tick = [&]() -> void
	{
		s_time++;
	};

	auto RB = [&](u16 addr) -> u8
	{
		// Memory writes are turned into reads while reset is being signalled
		u8 r;

		tick();
		FLUSH_TIME();
		READ_FAST(addr, r);
		if (!CAN_READ_FAST(addr)) r = READ_MEM(addr);
		CACHE_TIME();

		return r;
	};

	auto WB = [&](u16 addr, u8 v) -> void
	{
		// Memory writes are turned into reads while reset is being signalled
		if (mcpu.reset) { RB(addr); return; }

		tick();

		FLUSH_TIME();
		if (CAN_WRITE_FAST(addr)) WRITE_FAST(addr, v);
		else WRITE_MEM(addr, v);
		CACHE_TIME();
	};

	mcpu.tick = tick;
	mcpu.RB = RB;
	mcpu.WB = WB;

loop:
	
	// Read instruction
    
    u8 r = READ_CODE(mcpu.PC);
    if (r == CPU.halt_opcode) goto stop;
    
	mcpu.Op();

	// Update time
	if ( s_time >= 0 )
		goto out_of_time;
	
	#ifdef CPU_INSTR_HOOK
	{ CPU_INSTR_HOOK( (pc-1), (&instr [-1]), a, x, y, GET_SP(), TIME() ); }
	#endif

	goto loop;
	
out_of_time:
	// Optional action that triggers interrupt or changes irq/end time
	#ifdef CPU_DONE
	{
		CPU_DONE( result_ );
		if ( result_ >= 0 )
			goto interrupt;
		if ( s_time < 0 )
			goto loop;
	}
	#endif
stop:
	
	// Flush cached state
	CPU.r.pc = mcpu.PC;
	CPU.r.sp = mcpu.S;
	CPU.r.a  = mcpu.A;
	CPU.r.x  = mcpu.X;
	CPU.r.y  = mcpu.Y;

	CPU.r.flags = mcpu.P.raw;
	
	CPU.cpu_state_.base = s.base;
	CPU.cpu_state_.time = s_time;
	CPU.cpu_state = &CPU.cpu_state_;
}
