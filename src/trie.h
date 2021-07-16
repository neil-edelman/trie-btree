/** @license 2020 Neil Edelman, distributed under the terms of the
 [MIT License](https://opensource.org/licenses/MIT).

 @subtitle Prefix Tree

 ![Example of trie.](../web/trie.png)

 A <tag:<T>trie> is a prefix tree, digital tree, or trie, implemented as an
 array of pointers-to-`T` whose keys are always in lexicographically-sorted
 order. It can be seen as a <Morrison, 1968 PATRICiA>: a compact
 [binary radix trie](https://en.wikipedia.org/wiki/Radix_tree), only
 storing the where the keys are different. Strings can be any encoding with a
 byte null-terminator, including
 [modified UTF-8](https://en.wikipedia.org/wiki/UTF-8#Modified_UTF-8).

 These are similar to <Bayer, McCreight, 1972 Large (B-Trees)> in memory. Using
 <Knuth, 1998 Art 3> terms, instead of a B-tree of order-n nodes, a trie
 is a B-forest of max-257-leaf complete binary trees, (with max-256 two-byte
 branches.)

 @param[TRIE_NAME, TRIE_TYPE]
 <typedef:<PT>type> that satisfies `C` naming conventions when mangled and an
 optional returnable type that is declared, (it is used by reference only
 except if `TRIE_TEST`.) `<PT>` is private, whose names are prefixed in a
 manner to avoid collisions.

 @param[TRIE_KEY]
 A function that satisfies <typedef:<PT>key_fn>. Must be defined if and only if
 `TRIE_TYPE` is defined.

 @param[TRIE_TO_STRING]
 Defining this includes <to_string.h>, with the keys as the string.

 @param[TRIE_TEST]
 Unit testing framework <fn:<T>trie_test>, included in a separate header,
 <../test/test_trie.h>. Must be defined equal to a (random) filler function,
 satisfying <typedef:<PT>action_fn>. Requires that `NDEBUG` not be defined
 and `TRIE_ITERATE_TO_STRING`.

 @std C89 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <limits.h> /* Works `CHAR_BIT != 8`? Anyone have a TI compiler? */


#ifndef TRIE_NAME
#error Name TRIE_NAME undefined.
#endif
#if defined(TRIE_TYPE) ^ defined(TRIE_KEY)
#error TRIE_TYPE and TRIE_KEY have to be mutually defined.
#endif


#ifndef TRIE_H /* <!-- idempotent */
#define TRIE_H
/* http://c-faq.com/misc/bitsets.html */
#define TRIE_BITMASK(n) (1 << ((n) % CHAR_BIT))
#define TRIE_BITSLOT(n) ((n) / CHAR_BIT)
#define TRIE_BITTEST(a, n) ((a)[TRIE_BITSLOT(n)] & TRIE_BITMASK(n))
#define TRIE_BITDIFF(a, b, n) (((a)[TRIE_BITSLOT(n)] ^ (b)[TRIE_BITSLOT(n)]) \
	& ((1 << (CHAR_BIT - 1)) >> ((n) % CHAR_BIT)))
/* Worst-case all-left, `[0,max(tree.left>=255)]`. Fits nicely in struct. */
#define TRIE_MAX_LEFT 254
#define TRIE_MAX_BRANCH (TRIE_MAX_LEFT + 1)
#define TRIE_ORDER (TRIE_MAX_BRANCH + 1) /* Maximum branching factor. */
/** @return Whether `a` and `b` are equal up to the minimum of their lengths'.
 Used in <fn:<T>trie_prefix>. */
static int trie_is_prefix(const char *a, const char *b) {
	for( ; ; a++, b++) {
		if(*a == '\0') return 1;
		if(*a != *b) return *b == '\0';
	}
}
struct trie_branch { unsigned char left, skip; };
/* This is a bit field defined in <fn:<PT>extract>, but bit fields on `short`
 may be undefined, or may widen the value, who knows. */
