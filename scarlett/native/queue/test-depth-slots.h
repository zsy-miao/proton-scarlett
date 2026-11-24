static BOOL guarded_depth_slots(IUnknown *list)
{
    /* A newly implemented executable slot needs a pending-order consumer first.
     * Check the actual guest table (including newer console slots), not a mock PSO. */
    for (size_t off=0x60;off<sizeof(pool_vtbl);off+=sizeof(void *)) {
        switch (off) {
        case 0x120:
            if(pool_vtbl.b27!=pool_b27 || SUCCEEDED(record_b27(sl(list),0,0)))return FALSE;
            continue;
        case 0x180:
            if(pool_vtbl.vb!=pool_colour_vb || SUCCEEDED(record_colour_vb(sl(list),0,1,NULL)))return FALSE;
            continue;
        case 0x60: {
            __typeof__(&pool_clear4_draw) fn;memcpy(&fn,(const BYTE *)list->lpVtbl+off,sizeof fn);
            if(fn!=pool_clear4_draw || SUCCEEDED(record_clear4_draw(sl(list),3,1,0,0)))return FALSE;
            continue;
        }
        case 0xa8: case 0xb0: case 0xa0: case 0xb8: case 0xc0: case 0xc8: case 0x100: case 0x110: case 0x190: case 0x160: case 0x178: case 0x1a0: case 0x230: case 0x238: case 0x240:
        case 0x250: case 0x258: case 0x3f0: case 0x418: case 0x458: case 0x470: case 0x490: continue;
        }
        xs_stop_fn fn;memcpy(&fn,(const BYTE *)list->lpVtbl+off,sizeof(fn));if(fn!=xs_stop)return FALSE;
    }
    return TRUE;
}
