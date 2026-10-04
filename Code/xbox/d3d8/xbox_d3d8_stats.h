/*
** xbox_d3d8_stats.h -- live-object counters for the port's Direct3D layer (Xbox build only).
** For test programs: if these keep growing while a scene is unchanged, something is leaking
** Direct3D objects (the engine, or the layer's own reference counting).
*/
#ifndef XBOX_D3D8_STATS_H
#define XBOX_D3D8_STATS_H

struct XboxD3DStats {
	unsigned textures, texture_bytes;
	unsigned vertex_buffers, vertex_bytes;
	unsigned index_buffers, index_bytes;
	unsigned surfaces;              /* surface objects (texture levels handed out, image surfaces...) */
};

void XboxD3D_Get_Stats(XboxD3DStats *out);

#endif