struct trie_info { unsigned short info; };
/* X-macros: `C90` doesn't allow trialing commas in initializer lists. */
#define TRIE_STORE_TAIL_X \
	X(1, 4) X(2, 8) X(3, 16) X(4, 32) X(5, 64) X(6, 128) X(7, 256)
#define TRIE_STORE_X \
	X(0, 1) TRIE_STORE_TAIL_X
#define TRIE_STORE_XCSV \
	X(0, 1), X(1, 4), X(2, 8), X(3, 16), X(4, 32), X(5, 64), \
	X(6, 128), X(7, 256)
#endif /* idempotent --> */


/* <Kernighan and Ritchie, 1988, p. 231>. */
#if defined(T_) || defined(PT_) \
	|| (defined(TRIE_SUBTYPE) ^ (defined(CAT) || defined(CAT_)))
#error Unexpected P?T_ or CAT_?; possible stray TRIE_EXPECT_TRAIT?
#endif
#ifndef TRIE_SUBTYPE /* <!-- !sub-type */
#define CAT_(x, y) x ## _ ## y
#define CAT(x, y) CAT_(x, y)
#endif /* !sub-type --> */
#define T_(thing) CAT(TRIE_NAME, thing)
#define PT_(thing) CAT(trie, T_(thing))


/* Default values for string. */
#ifndef TRIE_TYPE /* <!-- !type */
static char *PT_(raw)(char **a) { return assert(a), *a; }
#define TRIE_TYPE char
#define TRIE_KEY &PT_(raw)
#endif /* !type --> */

/** Declared type of the trie; defaults to `char`. */
typedef TRIE_TYPE PT_(type);

/** Pointers to generic trees stored in memory, and part of the B-forest.
 Points to a non-empty semi-implicit complete binary tree of a
 fixed-maximum-size; reading `key.info` will tell which tree it is. */
union PT_(any_store) {
	struct trie_info *key;
#define X(n, m) struct PT_(store##n) *s##n;
	TRIE_STORE_X
#undef X
};

/** A leaf is either data at the base of the tree at the base of the B-forest
 that is the trie or another tree-link, and `is_internal` in `info` allows
 differentiation. */
union PT_(leaf) { PT_(type) *data; union PT_(any_store) child; };

/* Different width trees, designed to fit alignment (and cache) boundaries. */
struct PT_(store0) { struct trie_info info; char unused[6];
	union PT_(leaf) leaves[1]; }; /* Except this one wastes space. */
#define X(n, m) struct PT_(store##n) { struct trie_info info; \
	struct trie_branch branches[m - 1]; union PT_(leaf) leaves[m]; };
TRIE_STORE_TAIL_X
#undef X
/* We could go one more, but that ruins the alignment. Is it worth it? Maybe
 255 could be a special value that allows strings with long matches. */

/** A working tree, extracted from tight storage by <fn:<PT>extract>. */
struct PT_(tree) {
	unsigned /*is_allocated,*/ is_internal, store, bsize;
	struct trie_branch *branches;
	union PT_(leaf) *leaves;
};

/** To initialize it to an idle state, see <fn:<T>trie>, `TRIE_IDLE`, `{0}`
 (`C99`), or being `static`.

 ![States.](../web/states.png) */
struct T_(trie) { union PT_(any_store) root; };
#ifndef TRIE_IDLE /* <!-- !zero */
#define TRIE_IDLE { { 0 } }
#endif /* !zero --> */

/** A bi-predicate; returns true if the `replace` replaces the `original`; used
 in <fn:<T>trie_policy_put>. */
typedef int (*PT_(replace_fn))(PT_(type) *original, PT_(type) *replace);

/** Responsible for picking out the null-terminated string. Modifying the
 string while in any trie causes the trie to go into an undefined state. */
static const char *(*PT_(to_key))(const PT_(type) *a) = (TRIE_KEY);

/** @return False. Ignores `a` and `b`. @implements <typedef:<PT>replace_fn> */
static int PT_(false_replace)(PT_(type) *const a, PT_(type) *const b)
	{ return (void)a, (void)b, 0; }

