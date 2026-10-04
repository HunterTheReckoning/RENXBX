// cgc version 3.1.0013, build date Apr 24 2012
// command line args: -profile vp20
// source file: xbox_ffp.vs.cg
//vendor NVIDIA Corporation
//version 3.1.0.13
//profile vp20
//program main
//semantic main.m_screen
//semantic main.m_world
//semantic main.mat_diffuse
//semantic main.mat_ambient
//semantic main.mat_emissive
//semantic main.ambient_global
//semantic main.flags
//semantic main.flags2
//semantic main.light_dir
//semantic main.light_diffuse
//semantic main.texsel
//var float4 I.pos : $vin.POSITION : ATTR0 : 0 : 1
//var float4 I.norm : $vin.NORMAL : ATTR2 : 0 : 1
//var float4 I.diff : $vin.COLOR0 : ATTR3 : 0 : 1
//var float4 I.spec : $vin.COLOR1 : ATTR4 : 0 : 1
//var float4 I.tex0 : $vin.TEXCOORD0 : TEXCOORD0 : 0 : 1
//var float4 I.tex1 : $vin.TEXCOORD1 : TEXCOORD1 : 0 : 1
//var float4x4 m_screen :  : c[0], 4 : 1 : 1
//var float4x4 m_world :  : c[4], 4 : 2 : 1
//var float4 mat_diffuse :  : c[8] : 3 : 1
//var float4 mat_ambient :  : c[9] : 4 : 1
//var float4 mat_emissive :  : c[10] : 5 : 1
//var float4 ambient_global :  : c[11] : 6 : 1
//var float4 flags :  : c[12] : 7 : 1
//var float4 flags2 :  : c[13] : 8 : 1
//var float4 light_dir[0] :  : c[14] : 9 : 1
//var float4 light_dir[1] :  : c[15] : 9 : 1
//var float4 light_dir[2] :  : c[16] : 9 : 1
//var float4 light_dir[3] :  : c[17] : 9 : 1
//var float4 light_diffuse[0] :  : c[18] : 10 : 1
//var float4 light_diffuse[1] :  : c[19] : 10 : 1
//var float4 light_diffuse[2] :  : c[20] : 10 : 1
//var float4 light_diffuse[3] :  : c[21] : 10 : 1
//var float4 texsel :  : c[22] : 11 : 1
//var float4 main.pos : $vout.POSITION : HPOS : -1 : 1
//var float4 main.col0 : $vout.COLOR0 : COL0 : -1 : 1
//var float4 main.col1 : $vout.COLOR1 : COL1 : -1 : 1
//var float4 main.tex0 : $vout.TEXCOORD0 : TEX0 : -1 : 1
//var float4 main.tex1 : $vout.TEXCOORD1 : TEX1 : -1 : 1
//const c[23] = 1 0
// 53 instructions, 0 R-regs
0x00000000, 0x006ee61b, 0x08361400, 0x3f0007f8,
0x00000000, 0x004da01b, 0x0400186c, 0x2f0007f8,
0x00000000, 0x006ee01b, 0x04361000, 0x3f1007f8,
0x00000000, 0x006d201b, 0x1436146c, 0x3e0007f8,
0x00000000, 0x006d401b, 0x1436146c, 0x3e3007f8,
0x00000000, 0x004d801b, 0x0554186c, 0x2e0007f8,
0x00000000, 0x004ca455, 0x0836186c, 0x2e2007f8,
0x00000000, 0x008c8400, 0x0836186c, 0x9e2007f8,
0x00000000, 0x004d801b, 0x35fe186c, 0x2e3007f8,
0x00000000, 0x008cc4aa, 0x0836186c, 0x9e2007f8,
0x00000000, 0x006ee01b, 0x24361154, 0x3e2007f8,
0x00000000, 0x00a0001b, 0x2436486c, 0x212007f8,
0x00000000, 0x0800001b, 0x08361300, 0x902107f8,
0x00000000, 0x004000ff, 0x2436486c, 0x2e2007f8,
0x00000000, 0x00adc01b, 0x2636186c, 0x212007f8,
0x00000000, 0x006d201b, 0x0436106c, 0x3e0007f8,
0x00000000, 0x006d401b, 0x3436106c, 0x3e3007f8,
0x00000000, 0x008d601b, 0x0436186c, 0xde4007f8,
0x00000000, 0x006d001b, 0x1436146c, 0x3f0007f8,
0x00000000, 0x004d801b, 0x04aa186c, 0x2f0007f8,
0x00000000, 0x006d001b, 0x0436106c, 0x3f0007f8,
0x00000000, 0x004e401b, 0x0436186c, 0x2e3007f8,
0x00000000, 0x014ee01b, 0x24aa186c, 0x212007f8,
0x00000000, 0x0080001b, 0x35fe486d, 0x1e4007f8,
0x00000000, 0x00ade01b, 0x2636186c, 0x212007f8,
0x00000000, 0x004e601b, 0x0436186c, 0x2e3007f8,
0x00000000, 0x014ee01b, 0x24aa186c, 0x212007f8,
0x00000000, 0x0080001b, 0x35fe486d, 0x1e4007f8,
0x00000000, 0x00ae001b, 0x2636186c, 0x212007f8,
0x00000000, 0x00ae201b, 0x2636186c, 0x282007f8,
0x00000000, 0x004e801b, 0x0436186c, 0x2e3007f8,
0x00000000, 0x014ee01b, 0x24aa186c, 0x212007f8,
0x00000000, 0x0080001b, 0x35fe486d, 0x1e3007f8,
0x00000000, 0x014ee01b, 0x24aa186c, 0x282007f8,
0x00000000, 0x004ea01b, 0x0436186c, 0x2e0007f8,
0x00000000, 0x0080001b, 0x0400486c, 0xde0007f8,
0x00000000, 0x004c2055, 0x0836186c, 0x2f2007f8,
0x00000000, 0x012ee01b, 0x0400186c, 0x2e0007f8,
0x00000000, 0x008c0000, 0x0836186c, 0x9f2007f8,
0x00000000, 0x014ee01b, 0x04aa186c, 0x2e0007f8,
0x00000000, 0x0060001b, 0x0436146c, 0x5f0007f8,
0x00000000, 0x008d801b, 0x0400186c, 0x5070f818,
0x00000000, 0x008c40aa, 0x0836186c, 0x9f2007f8,
0x00000000, 0x006c601b, 0x2436106c, 0x3f2007f8,
0x00000000, 0x006ee1ff, 0x2436106c, 0x380007f8,
0x00000000, 0x0400001b, 0x08361300, 0x903807f8,
0x00000000, 0x0020121b, 0x0836106c, 0x2f1007f8,
0x00000000, 0x008da000, 0x0554186c, 0x90701800,
0x00000000, 0x0060141b, 0x0836146c, 0x5f0007f8,
0x00000000, 0x0040001b, 0x2400686c, 0x2070e800,
0x00000000, 0x004da81b, 0x08aa186c, 0x2070f820,
0x00000000, 0x008ed21b, 0x0400186c, 0x2070f848,
0x00000000, 0x008ed21b, 0x04aa186c, 0x2070f851,
