#include "Nes_Cpu_Impl.h"

template<u16 op> // Execute a single CPU instruction, defined by opcode "op".
void Ins(Nes_Cpu_Impl & cpu)       // With template magic, the compiler will literally synthesize >256 different functions.
{
	// Note: op 0x100 means "NMI", 0x101 means "Reset", 0x102 means "IRQ". They are implemented in terms of "BRK".
	// User is responsible for ensuring that WB() will not store into memory while Reset is being processed.
	unsigned addr = 0, d = 0, t = 0xFF, c = 0, sb = 0, pbits = op < 0x100 ? 0x30 : 0x20;

	// Define the opcode decoding matrix, which decides which micro-operations constitute
	// any particular opcode. (Note: The PLA of 6502 works on a slightly different principle.)
	enum { o8 = op / 8, o8m = 1 << (op % 8) };
	// Fetch op'th item from a bitstring encoded in a data-specific variant of base64,
	// where each character transmits 8 bits of information rather than 6.
	// This peculiar encoding was chosen to reduce the source code size.
	// Enum temporaries are used in order to ensure compile-time evaluation.
#define t(s,code) { enum { \
            i=o8m & (s[o8]>90 ? (130+" (),-089<>?BCFGHJLSVWZ[^hlmnxy|}"[s[o8]-94]) \
                              : (s[o8]-" (("[s[o8]/39])) }; if(i) { code; } }

	/* Decode address operand */
	t("                                !", addr = 0xFFFA) // NMI vector location
	t("                                *", addr = 0xFFFC) // Reset vector location
	t("!                               ,", addr = 0xFFFE) // Interrupt vector location
	t("zy}z{y}zzy}zzy}zzy}zzy}zzy}zzy}z ", addr = cpu.RB(cpu.PC++))
	t("2 yy2 yy2 yy2 yy2 XX2 XX2 yy2 yy ", d = cpu.X) // register index
	t("  62  62  62  62  om  om  62  62 ", d = cpu.Y)
	t("2 y 2 y 2 y 2 y 2 y 2 y 2 y 2 y  ", addr = u8(addr + d); d = 0; cpu.tick())              // add zeropage-index
	t(" y z!y z y z y z y z y z y z y z ", addr = u8(addr);   addr += 256 * cpu.RB(cpu.PC++))       // absolute address
	t("3 6 2 6 2 6 286 2 6 2 6 2 6 2 6 /", addr = cpu.RB(c = addr); addr += 256 * cpu.RB(cpu.wrap(c, c + 1)))// indirect w/ page wrap
	t("  *Z  *Z  *Z  *Z      6z  *Z  *Z ", cpu.Misfire(addr, addr + d)) // abs. load: extra misread when cross-page
	t("  4k  4k  4k  4k  6z      4k  4k ", cpu.RB(cpu.wrap(addr, addr + d)))// abs. store: always issue a misread
																		/* Load source operand */
	t("aa__ff__ab__,4  ____ -  ____     ", t &= cpu.A) // Many operations take A or X as operand. Some try in
	t("                knnn     4  99   ", t &= cpu.X) // error to take both; the outcome is an AND operation.
	t("                9989    99       ", t &= cpu.Y) // sty,dey,iny,tya,cpy
	t("                       4         ", t &= cpu.S) // tsx, las
	t("!!!!  !!  !!  !!  !   !!  !!  !!/", t &= cpu.P.raw | pbits; c = t)// php, flag test/set/clear, interrupts
	t("_^__dc___^__            ed__98   ", c = t; t = 0xFF)        // save as second operand
	t("vuwvzywvvuwvvuwv    zy|zzywvzywv ", t &= cpu.RB(addr + d)) // memory operand
	t(",2  ,2  ,2  ,2  -2  -2  -2  -2   ", t &= cpu.RB(cpu.PC++))   // immediate operand
																/* Operations that mogrify memory operands directly */
	t("    88                           ", cpu.P.V = t & 0x40; cpu.P.N = t & 0x80) // bit
	t("    nink    nnnk                 ", sb = cpu.P.C)       // rol,rla, ror,rra,arr
	t("nnnknnnk     0                   ", cpu.P.C = t & 0x80) // rol,rla, asl,slo,[arr,anc]
	t("        nnnknink                 ", cpu.P.C = t & 0x01) // lsr,sre, ror,rra,asr
	t("ninknink                         ", t = (t << 1) | (sb * 0x01))
	t("        nnnknnnk                 ", t = (t >> 1) | (sb * 0x80))
	t("                 !      kink     ", t = u8(t - 1))  // dec,dex,dey,dcp
	t("                         !  khnk ", t = u8(t + 1))  // inc,inx,iny,isb
															   /* Store modified value (memory) */
	t("kgnkkgnkkgnkkgnkzy|J    kgnkkgnk ", cpu.WB(addr + d, t))
	t("                   q             ", cpu.WB(cpu.wrap(addr, addr + d), t &= ((addr + d) >> 8))) // [shx,shy,shs,sha?]
																								 /* Some operations used up one clock cycle that we did not account for yet */
	t("rpstljstqjstrjst - - - -kjstkjst/", cpu.tick()) // nop,flag ops,inc,dec,shifts,stack,transregister,interrupts
													   /* Stack operations and unconditional jumps */
	t("     !  !    !                   ", cpu.tick(); t = cpu.Pop())                        // pla,plp,rti
	t("        !   !                    ", cpu.RB(cpu.PC++); cpu.PC = cpu.Pop(); cpu.PC |= (cpu.Pop() << 8)) // rti,rts
	t("            !                    ", cpu.RB(cpu.PC++))  // rts
	t("!   !                           /", d = cpu.PC + (op ? -1 : 1); cpu.Push(d >> 8); cpu.Push(d))      // jsr, interrupts
	t("!   !    8   8                  /", cpu.PC = addr) // jmp, jsr, interrupts
	t("!!       !                      /", cpu.Push(t))   // pha, php, interrupts
														  /* Bitmasks */
	t("! !!  !!  !!  !!  !   !!  !!  !!/", t = 1)
	t("  !   !                   !!  !! ", t <<= 1)
	t("! !   !   !!  !!       !   !   !/", t <<= 2)
	t("  !   !   !   !        !         ", t <<= 4)
	t("   !       !           !   !____ ", t = u8(~t)) // sbc, isb,      clear flag
	t("`^__   !       !               !/", t = c | t)  // ora, slo,      set flag
	t("  !!dc`_  !!  !   !   !!  !!  !  ", t = c & t)  // and, bit, rla, clear/test flag
	t("        _^__                     ", t = c ^ t)  // eor, sre
														   /* Conditional branches */
	t("      !       !       !       !  ", if (t) { cpu.tick(); cpu.Misfire(cpu.PC, addr = s8(addr) + cpu.PC); cpu.PC = addr; })
	t("  !       !       !       !      ", if (!t) { cpu.tick(); cpu.Misfire(cpu.PC, addr = s8(addr) + cpu.PC); cpu.PC = addr; })
	/* Addition and subtraction */
	t("            _^__            ____ ", c = t; t += cpu.A + cpu.P.C; cpu.P.V = (c^t) & (cpu.A^t) & 0x80; cpu.P.C = t & 0x100)
	t("                        ed__98   ", t = c - t; cpu.P.C = ~t & 0x100) // cmp,cpx,cpy, dcp, sbx
																			/* Store modified value (register) */
	t("aa__aa__aa__ab__ 4 !____    ____ ", cpu.A = t)
	t("                    nnnn 4   !   ", cpu.X = t) // ldx, dex, tax, inx, tsx,lax,las,sbx
	t("                 !  9988 !       ", cpu.Y = t) // ldy, dey, tay, iny
	t("                   4   0         ", cpu.S = t) // txs, las, shs
	t("!  ! ! !!  !   !       !   !   !/", cpu.P.raw = t & ~0x30) // plp, rti, flag set/clear
																  /* Generic status flag updates */
	t("wwwvwwwvwwwvwxwv 5 !}}||{}wv{{wv ", cpu.P.N = t & 0x80)
	t("wwwv||wvwwwvwxwv 5 !}}||{}wv{{wv ", cpu.P.Z = u8(t) == 0)
	t("             0                   ", cpu.P.V = (((t >> 5) + 1) & 2))         // [arr]
																				   /* All implemented opcodes are cycle-accurate and memory-access-accurate.
																				   * [] means that this particular separate rule exists only to provide the indicated unofficial opcode(s).
																				   */
}

void Nes_Cpu_Impl::Op()
{
	/* Check the state of NMI flag */
	bool nmi_now = nmi;

	unsigned op = RB(PC++);

	if (reset) { op = 0x101; }
	else if (nmi_now && !nmi_edge_detected) { op = 0x100; nmi_edge_detected = true; }
	else if (intr && !P.I) { op = 0x102; }
	if (!nmi_now) nmi_edge_detected = false;

	// Define function pointers for each opcode (00..FF) and each interrupt (100,101,102)
#define c(n) Ins<0x##n>,Ins<0x##n+1>,
#define o(n) c(n)c(n+2)c(n+4)c(n+6)
	static void(*const i[0x108])(Nes_Cpu_Impl &) =
	{
		o(00)o(08)o(10)o(18)o(20)o(28)o(30)o(38)
		o(40)o(48)o(50)o(58)o(60)o(68)o(70)o(78)
		o(80)o(88)o(90)o(98)o(A0)o(A8)o(B0)o(B8)
		o(C0)o(C8)o(D0)o(D8)o(E0)o(E8)o(F0)o(F8) o(100)
	};
#undef o
#undef c
	i[op](*this);

	reset = false;
};