/** For `any`, outputs `tree` for the kind of tree. */
static void PT_(extract)(const union PT_(any_store) any,
	struct PT_(tree) *const tree) {
	const unsigned short info = any.key->info;
	assert(any.key && tree);
	/* [is_allocated:1][is_internal:1][store:3][bsize:9] */
	/*tree->is_allocated = !!(info & 0x2000);*/
	tree->is_internal = !!(info & 0x1000);
	tree->store = (info >> 9) & 7, assert(tree->store <= 7);
	tree->bsize = info & 0x1FF, assert(tree->bsize <= TRIE_MAX_BRANCH);
	switch(tree->store) {
	case 0: tree->branches = 0; tree->leaves = any.s0->leaves; break;
#define X(n, m) case n: tree->branches = any.s##n->branches; \
	tree->leaves = any.s##n->leaves; break;
		TRIE_STORE_TAIL_X
#undef X
	default: assert(0);
	}
}

/** Compares keys of `a` and `b`. Used in <fn:<T>trie_from_array>.
 @implements <typedef:<PT>bipredicate_fn> */
static int PT_(compare)(const PT_(type) *const a, const PT_(type) *const b)
	{ return strcmp(PT_(to_key)(a), PT_(to_key)(b)); }

#ifdef TRIE_TO_STRING /* <!-- str */
/** Uses the natural `datum` -> `a` that is defined by `TRIE_KEY`. */
static void PT_(to_string)(PT_(type) *const a, char (*const z)[12])
	{ assert(a && z); sprintf(*z, "%.11s", PT_(to_key)(a)); }
#endif /* str --> */

/** @return Looks at only the index of `trie` for potential `key` matches,
 otherwise `key` is definitely not in `trie`. @order \O(`key.length`) */
static PT_(type) *PT_(match)(const struct T_(trie) *const trie,
	const char *const key) {
	union PT_(any_store) store = trie->root;
	struct PT_(tree) tree;
	const struct trie_branch *branch;
	size_t bit; /* `bit \in key`.  */
	struct { unsigned br0, br1, lf; } in_tree;
	struct { size_t cur, next; } byte; /* `key` null checks. */
	assert(trie && key);
	if(!store.key) return 0; /* Idle. */
	for(byte.cur = 0, bit = 0; ; ) { /* B-forest. */
		PT_(extract)(store, &tree);
		in_tree.br0 = 0, in_tree.br1 = tree.bsize, in_tree.lf = 0;
		while(in_tree.br0 < in_tree.br1) { /* Binary tree. */
			branch = tree.branches + in_tree.br0;
			for(byte.next = (bit += branch->skip) >> 3; byte.cur < byte.next;
				byte.cur++) if(key[byte.cur] == '\0') return 0; /* Too short. */
			if(!TRIE_BITTEST(key, bit))
				in_tree.br1 = ++in_tree.br0 + branch->left;
			else
				in_tree.br0 += branch->left + 1, in_tree.lf += branch->left + 1;
			bit++;
		}
		if(!tree.is_internal) break;
		store = tree.leaves[in_tree.lf].child;
	};
	return tree.leaves[in_tree.lf].data;
}

/** @return Exact match for `key` in `trie` or null. (fixme: private?) */
static PT_(type) *PT_(get)(const struct T_(trie) *const trie,
	const char *const key) {
	PT_(type) *n;
	return (n = PT_(match)(trie, key)) && !strcmp(PT_(to_key)(n), key) ? n : 0;
}

/** Expand `any` to ensure that it has one more unused capacity.
 @return Potentially a new tree. @throws[realloc] */
static const union PT_(any_store) *PT_(expand)(const union PT_(any_store) any) {
	struct PT_(tree) tree;
	assert(any.key);
	PT_(extract)(any, &tree);
	////
	return 0;
}

/** @return The leftmost key `lf` of key `any`. */
static const char *PT_(sample)(union PT_(any_store) any, unsigned lf) {
	struct PT_(tree) tree;
	assert(any.key);
	while(PT_(extract)(any, &tree), tree.is_internal)
		any = tree.leaves[lf].child, lf = 0;
	return PT_(to_key)(tree.leaves[lf].data);
}

