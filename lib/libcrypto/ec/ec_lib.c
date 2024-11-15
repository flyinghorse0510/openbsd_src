/* $OpenBSD: ec_lib.c,v 1.85 2024/11/08 13:55:45 tb Exp $ */
/*
 * Originally written by Bodo Moeller for the OpenSSL project.
 */
/* ====================================================================
 * Copyright (c) 1998-2003 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */
/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 * Binary polynomial ECC support in OpenSSL originally developed by
 * SUN MICROSYSTEMS, INC., and contributed to the OpenSSL project.
 */

#include <stdlib.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/objects.h>
#include <openssl/opensslv.h>

#include "bn_local.h"
#include "ec_local.h"

/* functions for EC_GROUP objects */

EC_GROUP *
EC_GROUP_new(const EC_METHOD *meth)
{
	EC_GROUP *group = NULL;

	if (meth == NULL) {
		ECerror(EC_R_SLOT_FULL);
		goto err;
	}
	if (meth->group_init == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}
	if ((group = calloc(1, sizeof(*group))) == NULL) {
		ECerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	group->meth = meth;

	BN_init(&group->order);
	BN_init(&group->cofactor);

	group->asn1_flag = OPENSSL_EC_NAMED_CURVE;
	group->asn1_form = POINT_CONVERSION_UNCOMPRESSED;

	if (!meth->group_init(group))
		goto err;

	return group;

 err:
	EC_GROUP_free(group);

	return NULL;
}
LCRYPTO_ALIAS(EC_GROUP_new);

void
EC_GROUP_free(EC_GROUP *group)
{
	if (group == NULL)
		return;

	if (group->meth->group_finish != NULL)
		group->meth->group_finish(group);

	EC_POINT_free(group->generator);
	BN_free(&group->order);
	BN_free(&group->cofactor);

	freezero(group->seed, group->seed_len);
	freezero(group, sizeof *group);
}
LCRYPTO_ALIAS(EC_GROUP_free);

void
EC_GROUP_clear_free(EC_GROUP *group)
{
	EC_GROUP_free(group);
}
LCRYPTO_ALIAS(EC_GROUP_clear_free);

int
EC_GROUP_copy(EC_GROUP *dest, const EC_GROUP *src)
{
	if (dest->meth->group_copy == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		return 0;
	}
	if (dest->meth != src->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		return 0;
	}
	if (dest == src)
		return 1;

	if (!dest->meth->group_copy(dest, src))
		return 0;

	EC_POINT_free(dest->generator);
	dest->generator = NULL;
	if (src->generator != NULL) {
		if (!EC_GROUP_set_generator(dest, src->generator, &src->order,
		    &src->cofactor))
			return 0;
	} else {
		/* XXX - should do the sanity checks as in set_generator() */
		if (!bn_copy(&dest->order, &src->order))
			return 0;
		if (!bn_copy(&dest->cofactor, &src->cofactor))
			return 0;
	}

	dest->curve_name = src->curve_name;
	dest->asn1_flag = src->asn1_flag;
	dest->asn1_form = src->asn1_form;

	if (!EC_GROUP_set_seed(dest, src->seed, src->seed_len))
		return 0;

	return dest->meth->group_copy(dest, src);
}
LCRYPTO_ALIAS(EC_GROUP_copy);

EC_GROUP *
EC_GROUP_dup(const EC_GROUP *in_group)
{
	EC_GROUP *group = NULL;

	if (in_group == NULL)
		goto err;

	if ((group = EC_GROUP_new(in_group->meth)) == NULL)
		goto err;
	if (!EC_GROUP_copy(group, in_group))
		goto err;

	return group;

 err:
	EC_GROUP_free(group);

	return NULL;
}
LCRYPTO_ALIAS(EC_GROUP_dup);

const EC_METHOD *
EC_GROUP_method_of(const EC_GROUP *group)
{
	return group->meth;
}
LCRYPTO_ALIAS(EC_GROUP_method_of);

int
EC_METHOD_get_field_type(const EC_METHOD *meth)
{
	return meth->field_type;
}
LCRYPTO_ALIAS(EC_METHOD_get_field_type);

int
ec_group_get_field_type(const EC_GROUP *group)
{
	if (group == NULL || group->meth == NULL)
		return NID_undef;

	return group->meth->field_type;
}

/*
 * If there is a user-provided cofactor, sanity check and use it. Otherwise
 * try computing the cofactor from generator order n and field cardinality q.
 * This works for all curves of cryptographic interest.
 *
 * Hasse's theorem: | h * n - (q + 1) | <= 2 * sqrt(q)
 *
 * So: h_min = (q + 1 - 2*sqrt(q)) / n and h_max = (q + 1 + 2*sqrt(q)) / n and
 * therefore h_max - h_min = 4*sqrt(q) / n. So if n > 4*sqrt(q) holds, there is
 * only one possible value for h:
 *
 *	h = \lfloor (h_min + h_max)/2 \rceil = \lfloor (q + 1)/n \rceil
 *
 * Otherwise, zero cofactor and return success.
 */
static int
ec_set_cofactor(EC_GROUP *group, const BIGNUM *in_cofactor)
{
	BN_CTX *ctx = NULL;
	BIGNUM *cofactor;
	int ret = 0;

	BN_zero(&group->cofactor);

	if ((ctx = BN_CTX_new()) == NULL)
		goto err;

	BN_CTX_start(ctx);
	if ((cofactor = BN_CTX_get(ctx)) == NULL)
		goto err;

	/*
	 * Unfortunately, the cofactor is an optional field in many standards.
	 * Internally, the library uses a 0 cofactor as a marker for "unknown
	 * cofactor".  So accept in_cofactor == NULL or in_cofactor >= 0.
	 */
	if (in_cofactor != NULL && !BN_is_zero(in_cofactor)) {
		if (BN_is_negative(in_cofactor)) {
			ECerror(EC_R_UNKNOWN_COFACTOR);
			goto err;
		}
		if (!bn_copy(cofactor, in_cofactor))
			goto err;
		goto done;
	}

	/*
	 * If the cofactor is too large, we cannot guess it and default to zero.
	 * The RHS of below is a strict overestimate of log(4 * sqrt(q)).
	 */
	if (BN_num_bits(&group->order) <=
	    (BN_num_bits(&group->field) + 1) / 2 + 3)
		goto done;

	/*
	 * Compute
	 *     h = \lfloor (q + 1)/n \rceil = \lfloor (q + 1 + n/2) / n \rfloor.
	 */

	/* h = n/2 */
	if (!BN_rshift1(cofactor, &group->order))
		goto err;
	/* h = 1 + n/2 */
	if (!BN_add_word(cofactor, 1))
		goto err;
	/* h = q + 1 + n/2 */
	if (!BN_add(cofactor, cofactor, &group->field))
		goto err;
	/* h = (q + 1 + n/2) / n */
	if (!BN_div_ct(cofactor, NULL, cofactor, &group->order, ctx))
		goto err;

 done:
	/* Use Hasse's theorem to bound the cofactor. */
	if (BN_num_bits(cofactor) > BN_num_bits(&group->field) + 1) {
		ECerror(EC_R_INVALID_GROUP_ORDER);
		goto err;
	}

	if (!bn_copy(&group->cofactor, cofactor))
		goto err;

	ret = 1;

 err:
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);

	return ret;
}

