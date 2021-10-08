#include "SDL.h"
#include "cputable.h"
#include <sys/time.h>

unsigned char vgapal[] = {
	0x00, 0x00, 0x00,
	0x00, 0x00, 0x2a,
	0x00, 0x2a, 0x00,
	0x00, 0x2a, 0x2a,
	0x2a, 0x00, 0x00,
	0x2a, 0x00, 0x2a,
	0x2a, 0x15, 0x00,
	0x2a, 0x2a, 0x2a,
	0x15, 0x15, 0x15,
	0x15, 0x15, 0x3f,
	0x15, 0x3f, 0x15,
	0x15, 0x3f, 0x3f,
	0x3f, 0x15, 0x15,
	0x3f, 0x15, 0x3f,
	0x3f, 0x3f, 0x15,
	0x3f, 0x3f, 0x3f};

#define IO_PORT_COUNT 0x400
#define RAM_SIZE 1024*1024
#define REGS_BASE 0xF0000
#define GRAPHICS_UPDATE_DELAY 360000
#define KEYBOARD_TIMER_UPDATE_DELAY 20000

#define REG_AX 0
#define REG_CX 1
#define REG_DX 2
#define REG_BX 3
#define REG_SP 4
#define REG_BP 5
#define REG_SI 6
#define REG_DI 7
#define REG_ES 8
#define REG_CS 9
#define REG_SS 10
#define REG_DS 11
#define REG_ZERO 12
#define REG_SCRATCH 13
#define REG_AL 0
#define REG_AH 1
#define REG_CL 2
#define REG_CH 3
#define REG_DL 4
#define REG_DH 5
#define REG_BL 6
#define REG_BH 7
#define FLAG_CF 40
#define FLAG_PF 41
#define FLAG_AF 42
#define FLAG_ZF 43
#define FLAG_SF 44
#define FLAG_IF 46
#define FLAG_DF 47
#define FLAG_OF 48

#define TABLE_XLAT_OPCODE 8
#define TABLE_XLAT_SUBFUNCTION 9
#define TABLE_STD_FLAGS 10
#define TABLE_PARITY_FLAG 11
#define TABLE_BASE_INST_SIZE 12
#define TABLE_I_W_SIZE 13
#define TABLE_I_MOD_SIZE 14
#define TABLE_COND_JUMP_DECODE_A 15
#define TABLE_COND_JUMP_DECODE_B 16
#define TABLE_COND_JUMP_DECODE_C 17
#define TABLE_COND_JUMP_DECODE_D 18
#define TABLE_FLAGS_BITFIELDS 19

#define FLAGS_UPDATE_SZP 1
#define FLAGS_UPDATE_AO_ARITH 2
#define FLAGS_UPDATE_OC_LOGIC 4

#define DECODE_RM_REG scratch2_uint = 4 * !i_mod, op_to_addr = rm_addr = i_mod < 3 ? (16 * regs16[seg_override_en ? seg_override : instructions[((scratch2_uint + 3)<<8)+i_rm]] + (unsigned short)( regs16[instructions[((scratch2_uint + 1)<<8)+i_rm]] + instructions[((scratch2_uint + 2)<<8)+i_rm] * i_data1 + regs16[instructions[(scratch2_uint<<8)+i_rm]])) : GET_REG_ADDR(i_rm), op_from_addr = GET_REG_ADDR(i_reg), i_d && (scratch_uint = op_from_addr, op_from_addr = rm_addr, op_to_addr = scratch_uint)

#define GET_REG_ADDR(reg_id) (REGS_BASE + (i_w ? 2 * reg_id : 2 * reg_id + (reg_id >> 2) & 7))

#define TOP_BIT 8*(i_w + 1)

#define R_M_OP(dest,op,src) (i_w ? op_dest = *(unsigned short*)&dest, op_result = *(unsigned short*)&dest op (op_source = *(unsigned short*)&src) : (op_dest = dest, op_result = dest op (op_source = *(unsigned char*)&src)))

#define SIGN_OF(a) (1 & (i_w ? *(short*)&a : a) >> (TOP_BIT - 1))

#define DAA_DAS(op1,op2,mask,min) set_AF((((scratch2_uint = regs8[REG_AL]) & 0x0F) > 9) || regs8[FLAG_AF]) && (op_result = regs8[REG_AL] op1 6, set_CF(regs8[FLAG_CF] || (regs8[REG_AL] op2 scratch2_uint))), set_CF((((mask & 1 ? scratch2_uint : regs8[REG_AL]) & mask) > min) || regs8[FLAG_CF]) && (op_result = regs8[REG_AL] op1 0x60)

unsigned char mem[RAM_SIZE], io_ports[IO_PORT_COUNT], *opcode_stream, *regs8, i_rm, i_w, i_reg, i_mod, i_mod_size, i_d, i_reg4bit, raw_opcode_id, xlat_opcode_id, extra, rep_mode, seg_override_en, rep_override_en, trap_flag, scratch_uchar, io_hi_lo, *vid_mem_base, spkr_en;
unsigned short *regs16, reg_ip, seg_override, file_index, wave_counter;
unsigned int op_source, op_dest, rm_addr, op_to_addr, op_from_addr, i_data0, i_data1, i_data2, scratch_uint, scratch2_uint, inst_counter, set_flags_type, pixel_colors[256], vmem_ctr;
int op_result, scratch_int;
//SDL_AudioSpec sdl_audio = {44100, AUDIO_U8, 1, 0, 128};
SDL_Surface *sdl_screen;
SDL_Color sdl_colors[256];
SDL_Event sdl_event;
unsigned char *disk;