static int PT_(add_unique)(struct T_(trie) *const trie, PT_(type) *const x) {
	const char *const x_key = PT_(to_key)(x);
	struct { size_t x, x0, x1; } in_bit;
	struct { union PT_(any_store) *ref, any; size_t start_bit; } in_forest;
	struct { unsigned br0, br1, lf; } in_tree;
	struct PT_(tree) tree;
	struct trie_branch *branch;
	union leaf *leaf;
	const char *sample;
	int is_write = 0, is_right = 0, is_split = 0;

	printf("inserting %s.\n", x_key);
	assert(trie && x);
	if(!trie->root.key) { /* Empty special case. */
		struct PT_(store0) *const s0 = malloc(sizeof *s0);
		if(!s0) { if(!errno) errno = ERANGE; return 0; }
		s0->info.info = 0, s0->leaves[0].data = x;
		trie->root.s0 = s0;
		return 1;
	}
	/* Trees in the B-forest. */
	in_bit.x = in_forest.start_bit = 0, in_forest.ref = &trie->root,
		in_forest.any = *in_forest.ref;
	do {
		PT_(extract)(in_forest.any, &tree);
tree:
		sample = PT_(sample)(in_forest.any, 0);
		in_bit.x0 = in_bit.x;
		/* Descend branches. */
		in_tree.br0 = 0, in_tree.br1 = tree.bsize, in_tree.lf = 0;
		while(in_tree.br0 < in_tree.br1) {
			branch = tree.branches + in_tree.br0;
			/* Test all the skip bits. */
			for(in_bit.x1 = in_bit.x + branch->skip; in_bit.x < in_bit.x1;
				in_bit.x++) if(TRIE_BITDIFF(x_key, sample, in_bit.x)) goto leaf;
			/* Decision bit. */
			if(!TRIE_BITTEST(x_key, in_bit.x)) {
				in_tree.br1 = ++in_tree.br0 + branch->left;
				if(is_write) branch->left++;
			} else {
				in_tree.br0 += branch->left + 1;
				in_tree.lf  += branch->left + 1;
				sample = PT_(sample)(in_forest.any, in_tree.lf);
			}
			in_bit.x0 = in_bit.x1, in_bit.x++;
		}
		assert(in_tree.br0 == in_tree.br1 && in_tree.lf <= tree.bsize);
	} while(tree.is_internal
		&& (in_forest.any = tree.leaves[in_tree.lf].child, 1));
	/* Got to the leaves. */
	in_bit.x1 = in_bit.x + UCHAR_MAX;
	while(!TRIE_BITDIFF(x_key, sample, in_bit.x))
		if(++in_bit.x > in_bit.x1) return errno = ERANGE, 0;

leaf:
	if(TRIE_BITTEST(x_key, in_bit.x))
		is_right = 1, in_tree.lf += in_tree.br1 - in_tree.br0 + 1;
	printf("insert %s, at index %u bit %lu, %s.\n", x_key, in_tree.lf, in_bit.x,
		is_right ? "right" : "left");
	assert(in_tree.lf <= tree.bsize + 1u);

	if(is_write) goto insert;
	/* If the tree is full, split it. */
	assert(tree.bsize <= TRIE_MAX_BRANCH);
	if(tree.bsize == TRIE_MAX_BRANCH) {
		printf("Splitting tree %p.\n", (void *)in_forest.any.key);
		assert(!is_split);
		/*if(!trie_split(trie, in_forest.idx)) return 0;*/
		assert(is_split = 1);
		/*printf("Returning to \"%s\" in tree %lu.\n", key, in_forest.idx);*/
	} else {
		/* Now we are sure that this tree is the one getting modified. */
		/* fixme: but also we don't know whether the width is the maximum */
		is_write = 1;
	}
	in_bit.x = in_forest.start_bit;
	goto tree;

insert:
#if 0
	leaf = tree->leaves + in_tree.lf;
	memmove(leaf + 1, leaf, sizeof *leaf * (tree->bsize + 1 - in_tree.lf));
	leaf->data = key;
	bmp_insert(tree->link, in_tree.lf);
	branch = tree->branches + in_tree.br0;
	if(in_tree.br0 != in_tree.br1) { /* Split `skip` with the existing branch. */
		assert(in_bit.b0 <= in_bit.b
			&& in_bit.b + !in_tree.br0 <= in_bit.b0 + branch->skip);
		branch->skip -= in_bit.b - in_bit.b0 + !in_tree.br0;
	}
	memmove(branch + 1, branch, sizeof *branch * (tree->bsize - in_tree.br0));
	assert(in_tree.br1 - in_tree.br0 < 256
		&& in_bit.b >= in_bit.b0 + !!in_tree.br0
		&& in_bit.b - in_bit.b0 - !!in_tree.br0 < 256);
	branch->left = is_right ? (unsigned char)(in_tree.br1 - in_tree.br0) : 0;
	branch->skip = (unsigned char)(in_bit.b - in_bit.b0 - !!in_tree.br0);
	tree->bsize++;
#endif

	return 1;
}