int
EC_GROUP_set_generator(EC_GROUP *group, const EC_POINT *generator,
    const BIGNUM *order, const BIGNUM *cofactor)
{
	if (generator == NULL) {
		ECerror(ERR_R_PASSED_NULL_PARAMETER);
		return 0;
	}

	/* Require group->field >= 1. */
	if (BN_is_zero(&group->field) || BN_is_negative(&group->field)) {
		ECerror(EC_R_INVALID_FIELD);
		return 0;
	}

	/*
	 * Require order > 1 and enforce an upper bound of at most one bit more
	 * than the field cardinality due to Hasse's theorem.
	 */
	if (order == NULL || BN_cmp(order, BN_value_one()) <= 0 ||
	    BN_num_bits(order) > BN_num_bits(&group->field) + 1) {
		ECerror(EC_R_INVALID_GROUP_ORDER);
		return 0;
	}

	if (group->generator == NULL)
		group->generator = EC_POINT_new(group);
	if (group->generator == NULL)
		return 0;

	if (!EC_POINT_copy(group->generator, generator))
		return 0;

	if (!bn_copy(&group->order, order))
		return 0;

	if (!ec_set_cofactor(group, cofactor))
		return 0;

	return 1;
}
LCRYPTO_ALIAS(EC_GROUP_set_generator);