char set_CF(int new_CF)
{
	return regs8[FLAG_CF] = (new_CF!=0);
}

char set_AF(int new_AF)
{
	return regs8[FLAG_AF] = (new_AF!=0);
}

char set_OF(int new_OF)
{
	return regs8[FLAG_OF] = (new_OF!=0);
}

char set_AF_OF_arith()
{
	op_source ^= op_dest ^ op_result;
	set_AF(op_source & 0x10);
	if(op_result == op_dest)
		return set_OF(0);
	else
		return set_OF(1 & (regs8[FLAG_CF] ^ op_source >> (TOP_BIT - 1)));
}

void make_flags()
{
	int i;
	scratch_uint = 0xF002;
	for(i=9; i--;)
		scratch_uint += regs8[FLAG_CF + i] << instructions[(TABLE_FLAGS_BITFIELDS<<8)+i];
}

void set_flags(int new_flags)
{
	int i;
	for(i=9; i--;)
		regs8[FLAG_CF + i] = ((1 << instructions[(TABLE_FLAGS_BITFIELDS<<8)+i] & new_flags)!=0);
}

void set_opcode(unsigned char opcode)
{
	xlat_opcode_id = instructions[(TABLE_XLAT_OPCODE<<8)+(raw_opcode_id = opcode)];
	extra = instructions[(TABLE_XLAT_SUBFUNCTION<<8)+opcode];
	i_mod_size = instructions[(TABLE_I_MOD_SIZE<<8)+opcode];
	set_flags_type = instructions[(TABLE_STD_FLAGS<<8)+opcode];
}

char pc_interrupt(unsigned char interrupt_num)
{
	set_opcode(0xCD);
	make_flags();
	i_w = 1;
	R_M_OP(mem[(regs16[REG_SS]<<4)+--regs16[REG_SP]], =, scratch_uint);
	R_M_OP(mem[(regs16[REG_SS]<<4)+--regs16[REG_SP]], =, regs16[REG_CS]);
	R_M_OP(mem[(regs16[REG_SS]<<4)+--regs16[REG_SP]], =, reg_ip);
	R_M_OP(mem[REGS_BASE+2*REG_CS], =, mem[4*interrupt_num+2]);
	R_M_OP(reg_ip, =, mem[4 * interrupt_num]);

	return regs8[FLAG_IF] = 0;
}

// AAA and AAS instructions - which_operation is +1 for AAA, and -1 for AAS
int AAA_AAS(char which_operation)
{
	return (regs16[REG_AX] += 262 * which_operation*set_AF(set_CF(((regs8[REG_AL] & 0x0F) > 9) || regs8[FLAG_AF])), regs8[REG_AL] &= 0x0F);
}

/*
void audio_callback(void *data, unsigned char *stream, int len)
{
	int i;
	for(i=0; i<len; i++)
		stream[i] = (spkr_en == 3) && CAST(unsigned short)mem[0x4AA] ? -((54 * wave_counter++ / CAST(unsigned short)mem[0x4AA]) & 1) : sdl_audio.silence;
	spkr_en = io_ports[0x61] & 3;
}
*/