/** Initialises `trie` to idle. @order \Theta(1) @allow */
static void T_(trie)(struct T_(trie) *const trie)
	{ assert(trie); trie->root.key = 0; }

/** Returns an initialised `trie` to idle. @allow */
static void T_(trie_)(struct T_(trie) *const trie) {
	assert(trie); /* fixme */
	T_(trie)(trie);
}

/** Sets `trie` to be empty. That is, the size of `trie` will be zero, but if
 it was previously in an active non-idle state, it continues to be.
 @order \Theta(1) @allow */
static void T_(trie_clear)(struct T_(trie) *const trie)
	{ assert(trie); /* ... */}

/** @return The <typedef:<PT>type> with `key` in `trie` or null no such item
 exists. @order \O(|`key`|), <Thareja 2011, Data>. @allow */
static PT_(type) *T_(trie_get)(const struct T_(trie) *const trie,
	const char *const key) { return assert(trie && key), PT_(get)(trie, key); }


/** @return If `x` is already in `trie`, returns false, otherwise success.
 @throws[realloc, ERANGE] */
static int T_(trie_add)(struct T_(trie) *const trie, PT_(type) *const x) {
	return assert(trie && x),
		PT_(get)(trie, PT_(to_key)(x)) ? 0 : PT_(add_unique)(trie, x);
}

#if 0
/** Looks at only the index for potential matches.
 @param[result] A index pointer to leaves that matches `key` when true.
 @return True if `key` in `trie` has matched, otherwise `key` is definitely is
 not in `trie`. @order \O(`key.length`) */
static int PT_(param_index_get)(const struct T_(trie) *const trie,
	const char *const key, size_t *const result) {
	size_t n0 = 0, n1 = trie->leaves.size, i = 0, left;
	TrieBranch branch;
	size_t n0_byte, str_byte = 0, bit = 0;
	assert(trie && key && result);
	if(!n1) return 0; /* Special case: there is nothing to match. */
	n1--, assert(n1 == trie->branches.size);
	while(n0 < n1) {
		branch = trie->branches.data[n0];
		bit += trie_skip(branch);
		/* Skip the don't care bits, ending up at the decision bit. */
		for(n0_byte = bit >> 3; str_byte < n0_byte; str_byte++)
			if(key[str_byte] == '\0') return 0;
		left = trie_left(branch);
		if(!trie_is_bit(key, bit)) n1 = ++n0 + left;
		else n0 += left + 1, i += left + 1;
		bit++;
	}
	assert(n0 == n1 && i < trie->leaves.size);
	*result = i;
	return 1;
}

/** @return True if found the exact `key` in `trie` and stored it's index in
 `result`. */
static int PT_(param_get)(const struct T_(trie) *const trie,
	const char *const key, size_t *const result) {
	return PT_(param_index_get)(trie, key, result)
		&& !strcmp(PT_(to_key)(trie->leaves.data[*result]), key);
}