const EC_POINT *
EC_GROUP_get0_generator(const EC_GROUP *group)
{
	return group->generator;
}
LCRYPTO_ALIAS(EC_GROUP_get0_generator);

int
EC_GROUP_get_order(const EC_GROUP *group, BIGNUM *order, BN_CTX *ctx)
{
	if (!bn_copy(order, &group->order))
		return 0;

	return !BN_is_zero(order);
}
LCRYPTO_ALIAS(EC_GROUP_get_order);

const BIGNUM *
EC_GROUP_get0_order(const EC_GROUP *group)
{
	return &group->order;
}

int
EC_GROUP_order_bits(const EC_GROUP *group)
{
	return group->meth->group_order_bits(group);
}
LCRYPTO_ALIAS(EC_GROUP_order_bits);

int
EC_GROUP_get_cofactor(const EC_GROUP *group, BIGNUM *cofactor, BN_CTX *ctx)
{
	if (!bn_copy(cofactor, &group->cofactor))
		return 0;

	return !BN_is_zero(&group->cofactor);
}
LCRYPTO_ALIAS(EC_GROUP_get_cofactor);

const BIGNUM *
EC_GROUP_get0_cofactor(const EC_GROUP *group)
{
	return &group->cofactor;
}

void
EC_GROUP_set_curve_name(EC_GROUP *group, int nid)
{
	group->curve_name = nid;
}
LCRYPTO_ALIAS(EC_GROUP_set_curve_name);

int
EC_GROUP_get_curve_name(const EC_GROUP *group)
{
	return group->curve_name;
}
LCRYPTO_ALIAS(EC_GROUP_get_curve_name);

void
EC_GROUP_set_asn1_flag(EC_GROUP *group, int flag)
{
	group->asn1_flag = flag;
}
LCRYPTO_ALIAS(EC_GROUP_set_asn1_flag);

int
EC_GROUP_get_asn1_flag(const EC_GROUP *group)
{
	return group->asn1_flag;
}
LCRYPTO_ALIAS(EC_GROUP_get_asn1_flag);

void
EC_GROUP_set_point_conversion_form(EC_GROUP *group,
    point_conversion_form_t form)
{
	group->asn1_form = form;
}
LCRYPTO_ALIAS(EC_GROUP_set_point_conversion_form);

point_conversion_form_t
EC_GROUP_get_point_conversion_form(const EC_GROUP *group)
{
	return group->asn1_form;
}
LCRYPTO_ALIAS(EC_GROUP_get_point_conversion_form);

size_t
EC_GROUP_set_seed(EC_GROUP *group, const unsigned char *seed, size_t len)
{
	free(group->seed);
	group->seed = NULL;
	group->seed_len = 0;

	if (seed == NULL || len == 0)
		return 1;

	if ((group->seed = malloc(len)) == NULL)
		return 0;
	memcpy(group->seed, seed, len);
	group->seed_len = len;

	return len;
}
LCRYPTO_ALIAS(EC_GROUP_set_seed);

unsigned char *
EC_GROUP_get0_seed(const EC_GROUP *group)
{
	return group->seed;
}
LCRYPTO_ALIAS(EC_GROUP_get0_seed);

size_t
EC_GROUP_get_seed_len(const EC_GROUP *group)
{
	return group->seed_len;
}
LCRYPTO_ALIAS(EC_GROUP_get_seed_len);

