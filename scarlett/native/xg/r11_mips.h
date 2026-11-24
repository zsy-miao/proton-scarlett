#pragma once
/* Byte boundary after n smallest levels; both measured chains end at 80x45. */
enum { R11_MIPS=6, R11_ALIGNMENT=256 };
static const unsigned r11_end[R11_MIPS+1]={0,0x3c00,0x12c00,0x4c400,0x12d400,0x4b1400,0x12c1400};
static inline unsigned r11_mip_byte(unsigned levels,unsigned mip,unsigned x,unsigned y)
{
    if (levels!=5 && levels!=6) return ~0u;
    if (mip>=levels) return r11_end[levels];
    unsigned full_mip=R11_MIPS-levels+mip;
    if (x>=(2560u>>full_mip) || y>=(1440u>>full_mip)) return r11_end[levels];
    unsigned tile=(y/8)*(320u>>full_mip)+x/8;
    unsigned word=(x&3)|((y&3)<<2)|((x&4)<<2)|((y&4)<<3);
    return r11_end[levels-mip-1]+tile*256+word*4;
}