int WinMain()
{

	struct timeval t, u;
	double elapsed;
	float pit_tick;
	unsigned char pit, video, skipx;
	unsigned int i, j, k;
	int c, h, s, palidx, palc, msgc = 0;
	FILE *f;

	SDL_Init(SDL_INIT_VIDEO);
	/*
	SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO);
	sdl_audio.callback = audio_callback;
	sdl_audio.samples = 512;
	SDL_OpenAudio(&sdl_audio, 0);
	*/

	sdl_screen = SDL_SetVideoMode(640, 400, 8, 0);
	SDL_EnableUNICODE(1);

	f = fopen("bios", "rb");
	fread(mem + 0xFF000, 4096, 1, f);
	fclose(f);

	disk = (char *)malloc(263725056);
	f = fopen("../DOS_PCem/harddisk.img", "rb");
	fread(disk, 263725056, 1, f);
	fclose(f);

	regs8 = mem + REGS_BASE;
	regs16 = (unsigned short *)regs8;
	regs16[REG_CS] = 0xFFF0;
	reg_ip = 0;

	for(;;)
	{
		opcode_stream = mem + 16 * regs16[REG_CS] + reg_ip;
		set_opcode(*opcode_stream);
		i_reg4bit = raw_opcode_id & 7;
		i_w = i_reg4bit & 1;
		i_d = (i_reg4bit >> 1) & 1;		
		i_data0 = *(short*)&opcode_stream[1];
		i_data1 = *(short*)&opcode_stream[2];
		i_data2 = *(short*)&opcode_stream[3];
		if(seg_override_en) seg_override_en--;
		if(rep_override_en) rep_override_en--;

		if(i_mod_size)
		{
			i_mod = (i_data0 & 0xFF) >> 6;
			i_rm = i_data0 & 7;
			i_reg = (i_data0 >> 3) & 7;

			if((!i_mod && i_rm == 6) || (i_mod == 2)) i_data2 = *(short*)&opcode_stream[4];
			else if(i_mod != 1)	i_data2 = i_data1;
			else i_data1 = (char)i_data1;

			DECODE_RM_REG;
		}

		switch(raw_opcode_id)
		{
			case 0x98: // CBW				
				regs8[REG_AH] = -SIGN_OF(regs8[REG_AL]);
				reg_ip++;
				skipx = 1;
				break;
			case 0x99: // CWD
				regs16[REG_DX] = -SIGN_OF(regs16[REG_AX]);
				reg_ip++;
				skipx = 1;
				break;
			case 0xD7: // XLAT
				if(seg_override_en) regs8[REG_AL] = mem[(regs16[seg_override]<<4)+regs8[REG_AL]+regs16[REG_BX]];
				else regs8[REG_AL] = mem[(regs16[REG_DS]<<4)+regs8[REG_AL]+regs16[REG_BX]];
				reg_ip++;
				skipx = 1;
				break;
			case 0xF5: // CMC
				regs8[FLAG_CF] ^= 1;
				reg_ip++;
				skipx = 1;
				break;
			case 0xF8: // CLC				
				regs8[FLAG_CF] = 0;
				reg_ip++;
				skipx = 1;
				break;			
			case 0xF9: // STC
				regs8[FLAG_CF] = 1;
				reg_ip++;
				skipx = 1;
				break;
			case 0xFA: // CLI
				regs8[FLAG_IF] = 0;
				reg_ip++;
				skipx = 1;
				break;
			case 0xFB: // STI
				regs8[FLAG_IF] = 1;
				reg_ip++;
				skipx = 1;
				break;
			case 0xFC: // CLD
				regs8[FLAG_DF] = 0;
				reg_ip++;
				skipx = 1;
				break;
			case 0xFD: // STD
				regs8[FLAG_DF] = 1;
				reg_ip++;
				skipx = 1;
				break;
			default:
				skipx = 0;
		}
		
		if(!skipx) {
		switch(xlat_opcode_id)
		{
			case 0:				
				scratch_uchar = (raw_opcode_id >> 1) & 7;
				reg_ip += (char)i_data0 * (i_w ^ (regs8[instructions[(TABLE_COND_JUMP_DECODE_A<<8)+scratch_uchar]] || regs8[instructions[(TABLE_COND_JUMP_DECODE_B<<8)+scratch_uchar]] || regs8[instructions[(TABLE_COND_JUMP_DECODE_C<<8)+scratch_uchar]] ^ regs8[instructions[(TABLE_COND_JUMP_DECODE_D<<8)+scratch_uchar]]));
				break;
			case 1: // MOV reg, imm
				i_w = ((raw_opcode_id & 8)!=0);
				R_M_OP(mem[GET_REG_ADDR(i_reg4bit)], =, i_data0);
				break;
			case 2: // INC|DEC regs16
				i_w = 1;
				i_d = 0;
				i_reg = i_reg4bit;
				DECODE_RM_REG;
				i_reg = extra;
				if(i_reg < 2)
				{
					R_M_OP(mem[op_from_addr], +=1-2*i_reg+, mem[REGS_BASE+2*REG_ZERO]);
					op_source = 1;
					set_AF_OF_arith();
					set_OF(op_dest + 1 - i_reg == 1 << (TOP_BIT - 1));
					if(xlat_opcode_id == 5) set_opcode(0x10);
				}
				else if(i_reg != 6)
				{
					if(!(i_reg - 3))
					{
						i_w = 1;
						R_M_OP(mem[(regs16[REG_SS]<<4)+--regs16[REG_SP]], =, regs16[REG_CS]);
					}											
					if(i_reg & 2)
					{
						i_w = 1;
						R_M_OP(mem[(regs16[REG_SS]<<4)+--regs16[REG_SP]], =, reg_ip + 2 + i_mod*(i_mod != 3) + 2*(!i_mod && i_rm == 6));
					}
					if(i_reg & 1) regs16[REG_CS] = *(short*)&mem[op_from_addr + 2];
					R_M_OP(reg_ip, =, mem[op_from_addr]);
					set_opcode(0x9A);
				}
				else
				{
					i_w = 1;
					R_M_OP(mem[(regs16[REG_SS]<<4)+--regs16[REG_SP]], =, mem[rm_addr]);
				}
				break;
			case 3: // PUSH regs16
				i_w = 1;
				R_M_OP(mem[(regs16[REG_SS]<<4)+--regs16[REG_SP]], =, regs16[i_reg4bit]);
				break;
			case 4: // POP regs16
				i_w = 1;
				regs16[REG_SP] += 2;
				R_M_OP(regs16[i_reg4bit], =, mem[(regs16[REG_SS]<<4)-2+regs16[REG_SP]]);
				break;
			case 5: // INC|DEC|JMP|CALL|PUSH
				if(i_reg < 2)
				{
					R_M_OP(mem[op_from_addr], +=1-2*i_reg+, mem[REGS_BASE+2*REG_ZERO]);
					op_source = 1;
					set_AF_OF_arith();
					set_OF(op_dest + 1 - i_reg == 1 << (TOP_BIT - 1));
					if(xlat_opcode_id == 5) set_opcode(0x10);
				}
				else if(i_reg != 6)
				{
					if(!(i_reg - 3))
					{
						i_w = 1;
						R_M_OP(mem[(regs16[REG_SS]<<4)+--regs16[REG_SP]], =, regs16[REG_CS]);
					}											
					if(i_reg & 2)
					{
						i_w = 1;
						R_M_OP(mem[(regs16[REG_SS]<<4)+--regs16[REG_SP]], =, reg_ip + 2 + i_mod*(i_mod != 3) + 2*(!i_mod && i_rm == 6));
					}
					if(i_reg & 1) regs16[REG_CS] = *(short*)&mem[op_from_addr + 2];
					R_M_OP(reg_ip, =, mem[op_from_addr]);
					set_opcode(0x9A);
				}
				else
				{
					i_w = 1;
					R_M_OP(mem[(regs16[REG_SS]<<4)+--regs16[REG_SP]], =, mem[rm_addr]);
				}
				break;
			case 6: // TEST r/m, imm16 / NOT|NEG|MUL|IMUL|DIV|IDIV reg
				op_to_addr = op_from_addr;
				if(i_reg==0)
				{
					set_opcode(0x20);
					reg_ip += i_w + 1;
					R_M_OP(mem[op_to_addr], &, i_data2);
				} else
				if(i_reg==2)
				{
					R_M_OP(mem[op_to_addr],=~,mem[op_from_addr]);
				} else
				if(i_reg==3)
				{
					R_M_OP(mem[op_to_addr],=-,mem[op_from_addr]);
					op_dest = 0;
					set_opcode(0x28);
					set_CF(op_result > op_dest);
				} else
				if(i_reg==4) // MUL
				{
					if(i_w)
					{
						set_opcode(0x10);
						op_result = *(unsigned short*)&mem[rm_addr] * (unsigned short)*regs16;
						regs16[i_w + 1] = op_result >> 16;
						regs16[REG_AX] = op_result;
						set_OF(set_CF(op_result - (unsigned short)op_result));
					}
					else
					{
						set_opcode(0x10);
						op_result = *(unsigned char*)&mem[rm_addr] * (unsigned char)*regs8;
						regs8[i_w + 1] = op_result >> 16;
						regs16[REG_AX] = op_result;
						set_OF(set_CF(op_result - (unsigned char)op_result));
					}
				} else
				if(i_reg==5) // IMUL
				{
					if(i_w)
					{
						set_opcode(0x10);
						op_result = *(short*)&mem[rm_addr] * (short)*regs16;
						regs16[i_w + 1] = op_result >> 16;
						regs16[REG_AX] = op_result;
						set_OF(set_CF(op_result - (short)op_result));
					}
					else
					{
						set_opcode(0x10);
						op_result = *(char*)&mem[rm_addr] * (char)*regs8;
						regs8[i_w + 1] = op_result >> 16;
						regs16[REG_AX] = op_result;
						set_OF(set_CF(op_result - (char)op_result));
					}
				} else
				if(i_reg==6) // DIV
				{
					if(i_w)
					{
						scratch_int = *(unsigned short*)&mem[rm_addr];
						if(scratch_int)
							!(scratch2_uint = (unsigned)(scratch_uint = (regs16[i_w+1] << 16) + regs16[REG_AX]) / scratch_int, scratch2_uint - (unsigned short)scratch2_uint) ? regs16[i_w+1] = scratch_uint - scratch_int * (*regs16 = scratch2_uint) : pc_interrupt(0);
					} else {
						scratch_int = *(unsigned char*)&mem[rm_addr];
						if(scratch_int)
							!(scratch2_uint = (unsigned short)(scratch_uint = (regs8[i_w+1] << 16) + regs16[REG_AX]) / scratch_int, scratch2_uint - (unsigned char)scratch2_uint) ? regs8[i_w+1] = scratch_uint - scratch_int * (*regs8 = scratch2_uint) : pc_interrupt(0);
					}
				} else
				if(i_reg==7) // IDIV
				{
					if(i_w)
					{
						scratch_int = *(short*)&mem[rm_addr];
						if(scratch_int)
							!(scratch2_uint = (int)(scratch_uint = (regs16[i_w+1] << 16) + regs16[REG_AX]) / scratch_int, scratch2_uint - (short)scratch2_uint) ? regs16[i_w+1] = scratch_uint - scratch_int * (*regs16 = scratch2_uint) : pc_interrupt(0);
					} else {
						scratch_int = *(char*)&mem[rm_addr];
						if(scratch_int)
							!(scratch2_uint = (short)(scratch_uint = (regs8[i_w+1] << 16) + regs16[REG_AX]) / scratch_int, scratch2_uint - (char)scratch2_uint) ? regs8[i_w+1] = scratch_uint - scratch_int * (*regs8 = scratch2_uint) : pc_interrupt(0);
					}
				}
				break;
			case 7: // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP AL/AX, immed
				rm_addr = REGS_BASE;
				i_data2 = i_data0;
				i_mod = 3;
				i_reg = extra;
				reg_ip--;
			case 8: // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP reg, immed
				op_to_addr = rm_addr;
				regs16[REG_SCRATCH] = (i_d |= !i_w) ? (char)i_data2 : i_data2;
				op_from_addr = REGS_BASE + 2 * REG_SCRATCH;
				reg_ip += !i_d + 1;
				set_opcode(0x08 * (extra = i_reg));
			case 9: // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP|MOV reg, r/m
				if(extra==0)
				{
					R_M_OP(mem[op_to_addr],+=,mem[op_from_addr]);
					set_CF(op_result < op_dest);
				} else
				if(extra==1)
				{
					R_M_OP(mem[op_to_addr],|=,mem[op_from_addr]);
				} else
				if(extra==2)
				{
					R_M_OP(mem[op_to_addr],+= regs8[FLAG_CF] +, mem[op_from_addr]);
					set_CF(regs8[FLAG_CF] && (op_result == op_dest) || (op_result < (int)op_dest));
					set_AF_OF_arith();
				} else
				if(extra==3)
				{
					R_M_OP(mem[op_to_addr],-= regs8[FLAG_CF] +, mem[op_from_addr]);
					set_CF(regs8[FLAG_CF] && (op_result == op_dest) || (- op_result < -(int)op_dest));
					set_AF_OF_arith();
				} else
				if(extra==4)
				{
					R_M_OP(mem[op_to_addr],&=,mem[op_from_addr]);
				} else
				if(extra==5)
				{
					R_M_OP(mem[op_to_addr],-=,mem[op_from_addr]);
					set_CF(op_result > op_dest);
				} else
				if(extra==6)
				{
					R_M_OP(mem[op_to_addr],^=,mem[op_from_addr]);
				} else
				if(extra==7)
				{
					R_M_OP(mem[op_to_addr],-,mem[op_from_addr]);	
					set_CF(op_result > op_dest);
				} else
				if(extra==8)
				{
					R_M_OP(mem[op_to_addr],=,mem[op_from_addr]);
				}
				break;
			case 10: // MOV sreg, r/m | POP r/m | LEA reg, r/m
				if(!i_w) // MOV
				{
					i_w = 1;
					i_reg += 8;
					DECODE_RM_REG;
					R_M_OP(mem[op_to_addr],=,mem[op_from_addr]);
				} else
				if(!i_d) // LEA
				{
					seg_override_en = 1;
					seg_override = REG_ZERO;
					DECODE_RM_REG;
					R_M_OP(mem[op_from_addr], =, rm_addr);
				}
				else // POP
				{
					i_w = 1;
					regs16[REG_SP] += 2;
					R_M_OP(mem[rm_addr], =, mem[(regs16[REG_SS]<<4)-2+regs16[REG_SP]]);
				}
				break;
			case 11: // MOV AL/AX, [loc]
				i_mod = i_reg = 0;
				i_rm = 6;
				i_data1 = i_data0;
				DECODE_RM_REG;
				R_M_OP(mem[op_from_addr],=,mem[op_to_addr]);
				break;
			case 12: // ROL|ROR|RCL|RCR|SHL|SHR|???|SAR reg/mem, 1/CL/imm (80186)
				scratch2_uint = SIGN_OF(mem[rm_addr]);
				if(extra)
				{
					++reg_ip;
					scratch_uint = (char)i_data1;
				} else
				{
					if(i_d) scratch_uint = 31 & regs8[REG_CL];
					else scratch_uint = 1;
				}
				if(scratch_uint)
				{
					if(i_reg < 4)
					{
						scratch_uint %= (i_reg >> 1) + TOP_BIT;
						R_M_OP(scratch2_uint, =, mem[rm_addr]);
					}
					if(i_reg & 1) R_M_OP(mem[rm_addr], >>=, scratch_uint);
					else R_M_OP(mem[rm_addr], <<=, scratch_uint);
					if(i_reg > 3) set_opcode(0x10);
					if(i_reg > 4) set_CF(op_dest >> (scratch_uint - 1) & 1);
				}
				if(i_reg==0) // ROL
				{
					R_M_OP(mem[rm_addr], += , scratch2_uint >> (TOP_BIT - scratch_uint));
					set_OF(SIGN_OF(op_result) ^ set_CF(op_result & 1));
				} else
				if(i_reg==1) // ROR
				{
					scratch2_uint &= (1 << scratch_uint) - 1;
					R_M_OP(mem[rm_addr], += , scratch2_uint << (TOP_BIT - scratch_uint));
					set_OF(SIGN_OF(op_result * 2) ^ set_CF(SIGN_OF(op_result)));
				} else
				if(i_reg==2) // RCL
				{
					R_M_OP(mem[rm_addr], += (regs8[FLAG_CF] << (scratch_uint - 1)) + , scratch2_uint >> (1 + TOP_BIT - scratch_uint));
					set_OF(SIGN_OF(op_result) ^ set_CF(scratch2_uint & 1 << (TOP_BIT - scratch_uint)));
				} else
				if(i_reg==3) // RCR
				{
					R_M_OP(mem[rm_addr], += (regs8[FLAG_CF] << (TOP_BIT - scratch_uint)) + , scratch2_uint << (1 + TOP_BIT - scratch_uint));
					set_CF(scratch2_uint & 1 << (scratch_uint - 1));
					set_OF(SIGN_OF(op_result) ^ SIGN_OF(op_result * 2));
				} else
				if(i_reg==4) // SHL
				{
					set_OF(SIGN_OF(op_result) ^ set_CF(SIGN_OF(op_dest << (scratch_uint - 1))));
				} else
				if(i_reg==5) // SHR
				{
					set_OF(SIGN_OF(op_dest));
				} else
				if(i_reg==7) // SAR
				{
					if(!(scratch_uint < TOP_BIT)) set_CF(scratch2_uint);
					set_OF(0);
					R_M_OP(mem[rm_addr], +=, scratch2_uint *= ~(((1 << TOP_BIT) - 1) >> scratch_uint));
				}
				break;
			case 13: // LOOPxx|JCZX
				regs16[REG_CX]--;
				scratch_uint = (regs16[REG_CX]!=0);
				if(i_reg4bit==0) scratch_uint &= !regs8[FLAG_ZF];
				else if(i_reg4bit==1) scratch_uint &= regs8[FLAG_ZF];
				else if(i_reg4bit==3) scratch_uint = !++regs16[REG_CX];
				reg_ip += scratch_uint*(char)i_data0;
				break;
			case 14: // JMP | CALL short/near
				reg_ip += 3 - i_d;
				if(!i_w)
				{
					if(i_d) // JMP far
					{
						reg_ip = 0;
						regs16[REG_CS] = i_data2;
					}
					else // CALL
					{
						i_w = 1;
						R_M_OP(mem[(regs16[REG_SS]<<4)+--regs16[REG_SP]], =, reg_ip);
					}
				}
				if(i_d && i_w) reg_ip += (char)i_data0;
				else reg_ip += i_data0;
				break;
			case 15: // TEST reg, r/m
				R_M_OP(mem[op_from_addr],&,mem[op_to_addr]);
				break;
			case 16: // XCHG AX, regs16
				i_w = 1;
				op_to_addr = REGS_BASE;
				op_from_addr = GET_REG_ADDR(i_reg4bit);
				if(op_to_addr != op_from_addr)
				{
					R_M_OP(mem[op_to_addr],^=,mem[op_from_addr]);
					R_M_OP(mem[op_from_addr],^=,mem[op_to_addr]);
					R_M_OP(mem[op_to_addr],^=,mem[op_from_addr]);
				}
				break;
			case 17: // MOVSx (extra=0)|STOSx (extra=1)|LODSx (extra=2)
				if(seg_override_en)	scratch2_uint = seg_override;
				else scratch2_uint = REG_DS;
				if(rep_override_en)	scratch_uint = regs16[REG_CX];
				else scratch_uint = 1;
				while(scratch_uint>0)
				{
					scratch_uint--;
					if(extra < 2)
						i = (regs16[REG_ES]<<4)+regs16[REG_DI];
					else
						i = REGS_BASE;
					if(extra & 1)
						j = REGS_BASE;
					else
						j = (regs16[scratch2_uint]<<4)+regs16[REG_SI];
					R_M_OP(mem[i], =, mem[j]);
					if(!(extra & 1)) regs16[REG_SI] -= ((regs8[FLAG_DF] << 1) - 1)*(i_w + 1);
					if(!(extra & 2)) regs16[REG_DI] -= ((regs8[FLAG_DF] << 1) - 1)*(i_w + 1);
				}

				if(rep_override_en) regs16[REG_CX] = 0;
				break;
			case 18: // CMPSx (extra=0)|SCASx (extra=1)
				if(seg_override_en) scratch2_uint = seg_override;
				else scratch2_uint = REG_DS;
				if(rep_override_en) scratch_uint = regs16[REG_CX];
				else scratch_uint = 1;
				if(scratch_uint)
				{
					while(scratch_uint>0)
					{
						if(!rep_override_en) scratch_uint--;
						if(extra) R_M_OP(mem[REGS_BASE], -, mem[(regs16[REG_ES]<<4)+regs16[REG_DI]]);
						else R_M_OP(mem[(regs16[scratch2_uint]<<4)+regs16[REG_SI]], -, mem[(regs16[REG_ES]<<4)+regs16[REG_DI]]);
						if(!extra) regs16[REG_SI] -= (2 * regs8[FLAG_DF] - 1)*(i_w + 1);
						regs16[REG_DI] -= (2 * regs8[FLAG_DF] - 1)*(i_w + 1);
						if(rep_override_en && !(--regs16[REG_CX] && (!op_result == rep_mode))) scratch_uint = 0;
					}
					set_flags_type = FLAGS_UPDATE_SZP | FLAGS_UPDATE_AO_ARITH;
					set_CF(op_result > op_dest);
				}
				break;
			case 19: // RET|RETF|IRET
				i_d = i_w;
				i_w = 1;
				regs16[REG_SP] += 2;
				R_M_OP(reg_ip, =, mem[(regs16[REG_SS]<<4)-2+regs16[REG_SP]]);				
				if(extra)
				{
					i_w = 1;
					regs16[REG_SP] += 2;
					R_M_OP(regs16[REG_CS], =, mem[(regs16[REG_SS]<<4)-2+regs16[REG_SP]]);
				}
				if(extra & 2)
				{
					i_w = 1;
					regs16[REG_SP] += 2;
					set_flags(R_M_OP(scratch_uint, =, mem[(regs16[REG_SS]<<4)-2+regs16[REG_SP]]));
				}
				else if(!i_d) regs16[REG_SP] += i_data0;
				break;
			case 20: // MOV r/m, immed
				R_M_OP(mem[op_from_addr], =, i_data2);
				break;
			case 21: // IN AL/AX, DX/imm8
				if(extra)
					scratch_uint = regs16[REG_DX];
				else
					scratch_uint = (unsigned char)i_data0;
				switch(scratch_uint)
				{
					case 0x60: // KBD
						break;
					case 0x3c9: // VGA
						if(palc == 0) io_ports[0x3c9] = sdl_colors[palidx].r >> 2;
						if(palc == 1) io_ports[0x3c9] = sdl_colors[palidx].g >> 2;
						if(palc == 2) io_ports[0x3c9] = sdl_colors[palidx++].b >> 2;
						if(++palc == 3) palc = 0;
						break;
					case 0x388: // ADLIB
						break;
					case 0x3da: // VGA
						io_ports[0x3DA] ^= 9;
						break;
					default:
						//printf("%.8d Unhandled inb(%.4x)\n", msgc++, scratch_uint);
						break;
				}
				R_M_OP(regs8[REG_AL], =, io_ports[scratch_uint]);
				break;
			case 22: // OUT DX/imm8, AL/AX
				if(extra)
					scratch_uint = regs16[REG_DX];
				else
					scratch_uint = (unsigned char)i_data0;
				R_M_OP(io_ports[scratch_uint], =, regs8[REG_AL]);
				switch(scratch_uint)
				{
					case 0x20:
					case 0x21:
						break;
					case 0x40:
						if(pit == 0) pit_tick = 1.0*regs8[REG_AL];
						if(pit++)
						{
							pit_tick += 256.0*regs8[REG_AL];
							if(pit_tick == 0.0) pit_tick = 65536.0/1193.18;
							else pit_tick = pit_tick/1193.18;
						}						
						break;
					case 0x43:
						pit = 0;
						break;
					case 0x388:
						break;
					case 0x389:
						break;
					case 0x3c7:
					case 0x3c8:
						palidx = regs8[REG_AL];
						palc = 0;
						break;
					case 0x3c9:
						if(palc == 0) sdl_colors[palidx].r = regs8[REG_AL] << 2;
						if(palc == 1) sdl_colors[palidx].g = regs8[REG_AL] << 2;
						if(palc == 2)
						{
							sdl_colors[palidx++].b = regs8[REG_AL] << 2;
							SDL_SetColors(sdl_screen, sdl_colors, 0, 256);
						}
						if(++palc == 3) palc = 0;
						break;
					default:
						//printf("%.8d Unhandled outb(%.4x)=%.2x\n", msgc++, scratch_uint, regs8[REG_AL]);
						break;
				}
				break;
			case 23: // REPxx
				rep_override_en = 2;
				rep_mode = i_w;
				if(seg_override_en) seg_override_en++;
				break;
			case 24: // NOP|XCHG reg, r/m
				if(op_to_addr != op_from_addr)
				{
					R_M_OP(mem[op_to_addr],^=,mem[op_from_addr]);
					R_M_OP(mem[op_from_addr],^=,mem[op_to_addr]);
					R_M_OP(mem[op_to_addr],^=,mem[op_from_addr]);
				}
				break;
			case 25: // PUSH reg
				i_w = 1;
				R_M_OP(mem[(regs16[REG_SS]<<4)+--regs16[REG_SP]], =, regs16[extra]);
				break;
			case 26: // POP reg
				i_w = 1;
				regs16[REG_SP] += 2;
				R_M_OP(regs16[extra], =, mem[(regs16[REG_SS]<<4)-2+regs16[REG_SP]]);
				break;
			case 27:
				seg_override_en = 2;
				seg_override = extra;
				if(rep_override_en) rep_override_en++;				
				break;
			case 28: // DAA/DAS
				i_w = 0;
				if(extra)
					DAA_DAS(-=, >=, 0xFF, 0x99);
				else
					DAA_DAS(+=, <, 0xF0, 0x90);
				break;
			case 29: // AAA/AAS
				op_result = AAA_AAS(extra - 1);
				break;
			//// 30 CBW
			//// 31 CWD
			case 32: // CALL FAR imm16:imm16
				i_w = 1;
				R_M_OP(mem[(regs16[REG_SS]<<4)+--regs16[REG_SP]], =, regs16[REG_CS]);
				R_M_OP(mem[(regs16[REG_SS]<<4)+--regs16[REG_SP]], =, reg_ip + 5);
				regs16[REG_CS] = i_data2;
				reg_ip = i_data0;
				break;
			case 33: // PUSHF
				make_flags();
				i_w = 1;
				R_M_OP(mem[(regs16[REG_SS]<<4)+--regs16[REG_SP]], =, scratch_uint);
				break;
			case 34: // POPF
				i_w = 1;
				regs16[REG_SP] += 2;
				set_flags(R_M_OP(scratch_uint, =, mem[(regs16[REG_SS]<<4)-2+regs16[REG_SP]]));
				break;
			case 35: // SAHF
				make_flags();
				set_flags((scratch_uint & 0xFF00) + regs8[REG_AH]);
				break;
			case 36: // LAHF
				make_flags();
				regs8[REG_AH] = scratch_uint;
				break;
			case 37: // LES|LDS reg, r/m
				i_d = 1;
				i_w = 1;
				DECODE_RM_REG;
				R_M_OP(mem[op_to_addr], =, mem[op_from_addr]);
				R_M_OP(mem[REGS_BASE+extra], =, mem[rm_addr+2]);
				break;
			case 38: // INT 3
				++reg_ip;
				pc_interrupt(3);
				break;
			case 39: // INT imm8
				reg_ip += 2;
				pc_interrupt(i_data0);
				break;
			case 40: // INTO
				++reg_ip;
				regs8[FLAG_OF] && pc_interrupt(4);
				break;
			case 41: // AAM
				if(i_data0 &= 0xFF)
				{
					regs8[REG_AH] = regs8[REG_AL] / i_data0;
					op_result = regs8[REG_AL] %= i_data0;
				}
				else
					pc_interrupt(0);
				break;
			case 42: // AAD
				i_w = 0;
				regs16[REG_AX] = op_result = 0xFF & regs8[REG_AL] + i_data0 * regs8[REG_AH];
				break;
			//// 44 XLAT
			//// 45 CMC
			//// 46 CLC|STC|CLI|STI|CLD|STD
			case 47: // TEST AL/AX, immed
				R_M_OP(regs8[REG_AL], &, i_data0);
				break;
			case 48:
				switch((char)i_data0)
				{
					case 0:
						printf("%c", regs8[REG_AL]);
						break;
					case 1:
						c = ((regs8[REG_CL]&0xC0)<<2)|regs8[REG_CH];
						h = regs8[REG_DH];
						s = regs8[REG_CL]&63;
						j = 512*(63*(16*c+h)+s-1); // CHS: 511/16/63
						for(i=0; i<512*regs8[REG_AL]; i++)
							disk[j + i] = mem[(regs16[REG_ES] << 4) + regs16[REG_BX] + i];
						break;
					case 2:
						c = ((regs8[REG_CL]&0xC0)<<2)|regs8[REG_CH];
						h = regs8[REG_DH];
						s = regs8[REG_CL]&63;
						j = 512*(63*(16*c+h)+s-1); // CHS: 511/16/63
						for(i=0; i<512*regs8[REG_AL]; i++)
							mem[(regs16[REG_ES] << 4) + regs16[REG_BX] + i] = disk[j + i];
						break;
					case 3:
						video = regs8[REG_AL];
						switch(video)
						{
							case 0x13:
								for(i=0; i<16; i++)
								{
									sdl_colors[i].r = vgapal[3*i+0] << 2;
									sdl_colors[i].g = vgapal[3*i+1] << 2;
									sdl_colors[i].b = vgapal[3*i+2] << 2;
								}			
								SDL_SetColors(sdl_screen, sdl_colors, 0, 256);
								break;
							default:
								//printf("%.8d Unhandled video mode: %.2x\n", msgc++, video);
								break;
						}
						break;
					case 4:
						if(regs8[REG_AH]!=0x0e)
							//printf("%.8d Unhandled video request: ah=%.2x, al=%.2x\n", msgc++, regs8[REG_AH], regs8[REG_AL]);
						break;
					case 5:
						SDL_Quit();
						exit(0);
						break;
				}
				break;
			default:
				//printf("%.8d Unhandled opcode: %d\n", msgc++, xlat_opcode_id);
				break;
		}

		reg_ip += (i_mod*(i_mod != 3) + 2*(!i_mod && i_rm == 6))*i_mod_size + instructions[(TABLE_BASE_INST_SIZE<<8)+raw_opcode_id] + instructions[(TABLE_I_W_SIZE<<8)+raw_opcode_id]*(i_w + 1);

		if(set_flags_type & FLAGS_UPDATE_SZP)
		{
			regs8[FLAG_SF] = SIGN_OF(op_result);
			regs8[FLAG_ZF] = !op_result;
			regs8[FLAG_PF] = instructions[(unsigned char)op_result + (TABLE_PARITY_FLAG<<8)];
			if(set_flags_type & FLAGS_UPDATE_AO_ARITH) set_AF_OF_arith();
			if(set_flags_type & FLAGS_UPDATE_OC_LOGIC)
			{
				set_CF(0);
				set_OF(0);
			}
		}
	}
	skipx = 0;
	
		if(!(inst_counter % GRAPHICS_UPDATE_DELAY))
		{
			if(video == 0x0d)
			{
			}
			if(video == 0x13)
				for(j=0; j<400; j++)
					for(i=0; i<640; i++)
						((unsigned char*)sdl_screen->pixels)[i+j*640] = mem[0xA0000+(i>>1)+(j>>1)*320];
			SDL_Flip(sdl_screen);
			SDL_PumpEvents();
		}

		if(!seg_override_en && !rep_override_en && regs8[FLAG_IF])
		{
			gettimeofday(&u, 0);
			elapsed = (u.tv_sec - t.tv_sec)*1000.0 + (u.tv_usec - t.tv_usec)*0.001;
			if( elapsed > pit_tick )
			{
				pc_interrupt(8);
				t = u;
			}
		}

		if(!(++inst_counter % 20000) && !seg_override_en && !rep_override_en && regs8[FLAG_IF])
		{
			if(SDL_PollEvent(&sdl_event))
			{
				if(sdl_event.type == SDL_KEYDOWN) io_ports[0x60] = sdl_event.key.keysym.scancode;
				if(sdl_event.type == SDL_KEYUP) io_ports[0x60] = sdl_event.key.keysym.scancode|0x80;
				pc_interrupt(9);
			}
		}
	}
	SDL_Quit();
	return 0;
}