int
EC_GROUP_set_curve(EC_GROUP *group, const BIGNUM *p, const BIGNUM *a,
    const BIGNUM *b, BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	int ret = 0;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth->group_set_curve == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}
	ret = group->meth->group_set_curve(group, p, a, b, ctx);

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(EC_GROUP_set_curve);

int
EC_GROUP_get_curve(const EC_GROUP *group, BIGNUM *p, BIGNUM *a, BIGNUM *b,
    BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	int ret = 0;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth->group_get_curve == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}
	ret = group->meth->group_get_curve(group, p, a, b, ctx);

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(EC_GROUP_get_curve);

int
EC_GROUP_set_curve_GFp(EC_GROUP *group, const BIGNUM *p, const BIGNUM *a,
    const BIGNUM *b, BN_CTX *ctx)
{
	return EC_GROUP_set_curve(group, p, a, b, ctx);
}
LCRYPTO_ALIAS(EC_GROUP_set_curve_GFp);

int
EC_GROUP_get_curve_GFp(const EC_GROUP *group, BIGNUM *p, BIGNUM *a, BIGNUM *b,
    BN_CTX *ctx)
{
	return EC_GROUP_get_curve(group, p, a, b, ctx);
}
LCRYPTO_ALIAS(EC_GROUP_get_curve_GFp);

EC_GROUP *
EC_GROUP_new_curve_GFp(const BIGNUM *p, const BIGNUM *a, const BIGNUM *b,
    BN_CTX *ctx)
{
	EC_GROUP *group;

	if ((group = EC_GROUP_new(EC_GFp_mont_method())) == NULL)
		goto err;

	if (!EC_GROUP_set_curve(group, p, a, b, ctx))
		goto err;

	return group;

 err:
	EC_GROUP_free(group);

	return NULL;
}
LCRYPTO_ALIAS(EC_GROUP_new_curve_GFp);

int
EC_GROUP_get_degree(const EC_GROUP *group)
{
	if (group->meth->group_get_degree == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		return 0;
	}
	return group->meth->group_get_degree(group);
}
LCRYPTO_ALIAS(EC_GROUP_get_degree);

int
EC_GROUP_check_discriminant(const EC_GROUP *group, BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	int ret = 0;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth->group_check_discriminant == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}
	ret = group->meth->group_check_discriminant(group, ctx);

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(EC_GROUP_check_discriminant);

int
EC_GROUP_check(const EC_GROUP *group, BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	EC_POINT *point = NULL;
	const BIGNUM *order;
	int ret = 0;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (!EC_GROUP_check_discriminant(group, ctx)) {
		ECerror(EC_R_DISCRIMINANT_IS_ZERO);
		goto err;
	}

	if (group->generator == NULL) {
		ECerror(EC_R_UNDEFINED_GENERATOR);
		goto err;
	}
	if (EC_POINT_is_on_curve(group, group->generator, ctx) <= 0) {
		ECerror(EC_R_POINT_IS_NOT_ON_CURVE);
		goto err;
	}

	if ((point = EC_POINT_new(group)) == NULL)
		goto err;
	if ((order = EC_GROUP_get0_order(group)) == NULL)
		goto err;
	if (BN_is_zero(order)) {
		ECerror(EC_R_UNDEFINED_ORDER);
		goto err;
	}
	if (!EC_POINT_mul(group, point, order, NULL, NULL, ctx))
		goto err;
	if (!EC_POINT_is_at_infinity(group, point)) {
		ECerror(EC_R_INVALID_GROUP_ORDER);
		goto err;
	}

	ret = 1;

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	EC_POINT_free(point);

	return ret;
}
LCRYPTO_ALIAS(EC_GROUP_check);

