/****************************************************************************
*
*                            Open Watcom Project
*
*    Portions Copyright (c) 1983-2002 Sybase, Inc. All Rights Reserved.
*
*  ========================================================================
*
*    This file contains Original Code and/or Modifications of Original
*    Code as defined in and that are subject to the Sybase Open Watcom
*    Public License version 1.0 (the 'License'). You may not use this file
*    except in compliance with the License. BY USING THIS FILE YOU AGREE TO
*    ALL TERMS AND CONDITIONS OF THE LICENSE. A copy of the License is
*    provided with the Original Code and Modifications, and is also
*    available at www.sybase.com/developer/opensource.
*
*    The Original Code and all software distributed under the License are
*    distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
*    EXPRESS OR IMPLIED, AND SYBASE AND ALL CONTRIBUTORS HEREBY DISCLAIM
*    ALL SUCH WARRANTIES, INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF
*    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR
*    NON-INFRINGEMENT. Please see the License for the specific language
*    governing rights and limitations under the License.
*
*  ========================================================================
*
* Description:  WHEN YOU FIGURE OUT WHAT THIS FILE DOES, PLEASE
*               DESCRIBE IT HERE!
*
****************************************************************************/


#include "dipwat.h"
#include "watmod.h"

#define MODINFO_SIZE(p) (sizeof( mod_info ) + (unsigned char)((mod_info *)(p))->name[0])


typedef struct {
    unsigned    count;
    unsigned    mod_off[1]; /* variable number */
} mod_table;


section_info *FindInfo( imp_image_handle *ii, imp_mod_handle im )
{
    unsigned            num_sects;
    section_info        *inf;

    if( im == IMH_NOMOD )
        return( NULL );
    inf = ii->sect;
    num_sects = ii->num_sects;
    for( ;; ) {
        if( IDX2IMH( inf->mod_base_idx ) > im ) {
            --inf;
            break;
        }
        --num_sects;
        if( num_sects == 0 )
            break;
        ++inf;
    }
    return( inf );
}

/*
 * ModPointer - given a mod_handle, return the module information pointer
 */

mod_info *ModPointer( imp_image_handle *ii, imp_mod_handle im )
{
    info_block          *blk;
    mod_table           *tbl;
    unsigned            index;
    section_info        *inf;

    inf = FindInfo( ii, im );
    if( inf == NULL )
        return( NULL );
    index = im - IDX2IMH( inf->mod_base_idx );
    for( blk = inf->mod_info; blk != NULL; blk = blk->next ) {
        tbl = blk->link;
        if( index < tbl->count ) {
            return( (mod_info *)((byte *)blk->info + tbl->mod_off[index]) );
        }
        index -= tbl->count;
    }
    return( NULL );
}

/*
 * AllocLinkTable - allocate the demand load link table
 *
 */
