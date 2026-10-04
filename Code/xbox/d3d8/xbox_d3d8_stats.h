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

/* Diagnostic: when on, the vertex program outputs w = 1, so the GPU applies no perspective
** correction. Used to tell whether depth problems come from that correction. Off normally. */
void XboxD3D_Set_Diagnostic_W1(bool on);

/* Things the layer met but doesn't support yet (each reported once), in the order met; NULL
** past the last. Test programs show these so a real model reveals what it still needs. */
const char *XboxD3D_Get_Notice(int index);

#endif