/** @return `trie` entry that matches bits of `key`, (ignoring the don't care
 bits,) or null if either `key` didn't have the length to fully differentiate
 more then one entry or the `trie` is empty. */
static PT_(type) *PT_(index_get)(const struct T_(trie) *const trie,
	const char *const key) {
	size_t i;
	return PT_(param_index_get)(trie, key, &i) ? trie->leaves.data[i] : 0;
}

/** In `trie`, which must be non-empty, given a partial `prefix`, stores all
 leaf prefix matches between `low`, `high`, only given the index, ignoring
 don't care bits. @order \O(`prefix.length`) @allow */
static void T_(trie_index_prefix)(const struct T_(trie) *const trie,
	const char *const prefix, size_t *const low, size_t *const high) {
	size_t n0 = 0, n1 = trie->leaves.size, i = 0, left;
	TrieBranch branch;
	size_t n0_byte, str_byte = 0, bit = 0;
	assert(trie && prefix && low && high && n1);
	n1--, assert(n1 == trie->branches.size);
	while(n0 < n1) {
		branch = trie->branches.data[n0];
		bit += trie_skip(branch);
		/* _Sic_; '\0' is _not_ included for partial match. */
		for(n0_byte = bit >> 3; str_byte <= n0_byte; str_byte++)
			if(prefix[str_byte] == '\0') goto finally;
		left = trie_left(branch);
		if(!trie_is_bit(prefix, bit)) n1 = ++n0 + left;
		else n0 += left + 1, i += left + 1;
		bit++;
	}
	assert(n0 == n1);
finally:
	assert(n0 <= n1 && i - n0 + n1 < trie->leaves.size);
	*low = i, *high = i - n0 + n1;
}

/** @return Whether, in `trie`, given a partial `prefix`, it has found `low`,
 `high` prefix matches. */
static int T_(trie_prefix)(const struct T_(trie) *const trie,
	const char *const prefix, size_t *const low, size_t *const high) {
	assert(trie && prefix && low && high);
	return trie->leaves.size ? (T_(trie_index_prefix)(trie, prefix, low, high),
		trie_is_prefix(prefix, PT_(to_key)(trie->leaves.data[*low]))) : 0;
}

/** Adds `datum` to `trie` and, if `eject` is non-null, stores the collided
 element, if any, as long as `replace` is null or returns true.
 @param[eject] If not-null, the ejected datum. If `replace` returns false, then
 `*eject == datum`, but it will still return true.
 @return Success. @throws[realloc, ERANGE] */
static int PT_(put)(struct T_(trie) *const trie, PT_(type) *const datum,
	PT_(type) **const eject, const PT_(replace_fn) replace) {
	const char *data_key;
	PT_(leaf) *match;
	size_t i;
	assert(trie && datum);
	data_key = PT_(to_key)(datum);
	/* Add if absent. */
	if(!PT_(param_get)(trie, data_key, &i)) {
		if(eject) *eject = 0;
		return PT_(add)(trie, datum);
	}
	assert(i < trie->leaves.size), match = trie->leaves.data + i;
	/* Collision policy. */
	if(replace && !replace(*match, datum)) {
		if(eject) *eject = datum;
	} else {
		if(eject) *eject = *match;
		*match = datum;
	}
	return 1;
}

/** Adds `datum` to `trie` if absent.
 @param[trie, datum] If null, returns null.
 @return Success. If data with the same key is present, returns true but
 doesn't add `datum`.
 @throws[realloc] There was an error with a re-sizing.
 @throws[ERANGE] The key is greater then 510 characters or the trie has reached
 it's maximum size. @order \O(`size`) @allow */
static int T_(trie_add)(struct T_(trie) *const trie, PT_(type) *const datum) {
	return assert(trie && datum), PT_(put)(trie, datum, 0, &PT_(false_replace));
}