static dip_status AllocLinkTable( section_info *inf, dword num_links, dword first_link )
{
    pointer_int         **lnk_tbl;
    pointer_int         *lnk;
    mod_table           *tbl;
    unsigned            num;
    dword               count;
    unsigned            i;
    demand_kind         dk;
    mod_info            *mod;
    unsigned            tbl_entries;
    pointer_int         end = 0;
    info_block          *blk;
#if defined( _M_X64 )
    unsigned            j;
#endif

    tbl_entries = ( ( num_links + ( MAX_LINK_ENTRIES - 1 ) ) / MAX_LINK_ENTRIES ) + 1;
    lnk_tbl = DCAlloc( tbl_entries * sizeof( pointer_int * ) );
    if( lnk_tbl == NULL ) {
        DCStatus( DS_ERR|DS_NO_MEM );
        return( DS_ERR|DS_NO_MEM );
    }
    for( i = 0; i < tbl_entries; ++i )
        lnk_tbl[i] = NULL;
    inf->dmnd_link = lnk_tbl;
    i = 0;
    num = MAX_LINK_ENTRIES;
    for( count = num_links; count > 0; count -= num ) {
        if( num > count )
            num = count;
        lnk = DCAlloc( num * sizeof( pointer_int ) );
        if( lnk == NULL ) {
            DCStatus( DS_ERR|DS_NO_MEM );
            return( DS_ERR|DS_NO_MEM );
        }
        lnk_tbl[i++] = lnk;
        if( !inf->ctl->v2 ) {
            if( DCRead( inf->ctl->sym_fid, lnk, num * sizeof( dword ) ) != num * sizeof( dword ) ) {
                DCStatus( DS_ERR|DS_FREAD_FAILED );
                return( DS_ERR|DS_FREAD_FAILED );
            }
#if defined( _M_X64 )
            for( j = num; j-- > 0; ) {
                lnk[j] = ((dword *)lnk)[j];
            }
#endif
        }
    }
    lnk = *lnk_tbl;
    num = 0;
    count = 0;
    for( dk = 0; dk < MAX_DMND; ++dk ) {
        for( blk = inf->mod_info; blk != NULL; blk = blk->next ) {
            tbl = blk->link;
            for( i = 0; i < tbl->count; ++i ) {
                mod = (mod_info *)((byte *)blk->info + tbl->mod_off[i]);
                if( inf->ctl->v2 ) {
                    if( mod->di[dk].u.size != 0 ) {
                        if( num >= MAX_LINK_ENTRIES ) {
                            lnk = *++lnk_tbl;
                            num = 0;
                        }
                        lnk[num] = mod->di[dk].info_off;
                        end = lnk[num] + mod->di[dk].u.size;
                        mod->di[dk].u.entries = 1;
                        mod->di[dk].info_off = count;
                        ++num;
                        ++count;
                    }
                } else {
                    if( mod->di[dk].u.entries != 0 ) {
                        mod->di[dk].info_off =
                        ( mod->di[dk].info_off - first_link ) / sizeof( pointer_int );
                    }
                }
            }
        }
    }
    if( inf->ctl->v2 ) {
        if( num >= MAX_LINK_ENTRIES ) {
            lnk = *++lnk_tbl;
            num = 0;
        }
        lnk[num] = end;
    }
    return( DS_OK );
}


/*
 * AdjustMods - adjust the demand info offsets in all the modules
 *            - allocate module index table
 *            - allocate demand load link table
 */
dip_status AdjustMods( section_info *inf, unsigned long adjust )
{
    info_block          *blk;
    unsigned            count;
    unsigned            mod_off;
    mod_table           *tbl;
    mod_info            *mod;
    dword               num_links;
    dword               first_link;
    dword               last_link;
    demand_kind         dk;
    unsigned            num;
    pointer_int         *lnk;
    pointer_int         **lnk_tbl;
    unsigned long       off;
    dip_status          ok;

    last_link = 0;
    first_link = (dword)-1L;
    num_links = 1;
    for( blk = inf->mod_info; blk != NULL; blk = blk->next ) {
        mod = (mod_info *)blk->info;
        count = 0;
        for( mod_off = 0; mod_off < blk->size; mod_off += MODINFO_SIZE( (byte *)mod + mod_off ) ) {
            ++count;
        }
        tbl = DCAlloc( sizeof( mod_table ) + ( count - 1 ) * sizeof( mod_info * ) );
        if( tbl == NULL ) {
            DCStatus( DS_ERR|DS_NO_MEM );
            return( DS_ERR|DS_NO_MEM );
        }
        tbl->count = count;
        blk->link = tbl;
        count = 0;
        for( mod_off = 0; mod_off < blk->size; mod_off += MODINFO_SIZE( mod ) ) {
            tbl->mod_off[count++] = mod_off;
            mod = (mod_info *)((byte *)blk->info + mod_off);
            for( dk = 0; dk < MAX_DMND; ++dk ) {
                if( inf->ctl->v2 ) {
                    if( mod->di[dk].u.size != 0 ) {
                        ++num_links;
                    }
                } else {
                    if( mod->di[dk].u.entries != 0 ) {
                        num_links = mod->di[dk].info_off;
                        if( first_link > num_links ) {
                            first_link = num_links;
                        }
                        num_links += mod->di[dk].u.entries * sizeof( pointer_int );
                        if( last_link < num_links ) {
                            last_link = num_links;
                        }
                    }
                }
            }
        }
    }
    if( !inf->ctl->v2 ) {
        off = first_link + adjust;
        if( DCSeek( inf->ctl->sym_fid, off, DIG_ORG) != off ) {
            DCStatus( DS_ERR|DS_FSEEK_FAILED );
            return( DS_ERR|DS_FSEEK_FAILED );
        }
        num_links = (last_link - first_link) / sizeof( pointer_int ) + 2;
    }
    ok = AllocLinkTable( inf, num_links, first_link );
    if( ok != DS_OK )
        return( ok );
    num = 0;
    lnk_tbl = inf->dmnd_link;
    lnk = *lnk_tbl;
    for( ; num_links-- > 0; ) {
        if( num >= MAX_LINK_ENTRIES ) {
            lnk = *++lnk_tbl;
            num = 0;
        }
        /*
         * shift over one bit so as to leave the lower bit
         * clear for testing if it is allocated or not
         */
        lnk[num] = (lnk[num] + adjust) << 1;
        ++num;
    }
    return( DS_OK );
}


