/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */

/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#include <stdlib.h>

static void DLOOP_Type_indexed_array_copy(MPI_Aint count,
                                          MPI_Aint contig_count,
                                          const MPI_Aint * input_blocklength_array,
                                          const void *input_displacement_array,
                                          MPI_Aint * output_blocklength_array,
                                          MPI_Aint * out_disp_array,
                                          int dispinbytes, MPI_Aint old_extent);

/*@
   DLOOP_Dataloop_create_indexed

   Arguments:
+  int icount
.  MPI_Aint *iblocklength_array
.  void *displacement_array (either ints or MPI_Aints)
.  int dispinbytes
.  MPI_Datatype oldtype
.  MPIR_Dataloop **dlp_p
.  int *dlsz_p

.N Errors
.N Returns 0 on success, -1 on error.
@*/

int MPIR_Dataloop_create_indexed(MPI_Aint icount,
                                 const MPI_Aint * blocklength_array,
                                 const void *displacement_array,
                                 int dispinbytes,
                                 MPI_Datatype oldtype, MPIR_Dataloop ** dlp_p, MPI_Aint * dlsz_p)
{
    int err, is_builtin;
    MPI_Aint i;
    MPI_Aint new_loop_sz, blksz;
    MPI_Aint first;

    MPI_Aint old_type_count = 0, contig_count, count;
    MPI_Aint old_extent;
    struct MPIR_Dataloop *new_dlp;

    count = (MPI_Aint) icount;  /* avoid subsequent casting */


    /* if count is zero, handle with contig code, call it an int */
    if (count == 0) {
        err = MPIR_Dataloop_create_contiguous(0, MPI_INT, dlp_p, dlsz_p);
        return err;
    }

    /* Skip any initial zero-length blocks */
    for (first = 0; first < count; first++)
        if ((MPI_Aint) blocklength_array[first])
            break;


    is_builtin = (DLOOP_Handle_hasloop_macro(oldtype)) ? 0 : 1;

    if (is_builtin) {
        MPIR_Datatype_get_extent_macro(oldtype, old_extent);
    } else {
        MPIR_Datatype_get_extent_macro(oldtype, old_extent);
    }

    for (i = first; i < count; i++) {
        old_type_count += (MPI_Aint) blocklength_array[i];
    }

    contig_count = MPIR_Type_indexed_count_contig(count,
                                                  blocklength_array,
                                                  displacement_array, dispinbytes, old_extent);

    /* if contig_count is zero (no data), handle with contig code */
    if (contig_count == 0) {
        err = MPIR_Dataloop_create_contiguous(0, MPI_INT, dlp_p, dlsz_p);
        return err;
    }

    /* optimization:
     *
     * if contig_count == 1 and block starts at displacement 0,
     * store it as a contiguous rather than an indexed dataloop.
     */
    if ((contig_count == 1) &&
        ((!dispinbytes && ((int *) displacement_array)[first] == 0) ||
         (dispinbytes && ((MPI_Aint *) displacement_array)[first] == 0))) {
        err = MPIR_Dataloop_create_contiguous(old_type_count, oldtype, dlp_p, dlsz_p);
        return err;
    }

    /* optimization:
     *
     * if contig_count == 1 (and displacement != 0), store this as
     * a single element blockindexed rather than a lot of individual
     * blocks.
     */
    if (contig_count == 1) {
        const void *disp_arr_tmp;       /* no ternary assignment to avoid clang warnings */
        if (dispinbytes)
            disp_arr_tmp = &(((const MPI_Aint *) displacement_array)[first]);
        else
            disp_arr_tmp = &(((const int *) displacement_array)[first]);
        err = MPIR_Dataloop_create_blockindexed(1,
                                                old_type_count,
                                                disp_arr_tmp, dispinbytes, oldtype, dlp_p, dlsz_p);

        return err;
    }

    /* optimization:
     *
     * if block length is the same for all blocks, store it as a
     * blockindexed rather than an indexed dataloop.
     */
    blksz = blocklength_array[first];
    for (i = first + 1; i < count; i++) {
        if (blocklength_array[i] != blksz) {
            blksz--;
            break;
        }
    }
    if (blksz == blocklength_array[first]) {
        const void *disp_arr_tmp;       /* no ternary assignment to avoid clang warnings */
        if (dispinbytes)
            disp_arr_tmp = &(((const MPI_Aint *) displacement_array)[first]);
        else
            disp_arr_tmp = &(((const int *) displacement_array)[first]);
        err = MPIR_Dataloop_create_blockindexed(icount - first,
                                                blksz,
                                                disp_arr_tmp, dispinbytes, oldtype, dlp_p, dlsz_p);

        return err;
    }

    /* note: blockindexed looks for the vector optimization */

    /* TODO: optimization:
     *
     * if an indexed of a contig, absorb the contig into the blocklen array
     * and keep the same overall depth
     */

    /* otherwise storing as an indexed dataloop */

    if (is_builtin) {
        MPIR_Dataloop_alloc(DLOOP_KIND_INDEXED, count, &new_dlp, &new_loop_sz);
        /* --BEGIN ERROR HANDLING-- */
        if (!new_dlp)
            return -1;
        /* --END ERROR HANDLING-- */

        new_dlp->kind = DLOOP_KIND_INDEXED | DLOOP_FINAL_MASK;

        new_dlp->el_size = old_extent;
        new_dlp->el_extent = old_extent;
        new_dlp->el_type = oldtype;
    } else {
        MPIR_Dataloop *old_loop_ptr = NULL;
        MPI_Aint old_loop_sz = 0;

        MPIR_Datatype_get_loopptr_macro(oldtype, old_loop_ptr);
        MPIR_Datatype_get_loopsize_macro(oldtype, old_loop_sz);

        MPIR_Dataloop_alloc_and_copy(DLOOP_KIND_INDEXED,
                                     contig_count,
                                     old_loop_ptr, old_loop_sz, &new_dlp, &new_loop_sz);
        /* --BEGIN ERROR HANDLING-- */
        if (!new_dlp)
            return -1;
        /* --END ERROR HANDLING-- */

        new_dlp->kind = DLOOP_KIND_INDEXED;

        MPIR_Datatype_get_size_macro(oldtype, new_dlp->el_size);
        MPIR_Datatype_get_extent_macro(oldtype, new_dlp->el_extent);
        MPIR_Datatype_get_basic_type(oldtype, new_dlp->el_type);
    }

    new_dlp->loop_params.i_t.count = contig_count;
    new_dlp->loop_params.i_t.total_blocks = old_type_count;

    /* copy in blocklength and displacement parameters (in that order)
     *
     * regardless of dispinbytes, we store displacements in bytes in loop.
     */
    DLOOP_Type_indexed_array_copy(count,
                                  contig_count,
                                  blocklength_array,
                                  displacement_array,
                                  new_dlp->loop_params.i_t.blocksize_array,
                                  new_dlp->loop_params.i_t.offset_array, dispinbytes, old_extent);

    *dlp_p = new_dlp;
    *dlsz_p = new_loop_sz;

    return MPI_SUCCESS;
}