/** Updates or adds `datum` to `trie`.
 @param[trie, datum] If null, returns null.
 @param[eject] If not null, on success it will hold the overwritten value or
 a pointer-to-null if it did not overwrite.
 @return Success.
 @throws[realloc] There was an error with a re-sizing.
 @throws[ERANGE] The key is greater then 510 characters or the trie has reached
 it's maximum size. @order \O(`size`) @allow */
static int T_(trie_put)(struct T_(trie) *const trie,
	PT_(type) *const datum, PT_(type) **const eject) {
	return assert(trie && datum), PT_(put)(trie, datum, eject, 0);
}

/** Adds `datum` to `trie` only if the entry is absent or if calling `replace`
 returns true.
 @param[eject] If not null, on success it will hold the overwritten value or
 a pointer-to-null if it did not overwrite a previous value. If a collision
 occurs and `replace` does not return true, this value will be `data`.
 @param[replace] Called on collision and only replaces it if the function
 returns true. If null, it is semantically equivalent to <fn:<T>trie_put>.
 @return Success. @throws[realloc] There was an error with a re-sizing.
 @throws[ERANGE] The key is greater then 510 characters or the trie has reached
 it's maximum size. @order \O(`size`) @allow */
static int T_(trie_policy_put)(struct T_(trie) *const trie,
	PT_(type) *const datum, PT_(type) **const eject,
	const PT_(replace_fn) replace) {
	return assert(trie && datum), PT_(put)(trie, datum, eject, replace);
}

/** @return Whether leaf index `i` has been removed from `trie`.
 @fixme There is nothing stopping an `assert` from being triggered. */
static int PT_(index_remove)(struct T_(trie) *const trie, size_t i) {
	size_t n0 = 0, n1 = trie->branches.size, parent_n0, left;
	size_t *parent, *twin; /* Branches. */
	assert(trie && i < trie->leaves.size
		&& trie->branches.size + 1 == trie->leaves.size);
	/* Remove leaf. */
	if(!--trie->leaves.size) return 1; /* Special case of one leaf. */
	memmove(trie->leaves.data + i, trie->leaves.data + i + 1,
		sizeof trie->leaves.data * (n1 - i));
	/* fixme: Do another descent _not_ modifying to see if the values can be
	 combined without overflow. */
	/* Remove branch. */
	for( ; ; ) {
		left = trie_left(*(parent = trie->branches.data + (parent_n0 = n0)));
		if(i <= left) { /* Pre-order binary search. */
			if(!left) { twin = n0 + 1 < n1 ? trie->branches.data + n0 + 1 : 0;
				break; }
			n1 = ++n0 + left;
			trie_left_dec(parent);
		} else {
			if((n0 += left + 1) >= n1)
				{ twin = left ? trie->branches.data + n0 - left : 0; break; }
			i -= left + 1;
		}
	}
	/* Merge `parent` with `sibling` before deleting `parent`. */
	if(twin)
		/* fixme: There is nothing to guarantee this. */
		assert(trie_skip(*twin) < TRIE_SKIP_MAX - trie_skip(*parent)),
		trie_skip_set(twin, trie_skip(*twin) + 1 + trie_skip(*parent));
	memmove(parent, parent + 1, sizeof n0 * (--trie->branches.size -parent_n0));
	return 1;
}

/** Remove `key` from `trie`. @return Success or else `key` was not in `trie`.
 @order \O(`size`) @allow */
static int T_(trie_remove)(struct T_(trie) *const trie, const char *const key) {
	size_t i;
	assert(trie && key);
	return PT_(param_get)(trie, key, &i) && PT_(index_remove)(trie, i);
}
#endif

static void PT_(unused_base_coda)(void);
static void PT_(unused_base)(void) {
	T_(trie)(0); T_(trie_)(0);
	T_(trie_get)(0, 0);
	PT_(unused_base_coda)();
}
static void PT_(unused_base_coda)(void) { PT_(unused_base)(); }


#if 0

/** Recursive function used for <fn:<PT>init>. Initialise branches of `trie` up
 to `bit` with `a` to `a_size` array of sorted leaves.
 @order Speed \O(`a_size` log E(`a.length`))?; memory \O(E(`a.length`)). */