void SetModBase( imp_image_handle *ii )
{
    unsigned        i;
    unsigned        base;
    unsigned        count;
    info_block      *blk;
    mod_table       *tbl;

    base = 0;
    for( i = 0; i < ii->num_sects; ++i ) {
        ii->sect[i].mod_base_idx = base;
        count = 0;
        for( blk = ii->sect[i].mod_info; blk != NULL; blk = blk->next ) {
            tbl = blk->link;
            count += tbl->count;
        }
        base += count;
    }
}


/*
 * ModInfoFini - free module index table
 */

void ModInfoFini( section_info *inf )
{
    info_block  *blk;
    pointer_int **lnk_tbl;
    pointer_int *lnk;

    for( blk = inf->mod_info; blk != NULL; blk = blk->next ) {
        DCFree( blk->link );
        blk->link = NULL;
    }
    lnk_tbl = inf->dmnd_link;
    if( lnk_tbl != NULL ) {
        for( ; (lnk = *lnk_tbl) != NULL; ++lnk_tbl ) {
            DCFree( lnk );
        }
        DCFree( inf->dmnd_link );
        inf->dmnd_link = NULL;
    }
}

/*
 * ModInfoSplit - find a good place to split module information
 */

unsigned ModInfoSplit( info_block *blk, section_info *inf )
{
    unsigned    off;
    unsigned    prev;
    byte        *start;

    /* unused parameters */ (void)inf;

    start = blk->info;
    prev = 0;
    // following use check for 16-bit unsigned type overflow
    for( off = MODINFO_SIZE( start ); off > prev && off <= blk->size; off += MODINFO_SIZE( start + off ) ) {
        prev = off;
    }
    return( prev );
}

word ModOff2Idx( section_info *inf, word off )
{
    unsigned    count;
    unsigned    moff;
    byte        *start;

    start = inf->mod_info->info;
    count = 0;
    for( moff = 0; moff < off; moff += MODINFO_SIZE( start + moff ) ) {
        ++count;
    }
    return( count );
}

/*
 * DIPImpWalkModList - walk the module list
 */

walk_result DIPIMPENTRY( WalkModList )( imp_image_handle *ii, DIP_IMP_MOD_WALKER *wk, void *d )
{
    info_block          *blk;
    mod_table           *tbl;
    unsigned            num_sects;
    unsigned            count;
    section_info        *inf;
    imp_mod_handle      im;
    walk_result         wr;

    inf = ii->sect;
    for( num_sects = ii->num_sects; num_sects != 0; --num_sects ) {
        im = IDX2IMH( inf->mod_base_idx );
        for( blk = inf->mod_info; blk != NULL; blk = blk->next ) {
            tbl = blk->link;
            for( count = 0; count < tbl->count; ++count ) {
                wr = wk( ii, im, d );
                if( wr != WR_CONTINUE )
                    return( wr );
                ++im;
            }
        }
        ++inf;
    }
    return( WR_CONTINUE );
}