/* DLOOP_Type_indexed_array_copy()
 *
 * Copies arrays into place, combining adjacent contiguous regions and
 * dropping zero-length regions.
 *
 * Extent passed in is for the original type.
 *
 * Output displacements are always output in bytes, while block
 * lengths are always output in terms of the base type.
 */
static void DLOOP_Type_indexed_array_copy(MPI_Aint count,
                                          MPI_Aint contig_count,
                                          const MPI_Aint * in_blklen_array,
                                          const void *in_disp_array,
                                          MPI_Aint * out_blklen_array,
                                          MPI_Aint * out_disp_array,
                                          int dispinbytes, MPI_Aint old_extent)
{
    MPI_Aint i, first, cur_idx = 0;

    /* Skip any initial zero-length blocks */
    for (first = 0; first < count; ++first)
        if ((MPI_Aint) in_blklen_array[first])
            break;

    out_blklen_array[0] = (MPI_Aint) in_blklen_array[first];

    if (!dispinbytes) {
        out_disp_array[0] = (MPI_Aint)
            ((int *) in_disp_array)[first] * old_extent;

        for (i = first + 1; i < count; ++i) {
            if (in_blklen_array[i] == 0) {
                continue;
            } else if (out_disp_array[cur_idx] +
                       ((MPI_Aint) out_blklen_array[cur_idx]) * old_extent ==
                       ((MPI_Aint) ((int *) in_disp_array)[i]) * old_extent) {
                /* adjacent to current block; add to block */
                out_blklen_array[cur_idx] += (MPI_Aint) in_blklen_array[i];
            } else {
                cur_idx++;
                MPIR_Assert(cur_idx < contig_count);
                out_disp_array[cur_idx] = ((MPI_Aint) ((int *) in_disp_array)[i]) * old_extent;
                out_blklen_array[cur_idx] = in_blklen_array[i];
            }
        }
    } else {    /* input displacements already in bytes */

        out_disp_array[0] = (MPI_Aint) ((MPI_Aint *) in_disp_array)[first];

        for (i = first + 1; i < count; ++i) {
            if (in_blklen_array[i] == 0) {
                continue;
            } else if (out_disp_array[cur_idx] +
                       ((MPI_Aint) out_blklen_array[cur_idx]) * old_extent ==
                       ((MPI_Aint) ((MPI_Aint *) in_disp_array)[i])) {
                /* adjacent to current block; add to block */
                out_blklen_array[cur_idx] += in_blklen_array[i];
            } else {
                cur_idx++;
                MPIR_Assert(cur_idx < contig_count);
                out_disp_array[cur_idx] = (MPI_Aint) ((MPI_Aint *) in_disp_array)[i];
                out_blklen_array[cur_idx] = (MPI_Aint) in_blklen_array[i];
            }
        }
    }

    MPIR_Assert(cur_idx == contig_count - 1);
    return;
}

