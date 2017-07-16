#ifndef _NES_CPU_IMPL_H_
#define _NES_CPU_IMPL_H_

#include <functional>

#include <stdint.h>

typedef uint_least32_t u32;
typedef uint_least16_t u16;
typedef uint_least8_t   u8;
typedef  int_least8_t   s8;

// Bitfield utilities
template<unsigned bitno, unsigned nbits = 1, typename T = u8>
struct RegBit
{
	T data;
	enum { mask = (1u << nbits) - 1u };
	template<typename T2>
	RegBit& operator=(T2 val)
	{
		data = (data & ~(mask << bitno)) | ((nbits > 1 ? val & mask : !!val) << bitno);
		return *this;
	}
	operator unsigned() const { return (data >> bitno) & mask; }
	RegBit& operator++ () { return *this = *this + 1; }
	unsigned operator++ (int) { unsigned r = *this; ++*this; return r; }
};

struct Nes_Cpu_Impl
{
	bool reset;
	bool nmi;
	bool nmi_edge_detected;
	bool intr;

    u16 PC;
    u8 A, X, Y, S;

	union /* Status flags: */
	{
		u8 raw;
		RegBit<0> C; // carry
		RegBit<1> Z; // zero
		RegBit<2> I; // interrupt enable/disable
		RegBit<3> D; // decimal mode (unsupported on NES, but flag exists)
					 // 4,5 (0x10,0x20) don't exist
		RegBit<6> V; // overflow
		RegBit<7> N; // negative
	} P;

	std::function<void()> tick;
	std::function<u8(u16)> RB;
	std::function<void(u16, u8)> WB;

	Nes_Cpu_Impl()
	{
		reset = false;
		nmi = false;
		nmi_edge_detected = false;
		intr = false;
	}

	u16 wrap(u16 oldaddr, u16 newaddr) { return (oldaddr & 0xFF00) + u8(newaddr); }
	void Misfire(u16 old, u16 addr) { u16 q = wrap(old, addr); if (q != addr) RB(q); }
	u8 Pop() { return RB(0x100 | u8(++S)); }
	void Push(u8 v) { WB(0x100 | u8(S--), v); }

	void Op();
};

#endif