walk_result MyWalkModList( imp_image_handle *ii, DIP_INT_MOD_WALKER *wk, void *d )
{
    info_block          *blk;
    mod_table           *tbl;
    unsigned            num_sects;
    unsigned            count;
    section_info        *inf;
    imp_mod_handle      im;
    walk_result         wr;

    inf = ii->sect;
    for( num_sects = ii->num_sects; num_sects != 0; --num_sects ) {
        im = IDX2IMH( inf->mod_base_idx );
        for( blk = inf->mod_info; blk != NULL; blk = blk->next ) {
            tbl = blk->link;
            for( count = 0; count < tbl->count; ++count ) {
                wr = wk( ii, im, d );
                if( wr != WR_CONTINUE )
                    return( wr );
                ++im;
            }
        }
        ++inf;
    }
    return( WR_CONTINUE );
}


/*
 * ModSrcLang -- return pointer to name of source language of the module
 */

char *DIPIMPENTRY( ModSrcLang )( imp_image_handle *ii, imp_mod_handle im )
{
    return( ii->lang + ModPointer( ii, im )->language );
}

/*
 * DIPImpModInfo - does a module have a particular brand of info
 */

dip_status DIPIMPENTRY( ModInfo )( imp_image_handle *ii, imp_mod_handle im, handle_kind hk )
{
    mod_info    *mod;
    static unsigned DmndType[MAX_HK] = {
        0,
        DMND_TYPES,
        DMND_LINES,
        DMND_LOCALS
    };

    if( hk == HK_IMAGE )
        return( DS_FAIL );
    mod = ModPointer( ii, im );
    return( mod->di[DmndType[hk]].u.entries != 0 ? DS_OK : DS_FAIL );
}

//NYI: should be OS && location sensitive
#define IS_PATH_CHAR( c ) ((c)=='\\'||(c)=='/'||(c)==':')
#define EXT_CHAR        '.'


/*
 * DIPImpModName -- return the module name
 */

size_t DIPIMPENTRY( ModName )( imp_image_handle *ii, imp_mod_handle im,
                                char *buff, size_t buff_size )
{
    char        *name;
    char        *start;
    char        *end;
    size_t      len;

    name = ModPointer( ii, im )->name;
    len = (unsigned char)name[0];
    if( len == 0 ) {
        *buff = '\0';
        return( 0 );
    }
    start = name + len;
    ++name;
    end = start + 1;
    if( *start == ')' ) {
        /* library member */
        --end;
        for( ;; ) {
            --start;
            if( *start == '(' ) {
                break;
            }
        }
        ++start;
    } else {
        /* file name */
        start = name;
        for( ; len > 0; --len ) {
            if( IS_PATH_CHAR( *name ) ) {
                start = name + 1;
                end = name + len;
            }
            if( *name == EXT_CHAR )
                end = name;
            ++name;
        }
    }
    len = end - start;
    if( buff_size > 0 ) {
        --buff_size;
        if( buff_size > len )
            buff_size = len;
        memcpy( buff, start, buff_size );
        buff[buff_size] = '\0';
    }
    return( len );
}

/*
 * PrimaryCueFile -- return the module source file
 */

size_t PrimaryCueFile( imp_image_handle *ii, imp_cue_handle *ic,
                        char *buff, size_t buff_size )
{
    size_t      len;
    char        *name;

    name = ModPointer( ii, ic->im )->name;
    len = (unsigned char)name[0];
    if( buff_size > 0 ) {
        --buff_size;
        if( buff_size > len )
            buff_size = len;
        memcpy( buff, name + 1, buff_size );
        buff[buff_size] = '\0';
    }
    return( len );
}
