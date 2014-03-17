/*
 * Copyright © 2013 Jason Ekstrand
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "wlb-private.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

void
wlb_matrix_init(struct wlb_matrix *M)
{
	float identity[] = {
		1, 0, 0,
		0, 1, 0,
		0, 0, 1
	};

	memcpy(M->d, identity, sizeof identity);
}

static void
matrix_mult_impl(float *C, const float *A, const float *B)
{
	C[0] = A[0]*B[0] + A[3]*B[1] + A[6]*B[2];
	C[1] = A[1]*B[0] + A[4]*B[1] + A[7]*B[2];
	C[2] = A[2]*B[0] + A[5]*B[1] + A[8]*B[2];
	C[3] = A[0]*B[3] + A[3]*B[4] + A[6]*B[5];
	C[4] = A[1]*B[3] + A[4]*B[4] + A[7]*B[5];
	C[5] = A[2]*B[3] + A[5]*B[4] + A[8]*B[5];
	C[6] = A[0]*B[6] + A[3]*B[7] + A[6]*B[8];
	C[7] = A[1]*B[6] + A[4]*B[7] + A[7]*B[8];
	C[8] = A[2]*B[6] + A[5]*B[7] + A[8]*B[8];
}

/* Computes C = AB */
void
wlb_matrix_mult(struct wlb_matrix *dest,
		const struct wlb_matrix *A, const struct wlb_matrix *B)
{
	float tmp[9];

	if (dest == A || dest == B) {
		matrix_mult_impl(tmp, A->d, B->d);
		memcpy(dest->d, tmp, sizeof tmp);
	} else {
		matrix_mult_impl(dest->d, A->d, B->d);
	}
}

void
wlb_matrix_translate(struct wlb_matrix *dest,
		     const struct wlb_matrix *src, float dx, float dy)
{
	struct wlb_matrix tmat = { .d = {
		1, 0, 0, 0, 1, 0, dx, dy, 1
	} };

	wlb_matrix_mult(dest, src, &tmat);
}

void
wlb_matrix_rotate(struct wlb_matrix *dest,
		  const struct wlb_matrix *src, float cos, float sin)
{
	struct wlb_matrix tmat = { .d = {
		cos, sin, 0, -sin, cos, 0, 0, 0, 1
	} };

	wlb_matrix_mult(dest, src, &tmat);
}

void
wlb_matrix_scale(struct wlb_matrix *dest,
		 const struct wlb_matrix *src, float sx, float sy)
{
	struct wlb_matrix tmat = { .d = {
		sx, 0, 0, 0, sy, 0, 0, 0, 1
	} };

	wlb_matrix_mult(dest, src, &tmat);
}

void
wlb_matrix_ortho(struct wlb_matrix *dest, float l, float r, float t, float b)
{
	struct wlb_matrix tmat = { .d = {
		2/(r-l), 0, 0, 0, 2/(t-b), 0, (r+l)/(l-r), (t+b)/(b-t), 1
	} };

	memcpy(dest, &tmat, sizeof tmat);
}