int
EC_GROUP_cmp(const EC_GROUP *a, const EC_GROUP *b, BN_CTX *ctx)
{
	int r = 0;
	BIGNUM *a1, *a2, *a3, *b1, *b2, *b3;
	BN_CTX *ctx_new = NULL;

	/* compare the field types */
	if (ec_group_get_field_type(a) != ec_group_get_field_type(b))
		return 1;
	/* compare the curve name (if present in both) */
	if (EC_GROUP_get_curve_name(a) && EC_GROUP_get_curve_name(b) &&
	    EC_GROUP_get_curve_name(a) != EC_GROUP_get_curve_name(b))
		return 1;

	if (!ctx)
		ctx_new = ctx = BN_CTX_new();
	if (!ctx)
		return -1;

	BN_CTX_start(ctx);
	if ((a1 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((a2 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((a3 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((b1 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((b2 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((b3 = BN_CTX_get(ctx)) == NULL)
		goto err;

	/*
	 * XXX This approach assumes that the external representation of
	 * curves over the same field type is the same.
	 */
	if (!a->meth->group_get_curve(a, a1, a2, a3, ctx) ||
	    !b->meth->group_get_curve(b, b1, b2, b3, ctx))
		r = 1;

	if (r || BN_cmp(a1, b1) || BN_cmp(a2, b2) || BN_cmp(a3, b3))
		r = 1;

	/* XXX EC_POINT_cmp() assumes that the methods are equal */
	if (r || EC_POINT_cmp(a, EC_GROUP_get0_generator(a),
		EC_GROUP_get0_generator(b), ctx))
		r = 1;

	if (!r) {
		/* compare the order and cofactor */
		if (!EC_GROUP_get_order(a, a1, ctx) ||
		    !EC_GROUP_get_order(b, b1, ctx) ||
		    !EC_GROUP_get_cofactor(a, a2, ctx) ||
		    !EC_GROUP_get_cofactor(b, b2, ctx))
			goto err;
		if (BN_cmp(a1, b1) || BN_cmp(a2, b2))
			r = 1;
	}
	BN_CTX_end(ctx);
	if (ctx_new)
		BN_CTX_free(ctx);

	return r;

 err:
	BN_CTX_end(ctx);
	if (ctx_new)
		BN_CTX_free(ctx);
	return -1;
}
LCRYPTO_ALIAS(EC_GROUP_cmp);

/*
 * Coordinate blinding for EC_POINT.
 *
 * The underlying EC_METHOD can optionally implement this function:
 * underlying implementations should return 0 on errors, or 1 on success.
 *
 * This wrapper returns 1 in case the underlying EC_METHOD does not support
 * coordinate blinding.
 */
int
ec_point_blind_coordinates(const EC_GROUP *group, EC_POINT *p, BN_CTX *ctx)
{
	if (group->meth->blind_coordinates == NULL)
		return 1;

	return group->meth->blind_coordinates(group, p, ctx);
}

EC_POINT *
EC_POINT_new(const EC_GROUP *group)
{
	EC_POINT *point = NULL;

	if (group == NULL) {
		ECerror(ERR_R_PASSED_NULL_PARAMETER);
		goto err;
	}
	if (group->meth->point_init == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}

	if ((point = calloc(1, sizeof(*point))) == NULL) {
		ECerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	point->meth = group->meth;

	if (!point->meth->point_init(point))
		goto err;

	return point;

 err:
	EC_POINT_free(point);

	return NULL;
}
LCRYPTO_ALIAS(EC_POINT_new);

void
EC_POINT_free(EC_POINT *point)
{
	if (point == NULL)
		return;

	if (point->meth->point_finish != NULL)
		point->meth->point_finish(point);

	freezero(point, sizeof *point);
}
LCRYPTO_ALIAS(EC_POINT_free);

void
EC_POINT_clear_free(EC_POINT *point)
{
	EC_POINT_free(point);
}
LCRYPTO_ALIAS(EC_POINT_clear_free);

int
EC_POINT_copy(EC_POINT *dest, const EC_POINT *src)
{
	if (dest->meth->point_copy == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		return 0;
	}
	if (dest->meth != src->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		return 0;
	}
	if (dest == src)
		return 1;
	return dest->meth->point_copy(dest, src);
}
LCRYPTO_ALIAS(EC_POINT_copy);

EC_POINT *
EC_POINT_dup(const EC_POINT *in_point, const EC_GROUP *group)
{
	EC_POINT *point = NULL;

	if (in_point == NULL)
		goto err;

	if ((point = EC_POINT_new(group)) == NULL)
		goto err;

	if (!EC_POINT_copy(point, in_point))
		goto err;

	return point;

 err:
	EC_POINT_free(point);

	return NULL;
}
LCRYPTO_ALIAS(EC_POINT_dup);

const EC_METHOD *
EC_POINT_method_of(const EC_POINT *point)
{
	return point->meth;
}
LCRYPTO_ALIAS(EC_POINT_method_of);

int
EC_POINT_set_to_infinity(const EC_GROUP *group, EC_POINT *point)
{
	if (group->meth->point_set_to_infinity == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		return 0;
	}
	if (group->meth != point->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		return 0;
	}
	return group->meth->point_set_to_infinity(group, point);
}
LCRYPTO_ALIAS(EC_POINT_set_to_infinity);

int
EC_POINT_set_Jprojective_coordinates(const EC_GROUP *group, EC_POINT *point,
    const BIGNUM *x, const BIGNUM *y, const BIGNUM *z, BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	int ret = 0;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth->point_set_Jprojective_coordinates == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}
	if (group->meth != point->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		goto err;
	}
	if (!group->meth->point_set_Jprojective_coordinates(group, point,
	    x, y, z, ctx))
		goto err;

	if (EC_POINT_is_on_curve(group, point, ctx) <= 0) {
		ECerror(EC_R_POINT_IS_NOT_ON_CURVE);
		goto err;
	}

	ret = 1;

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}

int
EC_POINT_get_Jprojective_coordinates(const EC_GROUP *group,
    const EC_POINT *point, BIGNUM *x, BIGNUM *y, BIGNUM *z, BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	int ret = 0;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth->point_get_Jprojective_coordinates == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}
	if (group->meth != point->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		goto err;
	}
	ret = group->meth->point_get_Jprojective_coordinates(group, point,
	    x, y, z, ctx);

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}

int
EC_POINT_set_Jprojective_coordinates_GFp(const EC_GROUP *group, EC_POINT *point,
    const BIGNUM *x, const BIGNUM *y, const BIGNUM *z, BN_CTX *ctx)
{
	return EC_POINT_set_Jprojective_coordinates(group, point, x, y, z, ctx);
}
LCRYPTO_ALIAS(EC_POINT_set_Jprojective_coordinates_GFp);

int
EC_POINT_get_Jprojective_coordinates_GFp(const EC_GROUP *group,
    const EC_POINT *point, BIGNUM *x, BIGNUM *y, BIGNUM *z, BN_CTX *ctx)
{
	return EC_POINT_get_Jprojective_coordinates(group, point, x, y, z, ctx);
}
LCRYPTO_ALIAS(EC_POINT_get_Jprojective_coordinates_GFp);

int
EC_POINT_set_affine_coordinates(const EC_GROUP *group, EC_POINT *point,
    const BIGNUM *x, const BIGNUM *y, BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	int ret = 0;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth->point_set_affine_coordinates == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}
	if (group->meth != point->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		goto err;
	}
	if (!group->meth->point_set_affine_coordinates(group, point, x, y, ctx))
		goto err;

	if (EC_POINT_is_on_curve(group, point, ctx) <= 0) {
		ECerror(EC_R_POINT_IS_NOT_ON_CURVE);
		goto err;
	}

	ret = 1;

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(EC_POINT_set_affine_coordinates);

int
EC_POINT_set_affine_coordinates_GFp(const EC_GROUP *group, EC_POINT *point,
    const BIGNUM *x, const BIGNUM *y, BN_CTX *ctx)
{
	return EC_POINT_set_affine_coordinates(group, point, x, y, ctx);
}
LCRYPTO_ALIAS(EC_POINT_set_affine_coordinates_GFp);

int
EC_POINT_get_affine_coordinates(const EC_GROUP *group, const EC_POINT *point,
    BIGNUM *x, BIGNUM *y, BN_CTX *ctx_in)
{
	BN_CTX *ctx = NULL;
	int ret = 0;

	if (EC_POINT_is_at_infinity(group, point) > 0) {
		ECerror(EC_R_POINT_AT_INFINITY);
		goto err;
	}

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth->point_get_affine_coordinates == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}
	if (group->meth != point->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		goto err;
	}
	ret = group->meth->point_get_affine_coordinates(group, point, x, y, ctx);

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(EC_POINT_get_affine_coordinates);

int
EC_POINT_get_affine_coordinates_GFp(const EC_GROUP *group, const EC_POINT *point,
    BIGNUM *x, BIGNUM *y, BN_CTX *ctx)
{
	return EC_POINT_get_affine_coordinates(group, point, x, y, ctx);
}
LCRYPTO_ALIAS(EC_POINT_get_affine_coordinates_GFp);

int
EC_POINT_set_compressed_coordinates(const EC_GROUP *group, EC_POINT *point,
    const BIGNUM *x, int y_bit, BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	int ret = 0;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth->point_set_compressed_coordinates == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}
	if (group->meth != point->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		goto err;
	}
	ret = group->meth->point_set_compressed_coordinates(group, point,
	    x, y_bit, ctx);

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(EC_POINT_set_compressed_coordinates);

int
EC_POINT_set_compressed_coordinates_GFp(const EC_GROUP *group, EC_POINT *point,
    const BIGNUM *x, int y_bit, BN_CTX *ctx)
{
	return EC_POINT_set_compressed_coordinates(group, point, x, y_bit, ctx);
}
LCRYPTO_ALIAS(EC_POINT_set_compressed_coordinates_GFp);

int
EC_POINT_add(const EC_GROUP *group, EC_POINT *r, const EC_POINT *a,
    const EC_POINT *b, BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	int ret = 0;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth->add == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}
	if (group->meth != r->meth || group->meth != a->meth ||
	    group->meth != b->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		goto err;
	}
	ret = group->meth->add(group, r, a, b, ctx);

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(EC_POINT_add);

int
EC_POINT_dbl(const EC_GROUP *group, EC_POINT *r, const EC_POINT *a,
    BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	int ret = 0;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth->dbl == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}
	if (group->meth != r->meth || r->meth != a->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		goto err;
	}
	ret = group->meth->dbl(group, r, a, ctx);

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(EC_POINT_dbl);

int
EC_POINT_invert(const EC_GROUP *group, EC_POINT *a, BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	int ret = 0;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth->invert == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}
	if (group->meth != a->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		goto err;
	}
	ret = group->meth->invert(group, a, ctx);

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(EC_POINT_invert);

int
EC_POINT_is_at_infinity(const EC_GROUP *group, const EC_POINT *point)
{
	if (group->meth->is_at_infinity == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		return 0;
	}
	if (group->meth != point->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		return 0;
	}
	return group->meth->is_at_infinity(group, point);
}
LCRYPTO_ALIAS(EC_POINT_is_at_infinity);

int
EC_POINT_is_on_curve(const EC_GROUP *group, const EC_POINT *point,
    BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	int ret = -1;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth->is_on_curve == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}
	if (group->meth != point->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		goto err;
	}
	ret = group->meth->is_on_curve(group, point, ctx);

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(EC_POINT_is_on_curve);

int
EC_POINT_cmp(const EC_GROUP *group, const EC_POINT *a, const EC_POINT *b,
    BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	int ret = -1;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth->point_cmp == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}
	if (group->meth != a->meth || a->meth != b->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		goto err;
	}
	ret = group->meth->point_cmp(group, a, b, ctx);

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(EC_POINT_cmp);

int
EC_POINT_make_affine(const EC_GROUP *group, EC_POINT *point, BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	int ret = 0;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth->make_affine == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}
	if (group->meth != point->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		goto err;
	}
	ret = group->meth->make_affine(group, point, ctx);

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(EC_POINT_make_affine);

int
EC_POINTs_make_affine(const EC_GROUP *group, size_t num, EC_POINT *points[],
    BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	size_t i;
	int ret = 0;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth->points_make_affine == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}
	for (i = 0; i < num; i++) {
		if (group->meth != points[i]->meth) {
			ECerror(EC_R_INCOMPATIBLE_OBJECTS);
			goto err;
		}
	}
	ret = group->meth->points_make_affine(group, num, points, ctx);

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(EC_POINTs_make_affine);

int
EC_POINTs_mul(const EC_GROUP *group, EC_POINT *r, const BIGNUM *scalar,
    size_t num, const EC_POINT *points[], const BIGNUM *scalars[],
    BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	int ret = 0;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	/* Only num == 0 and num == 1 is supported. */
	if (group->meth->mul_generator_ct == NULL ||
	    group->meth->mul_single_ct == NULL ||
	    group->meth->mul_double_nonct == NULL ||
	    num > 1) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}

	if (num == 1 && points != NULL && scalars != NULL) {
		/* Either bP or aG + bP, this is sane. */
		ret = EC_POINT_mul(group, r, scalar, points[0], scalars[0], ctx);
	} else if (scalar != NULL && points == NULL && scalars == NULL) {
		/* aG, this is sane */
		ret = EC_POINT_mul(group, r, scalar, NULL, NULL, ctx);
	} else {
		/* anything else is an error */
		ECerror(ERR_R_EC_LIB);
		goto err;
	}

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(EC_POINTs_mul);

int
EC_POINT_mul(const EC_GROUP *group, EC_POINT *r, const BIGNUM *g_scalar,
    const EC_POINT *point, const BIGNUM *p_scalar, BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	int ret = 0;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth->mul_generator_ct == NULL ||
	    group->meth->mul_single_ct == NULL ||
	    group->meth->mul_double_nonct == NULL) {
		ECerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		goto err;
	}

	if (g_scalar != NULL && point == NULL && p_scalar == NULL) {
		/*
		 * In this case we want to compute g_scalar * GeneratorPoint:
		 * this codepath is reached most prominently by (ephemeral) key
		 * generation of EC cryptosystems (i.e. ECDSA keygen and sign
		 * setup, ECDH keygen/first half), where the scalar is always
		 * secret. This is why we ignore if BN_FLG_CONSTTIME is actually
		 * set and we always call the constant time version.
		 */
		ret = group->meth->mul_generator_ct(group, r, g_scalar, ctx);
	} else if (g_scalar == NULL && point != NULL && p_scalar != NULL) {
		/*
		 * In this case we want to compute p_scalar * GenericPoint:
		 * this codepath is reached most prominently by the second half
		 * of ECDH, where the secret scalar is multiplied by the peer's
		 * public point. To protect the secret scalar, we ignore if
		 * BN_FLG_CONSTTIME is actually set and we always call the
		 * constant time version.
		 */
		ret = group->meth->mul_single_ct(group, r, p_scalar, point, ctx);
	} else if (g_scalar != NULL && point != NULL && p_scalar != NULL) {
		/*
		 * In this case we want to compute
		 *   g_scalar * GeneratorPoint + p_scalar * GenericPoint:
		 * this codepath is reached most prominently by ECDSA signature
		 * verification. So we call the non-ct version.
		 */
		ret = group->meth->mul_double_nonct(group, r, g_scalar,
		    p_scalar, point, ctx);
	} else {
		/* Anything else is an error. */
		ECerror(ERR_R_EC_LIB);
		goto err;
	}

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(EC_POINT_mul);

int
EC_GROUP_precompute_mult(EC_GROUP *group, BN_CTX *ctx_in)
{
	return 1;
}
LCRYPTO_ALIAS(EC_GROUP_precompute_mult);

int
EC_GROUP_have_precompute_mult(const EC_GROUP *group)
{
	return 0;
}
LCRYPTO_ALIAS(EC_GROUP_have_precompute_mult);

int
ec_group_simple_order_bits(const EC_GROUP *group)
{
	/* XXX change group->order to a pointer? */
#if 0
	if (group->order == NULL)
		return 0;
#endif
	return BN_num_bits(&group->order);
}