static void PT_(init_branches_r)(struct T_(trie) *const trie, size_t bit,
	const size_t a, const size_t a_size) {
	size_t b = a, b_size = a_size, half;
	size_t skip = 0;
	TrieBranch *branch;
	assert(trie && a_size && a_size <= trie->leaves.size && trie->leaves.size
		&& trie->branches.capacity >= trie->leaves.size - 1);
	if(a_size <= 1) return;
	/* Endpoints of sorted range: skip [_1_111...] or [...000_0_] don't care.
	 fixme: UINT_MAX overflow. */
	while(trie_is_bit(PT_(to_key)(trie->leaves.data[a]), bit)
		|| !trie_is_bit(PT_(to_key)(trie->leaves.data[a + a_size - 1]), bit))
		bit++, skip++;
	/* Do a binary search for the first `leaves[a+half_s]#bit == 1`. */
	while(b_size) half = b_size >> 1,
		trie_is_bit(PT_(to_key)(trie->leaves.data[b + half]), bit)
		? b_size = half : (half++, b += half, b_size -= half);
	b_size = b - a;
	/* Should have space for all branches pre-allocated in <fn:<PT>init>. */
	branch = trie_branch_array_new(&trie->branches), assert(branch);
	*branch = trie_branch(skip, b_size - 1);
	bit++;
	PT_(init_branches_r)(trie, bit, a, b_size);
	PT_(init_branches_r)(trie, bit, b, a_size - b_size);
}

/** Initializes `trie` to `a` of size `a_size`, which cannot be zero.
 @return Success. @throws[ERANGE, malloc] */
static int PT_(init)(struct T_(trie) *const trie, PT_(type) *const*const a,
	const size_t a_size) {
	PT_(leaf) *leaves;
	assert(trie && a && a_size);
	T_(trie)(trie);
	/* This will store space for all of the duplicates, as well. */
	if(!A_(array_reserve)(&trie->leaves, a_size)
		|| !trie_branch_array_reserve(&trie->branches, a_size - 1)) return 0;
	leaves = trie->leaves.data;
	memcpy(leaves, a, sizeof *a * a_size);
	trie->leaves.size = a_size;
	/* Sort, get rid of duplicates, and initialize branches, from `compare.h`. */
	qsort(leaves, a_size, sizeof *a, &PA_(vcompar));
	A_(array_unique)(&trie->leaves);
	PT_(init_branches_r)(trie, 0, 0, trie->leaves.size);
	assert(trie->branches.size + 1 == trie->leaves.size);
	return 1;
}

/** Initializes `trie` from an `array` of pointers-to-`<T>` of `array_size`.
 @return Success. @throws[realloc] @order \O(`array_size`) @allow */
static int T_(trie_from_array)(struct T_(trie) *const trie,
	PT_(type) *const*const array, const size_t array_size) {
	return assert(trie && array && array_size),
		PT_(init)(trie, array, array_size);
}

#endif


#ifdef TRIE_TO_STRING /* <!-- string */

static const char *T_(trie_to_string)(const struct T_(trie) *const trie) {
	(void)trie;
	return "foo";
}

#if defined(TRIE_TEST) /* <!-- test */
#include "../test/test_trie.h" /** \include */
#endif /* test --> */

#undef TRIE_TO_STRING
	
#endif /* string --> */

#ifdef TRIE_EXPECT_TRAIT /* <!-- trait */
#undef TRIE_EXPECT_TRAIT
#else /* trait --><!-- !trait */
#ifndef TRIE_SUBTYPE /* <!-- !sub-type */
#undef CAT
#undef CAT_
#else /* !sub-type --><!-- sub-type */
#undef TRIE_SUBTYPE
#endif /* sub-type --> */
#undef T_
#undef PT_
#undef TRIE_NAME
#undef TRIE_TYPE
#undef TRIE_KEY
#ifdef TRIE_TEST
#undef TRIE_TEST
#endif
#endif /* !trait --> */

#undef TRIE_TRAITS