/* DLOOP_Type_indexed_count_contig()
 *
 * Determines the actual number of contiguous blocks represented by the
 * blocklength/displacement arrays.  This might be less than count (as
 * few as 1).
 *
 * Extent passed in is for the original type.
 */
MPI_Aint MPIR_Type_indexed_count_contig(MPI_Aint count,
                                        const MPI_Aint * blocklength_array,
                                        const void *displacement_array,
                                        int dispinbytes, MPI_Aint old_extent)
{
    MPI_Aint i, contig_count = 1;
    MPI_Aint cur_blklen, first;

    if (count) {
        /* Skip any initial zero-length blocks */
        for (first = 0; first < count; ++first)
            if ((MPI_Aint) blocklength_array[first])
                break;

        if (first == count) {   /* avoid invalid reads later on */
            contig_count = 0;
            return contig_count;
        }

        cur_blklen = (MPI_Aint) blocklength_array[first];
        if (!dispinbytes) {
            MPI_Aint cur_tdisp = (MPI_Aint) ((int *) displacement_array)[first];

            for (i = first + 1; i < count; ++i) {
                if (blocklength_array[i] == 0) {
                    continue;
                } else if (cur_tdisp + (MPI_Aint) cur_blklen ==
                           (MPI_Aint) ((int *) displacement_array)[i]) {
                    /* adjacent to current block; add to block */
                    cur_blklen += (MPI_Aint) blocklength_array[i];
                } else {
                    cur_tdisp = (MPI_Aint) ((int *) displacement_array)[i];
                    cur_blklen = (MPI_Aint) blocklength_array[i];
                    contig_count++;
                }
            }
        } else {
            MPI_Aint cur_bdisp = (MPI_Aint) ((MPI_Aint *) displacement_array)[first];

            for (i = first + 1; i < count; ++i) {
                if (blocklength_array[i] == 0) {
                    continue;
                } else if (cur_bdisp + (MPI_Aint) cur_blklen * old_extent ==
                           (MPI_Aint) ((MPI_Aint *) displacement_array)[i]) {
                    /* adjacent to current block; add to block */
                    cur_blklen += (MPI_Aint) blocklength_array[i];
                } else {
                    cur_bdisp = (MPI_Aint) ((MPI_Aint *) displacement_array)[i];
                    cur_blklen = (MPI_Aint) blocklength_array[i];
                    contig_count++;
                }
            }
        }
    }
    return contig_count;
}
