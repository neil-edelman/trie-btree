#include <stdio.h>
#include <string.h>
#include "orcish.h"

#if defined(QUOTE) || defined(QUOTE_)
#error QUOTE_? cannot be defined.
#endif
#define QUOTE_(name) #name
#define QUOTE(name) QUOTE_(name)

/** Works by side-effects, _ie_ fills the type with data. Only defined if
 `TRIE_TEST`. */
typedef void (*PT_(action_fn))(PT_(type) *);
/* Used in <fn:<PT>graph_choose>. */
typedef void (*PT_(tree_file_fn))(const struct PT_(tree) *, size_t, FILE *);

/* `TRIE_TEST` must be a function that implements <typedef:<PT>action_fn>. */
static void (*PT_(filler))(PT_(type) *) = (TRIE_TEST);

/* Debug number, which is the number printed next to the graphs, _etc_. */
static unsigned PT_(no);

/** Is `lf` going to the right in `tr`? */
static unsigned PT_(is_right)(const struct PT_(tree) *const tr,
	const unsigned lf) {
	struct { unsigned br0, br1, lf; } t;
	unsigned left, is_right = 0;
	t.br0 = 0, t.br1 = tr->bsize, t.lf = 0;
	while(t.br0 < t.br1) {
		left = tr->branch[t.br0].left;
		if(lf <= t.lf + left) t.br1 = ++t.br0 + left, is_right = 0;
		else t.br0 += left + 1, t.lf += left + 1, is_right = 1;
	}
	return is_right;
}

/** Given a branch `b` in `tree` branches, calculate the right child branches.
 @order \O(log `size`) */
static unsigned PT_(right)(const struct PT_(tree) *const tree,
	const unsigned b) {
	unsigned left, right, total = tree->bsize, b0 = 0;
	assert(tree && b < tree->bsize);
	for( ; ; ) {
		right = total - (left = tree->branch[b0].left) - 1;
		assert(left < total && right < total);
		if(b0 >= b) break;
		if(b <= b0 + left) total = left, b0++;
		else total = right, b0 += left + 1;
	}
	assert(b0 == b);
	return right;
}

/** @return Follows the branches to `b` in `tree` and returns the leaf. */
static unsigned PT_(left_leaf)(const struct PT_(tree) *const tree,
	const unsigned b) {
	unsigned left, right, total = tree->bsize, i = 0, b0 = 0;
	assert(tree && b < tree->bsize);
	for( ; ; ) {
		right = total - (left = tree->branch[b0].left) - 1;
		assert(left < tree->bsize && right < tree->bsize);
		if(b0 >= b) break;
		if(b <= b0 + left) total = left, b0++;
		else total = right, b0 += left + 1, i += left + 1;
	}
	assert(b0 == b);
	return i;
}

/** Graphs `tree` on `fp`. */
static void PT_(graph_tree_bits)(const struct PT_(tree) *const tree,
	const size_t treebit, FILE *const fp) {
	unsigned b, i;
	assert(tree && fp);
	fprintf(fp, "\ttree%pbranch0 [shape = box, style = filled, "
		"fillcolor=\"Grey95\" label = <\n"
		"<TABLE BORDER=\"0\" CELLBORDER=\"0\">\n", (const void *)tree);
	for(i = 0; i <= tree->bsize; i++) {
		const char *key = PT_(sample)(tree, i);
		const struct trie_branch *branch = tree->branch;
		size_t next_branch = treebit + branch->skip;
		const char *params, *start, *end;
		struct { unsigned br0, br1; } in_tree;
		unsigned is_child = trie_bmp_test(&tree->is_child, i);
		/* 0-width joiner "&#8288;": GraphViz gets upset when tag closed
		 immediately. */
		fprintf(fp, "\t<TR>\n"
			"\t\t<TD ALIGN=\"LEFT\" BORDER=\"0\""
			" PORT=\"%u\">%s%s%s⊔</FONT></TD>\n",
			i, is_child ? "↓<FONT COLOR=\"Gray\">" : "", key,
			is_child ? "" : "<FONT COLOR=\"Grey\">");
		in_tree.br0 = 0, in_tree.br1 = tree->bsize;
		for(b = 0; in_tree.br0 < in_tree.br1; b++) {
			const unsigned bit = !!TRIE_QUERY(key, b);
			if(next_branch) {
				next_branch--;
				params = "", start = "", end = "";
			} else {
				if(!bit) {
					in_tree.br1 = ++in_tree.br0 + branch->left;
					params = " BGCOLOR=\"White\" BORDER=\"1\"";
					start = "", end = "";
				} else {
					in_tree.br0 += branch->left + 1;
					params
						= " BGCOLOR=\"Black\" COLOR=\"White\" BORDER=\"1\"";
					start = "<FONT COLOR=\"White\">", end = "</FONT>";
				}
				next_branch = (branch = tree->branch + in_tree.br0)->skip;
			}
			if(b && !(b & 7)) fprintf(fp, "\t\t<TD BORDER=\"0\">&nbsp;</TD>\n");
			fprintf(fp, "\t\t<TD%s>%s%u%s</TD>\n", params, start, bit, end);
		}
		fprintf(fp, "\t</TR>\n");
	}
	fprintf(fp, "</TABLE>>];\n");
	/* Draw the lines between trees. */
	for(i = 0; i <= tree->bsize; i++) if(trie_bmp_test(&tree->is_child, i))
		fprintf(fp, "\ttree%pbranch0:%u -> tree%pbranch0 "
		"[style = dotted%s];\n", (const void *)tree, i,
		(const void *)tree->leaf[i].child,
		PT_(is_right)(tree, i) ? ", arrowhead = vee" : "");
	/* Recurse. */
	for(i = 0; i <= tree->bsize; i++) if(trie_bmp_test(&tree->is_child, i)) {
		struct { unsigned br0, br1, lf; } in_tree;
		size_t bit = treebit;
		in_tree.br0 = 0, in_tree.br1 = tree->bsize, in_tree.lf = 0;
		while(in_tree.br0 < in_tree.br1) {
			const struct trie_branch *branch = tree->branch + in_tree.br0;
			bit += branch->skip;
			if(i <= in_tree.lf + branch->left)
				in_tree.br1 = ++in_tree.br0 + branch->left;
			else
				in_tree.br0 += branch->left + 1, in_tree.lf += branch->left + 1;
			bit++;
		}
		PT_(graph_tree_bits)(tree->leaf[i].child, bit, fp);
	}
}

/** Graphs `any` on `fp`. */
static void PT_(graph_tree_mem)(const struct PT_(tree) *const tree,
	const size_t treebit, FILE *const fp) {
	const struct trie_branch *branch;
	unsigned b, i;
	(void)treebit;
	assert(tree && fp);
	/* Tree is one record node in memory -- GraphViz says html is
	 case-insensitive, but no. */
	fprintf(fp, "\ttree%pbranch0 [shape = box, "
		"style = filled, fillcolor = Gray95, label = <\n"
		"<TABLE BORDER=\"0\">\n"
		"\t<TR><TD COLSPAN=\"%u\" ALIGN=\"LEFT\">"
		"<FONT COLOR=\"Gray75\">b%lu, %s</FONT></TD></TR>\n"
		"\t<TR>\n"
		"\t\t<TD ALIGN=\"right\" BORDER=\"0\">left</TD>\n",
		(const void *)tree, tree->bsize + 2, treebit, orcify(tree));
	for(b = 0; b < tree->bsize; b++) branch = tree->branch + b,
		fprintf(fp, "\t\t<TD BGCOLOR=\"Gray90\">%u</TD>\n", branch->left);
	fprintf(fp, "\t</TR>\n"
		"\t<TR>\n"
		"\t\t<TD ALIGN=\"right\" BORDER=\"0\">skip</TD>\n");
	for(b = 0; b < tree->bsize; b++) branch = tree->branch + b,
		fprintf(fp, "\t\t<TD>%u</TD>\n", branch->skip);
	fprintf(fp, "\t</TR>\n"
		"\t<TR>\n"
		"\t\t<TD ALIGN=\"right\" BORDER=\"0\">leaves</TD>\n");
	for(i = 0; i <= tree->bsize; i++) {
		if(trie_bmp_test(&tree->is_child, i))
			fprintf(fp, "\t\t<TD PORT=\"%u\" BGCOLOR=\"Gray90\">...</TD>\n", i);
		else
			fprintf(fp, "\t\t<TD BGCOLOR=\"Grey90\">%s<FONT COLOR=\"Grey\">"
			"⊔</FONT></TD>\n", PT_(to_key)(tree->leaf[i].data));
			/* Should really escape it . . . don't have weird characters. */
	}
	fprintf(fp, "\t</TR>\n"
		"</TABLE>>];\n");
	/* Draw the lines between trees. */
	for(i = 0; i <= tree->bsize; i++) if(trie_bmp_test(&tree->is_child, i))
		fprintf(fp, "\ttree%pbranch0:%u -> tree%pbranch0 "
		"[style = dotted%s];\n", (const void *)tree, i,
		(const void *)tree->leaf[i].child,
		PT_(is_right)(tree, i) ? ", arrowhead = vee" : "");
	/* Recurse. */
	for(i = 0; i <= tree->bsize; i++) if(trie_bmp_test(&tree->is_child, i)) {
		struct { unsigned br0, br1, lf; } in_tree;
		size_t bit = treebit;
		in_tree.br0 = 0, in_tree.br1 = tree->bsize, in_tree.lf = 0;
		while(in_tree.br0 < in_tree.br1) {
			branch = tree->branch + in_tree.br0;
			bit += branch->skip;
			if(i <= in_tree.lf + branch->left)
				in_tree.br1 = ++in_tree.br0 + branch->left;
			else
				in_tree.br0 += branch->left + 1, in_tree.lf += branch->left + 1;
			bit++;
		}
		PT_(graph_tree_mem)(tree->leaf[i].child, bit, fp);
	}
}

/** Graphs `any` on `fp`. */
static void PT_(graph_tree_logic)(const struct PT_(tree) *const tree,
	const size_t treebit, FILE *const fp) {
	const struct trie_branch *branch;
	unsigned left, right, b, i;
	(void)treebit;
	assert(tree && fp);
	fprintf(fp, "\t// tree %p\n", (const void *)tree);
	if(tree->bsize) {
		fprintf(fp, "\t// branches\n");
		for(b = 0; b < tree->bsize; b++) { /* Branches. */
			branch = tree->branch + b;
			left = branch->left, right = PT_(right)(tree, b);
			fprintf(fp, "\ttree%pbranch%u [label = \"%u\", shape = circle, "
				"style = filled, fillcolor = Grey95];\n"
				"\ttree%pbranch%u -> ", (const void *)tree, b, branch->skip,
				(const void *)tree, b);
			if(left) {
				fprintf(fp, "tree%pbranch%u;\n",
					(const void *)tree, b + 1);
			} else {
				unsigned leaf = PT_(left_leaf)(tree, b);
				if(trie_bmp_test(&tree->is_child, leaf)) fprintf(fp,
					"tree%pbranch0 [style = dotted];\n",
					(const void *)tree->leaf[leaf].child);
				else fprintf(fp,
					"tree%pleaf%u [color = Gray];\n",
					(const void *)tree, leaf);
			}
			fprintf(fp, "\ttree%pbranch%u -> ", (const void *)tree, b);
			if(right) {
				fprintf(fp, "tree%pbranch%u [arrowhead = vee];\n",
					(const void *)tree, b + left + 1);
			} else {
				unsigned leaf = PT_(left_leaf)(tree, b) + left + 1;
				if(trie_bmp_test(&tree->is_child, leaf)) fprintf(fp,
					"tree%pbranch0 [style = dotted, arrowhead = vee];\n",
					(const void *)tree->leaf[leaf].child);
				else fprintf(fp,
					"tree%pleaf%u [color = Gray, arrowhead = vee];\n",
					(const void *)tree, leaf);
			}
		}
	}

	fprintf(fp, "\t// leaves\n");
	if(tree->bsize) {
		/* \sqcup ⊔ was good, but it didn't leave much space. */
		for(i = 0; i <= tree->bsize; i++) if(!trie_bmp_test(&tree->is_child, i))
			fprintf(fp, "\ttree%pleaf%u [label = <%s<FONT COLOR=\"Grey\">⊔</FONT>>];\n",
			(const void *)tree, i, PT_(to_key)(tree->leaf[i].data));
	} else {
		/* Lazy hack: just call this a branch, even though it's a leaf, so that
		 others may reference it. */
		if(trie_bmp_test(&tree->is_child, 0)) {
			fprintf(fp, "\ttree%pbranch0 [label = \"\", shape = circle];\n"
				"\ttree%pbranch0 -> tree%pbranch0"
				" [style = dashed];\n",
				(const void *)tree, (const void *)tree,
				(const void *)tree->leaf[0].child);
		} else {
			fprintf(fp, "\ttree%pbranch0 [label = <%s<FONT COLOR=\"Grey\">⊔</FONT>>];\n",
				(const void *)tree, PT_(to_key)(tree->leaf[0].data));
		}
	}
	fprintf(fp, "\n");

	/* Recurse. */
	for(i = 0; i <= tree->bsize; i++) if(trie_bmp_test(&tree->is_child, i))
		PT_(graph_tree_logic)(tree->leaf[i].child, 0, fp);
}

/** Draw a graph of `trie` to `fn` in Graphviz format with `tf` as it's
 tree-drawing output. */
static void PT_(graph_choose)(const struct T_(trie) *const trie,
	const char *const fn, const PT_(tree_file_fn) tf) {
	FILE *fp;
	assert(trie && fn);
	if(!(fp = fopen(fn, "w"))) { perror(fn); return; }
	fprintf(fp, "digraph {\n"
		"\tnode [shape = none];\n"
		/*"\tforcelabels = true;\n"*/"\n");
	if(!trie->root) fprintf(fp, "\tidle;");
	else tf(trie->root, 0, fp);
	fprintf(fp, "\tnode [color = Red];\n"
		"}\n");
	fclose(fp);
}

/** Graphs logical `trie` output to `fn`. */
static void PT_(graph)(const struct T_(trie) *const trie,
	const char *const fn) {
	const char logic[] = "-logic", mem[] = "-mem", bits[] = "-bits";
	char temp[128], *dash, *dot;
	size_t fn_len = strlen(fn), i, i_fn, i_temp;
	/* Whatever we're going to add to the string. */
	if(fn_len > sizeof temp - 30 - 1
		|| !(dash = strchr(fn, '-')) || !(dot = strchr(dash, '.'))) {
		fprintf(stderr, "Too long or doesn't '-' and then '.': <%s>.\n", fn);
		assert(0);
		return;
	}
	printf("graph.%u: base %s.\n", PT_(no), fn);
	i = (size_t)(dash - fn), memcpy(temp, fn, i_temp = i_fn = i);
	temp[i_temp++] = '-';
	sprintf(temp + i_temp, "%u", PT_(no)), i_temp += strlen(temp + i_temp);
	i = (size_t)(dot - fn) - i_fn, memcpy(temp + i_temp, fn + i_fn, i),
		i_temp += i, i_fn += i;

	memcpy(temp + i_temp, logic, sizeof logic - 1);
	memcpy(temp + i_temp + sizeof logic - 1, fn + i_fn, fn_len - i_fn + 1);
	PT_(graph_choose)(trie, temp, &PT_(graph_tree_logic));
	memcpy(temp + i_temp, mem, sizeof mem - 1);
	memcpy(temp + i_temp + sizeof mem - 1, fn + i_fn, fn_len - i_fn + 1);
	PT_(graph_choose)(trie, temp, &PT_(graph_tree_mem));
	memcpy(temp + i_temp, bits, sizeof bits - 1);
	memcpy(temp + i_temp + sizeof bits - 1, fn + i_fn, fn_len - i_fn + 1);
	PT_(graph_choose)(trie, temp, &PT_(graph_tree_bits));
}

static void PT_(print)(const struct PT_(tree) *const tree) {
	const struct trie_branch *branch;
	unsigned b, i;
	assert(tree);
	printf("%s:\n"
		"left ", orcify(tree));
	for(b = 0; b < tree->bsize; b++) branch = tree->branch + b,
		printf("%s%u", b ? ", " : "", branch->left);
	printf("\n"
		"skip ");
	for(b = 0; b < tree->bsize; b++) branch = tree->branch + b,
		printf("%s%u", b ? ", " : "", branch->skip);
	printf("\n"
		"leaves ");
	for(i = 0; i <= tree->bsize; i++) {
		if(i) printf(", ");
		printf("%s", trie_bmp_test(&tree->is_child, i)
			? orcify(tree->leaf[i].child) : PT_(to_key)(tree->leaf[i].data));
	}
	printf("\n");
}

/** Make sure `any` is in a valid state, (and all the children.) */
static void PT_(valid_tree)(const struct PT_(tree) *const tree) {
	unsigned i;
	int cmp = 0;
	const char *str1 = 0;
	assert(tree && tree->bsize <= TRIE_BRANCHES);
	for(i = 0; i < tree->bsize; i++)
		assert(tree->branch[i].left < tree->bsize - 1 - i);
	for(i = 0; i <= tree->bsize; i++) {
		if(trie_bmp_test(&tree->is_child, i)) {
			PT_(valid_tree)(tree->leaf[i].child);
		} else {
			const char *str2;
			assert(tree->leaf[i].data);
			str2 = PT_(to_key)(tree->leaf[i].data);
			if(str1) cmp = strcmp(str1, str2), assert(cmp < 0);
			str1 = str2;
		}
	}
}

/** Makes sure the `trie` is in a valid state. */
static void PT_(valid)(const struct T_(trie) *const trie) {
	if(!trie || !trie->root) return;
	PT_(valid_tree)(trie->root);
}

/** Ignores `a` and `b`. @return False. */
static int PT_(false)(PT_(type) *const a, PT_(type) *const b)
	{ (void)a, (void)b; return 0; }

/** Ignores `a` and `b`. @return True. */
static int PT_(true)(PT_(type) *const a, PT_(type) *const b)
	{ (void)a, (void)b; return 1; }

static void PT_(test)(void) {
	struct T_(trie) trie = TRIE_IDLE;
	struct T_(trie_iterator) it;
	size_t n, m, count, sum;
	struct { PT_(type) data; int is_in; } es[20/*00*/];
	PT_(type) dup;
	const size_t es_size = sizeof es / sizeof *es;
	PT_(type) *data;
	int ret;

	/* Idle. */
	PT_(valid)(0);
	PT_(valid)(&trie);
	T_(trie)(&trie), PT_(valid)(&trie);
	PT_(graph)(&trie, "graph/" QUOTE(TRIE_NAME) "-idle.gv");
	T_(trie_)(&trie), PT_(valid)(&trie);
	data = T_(trie_match)(&trie, ""), assert(!data);
	data = T_(trie_get)(&trie, ""), assert(!data);

	/* Make random data. */
	for(n = 0; n < es_size; n++) PT_(filler)(&es[n].data);

	/* Adding. */
	count = 0;
	errno = 0;
	for(n = 0, PT_(no) = 1; n < es_size; n++) {
		printf("Adding %s.\n", PT_(to_key)(&es[n].data));
		es[n].is_in = T_(trie_add)(&trie, &es[n].data);
		if(!((n + 1) & n) || n + 1 == es_size)
			PT_(graph)(&trie, "graph/" QUOTE(TRIE_NAME) "-sample.gv");
		assert(!errno);
		if(!es[n].is_in) { printf("Duplicate value.\n"); continue; };
		count++;
		for(m = 0; m <= n; m++) {
			if(!es[m].is_in) continue;
			data = T_(trie_get)(&trie, PT_(to_key)(&es[m].data));
			/* This is O(n^2) spam.
			printf("test get(%s) = %s\n", PT_(to_key)(&es[m].data),
				data ? PT_(to_key)(data) : "<didn't find>");*/
			assert(data == &es[m].data);
		}
		PT_(no)++;
	}
	for(n = 0; n < es_size; n++)
		if(es[n].is_in) data = T_(trie_get)(&trie, PT_(to_key)(&es[n].data)),
			assert(data == &es[n].data);
		else printf("es %lu duplicate\n", n);
	printf("Now trie is %s.\n", T_(trie_to_string)(&trie));

	/* Test prefix and size. */
	sum = !!T_(trie_get)(&trie, "");
	for(n = 1; n < 256; n++) {
		char a[2] = { '\0', '\0' };
		a[0] = (char)n;
		T_(trie_prefix)(&trie, a, &it);
		sum += T_(trie_size)(&it);
	}
	T_(trie_prefix)(&trie, "", &it);
	n = T_(trie_size)(&it);
	printf("%lu items inserted; %lu items counted; sum of sub-trees %lu.\n",
		count, n, sum), assert(n == count && n == sum);

	/* Replacement. */
	ret = T_(trie_add)(&trie, &es[0].data); /* Doesn't add. */
	assert(!ret);
	ret = T_(trie_put)(&trie, &es[0].data, 0); /* Replaces with itself. */
	assert(ret);
	ret = T_(trie_put)(&trie, &es[0].data, &data); /* Replaces with itself. */
	assert(ret && data == &es[0].data);
	ret = T_(trie_policy_put)(&trie, &es[0].data, 0, 0); /* Does add. */
	assert(ret);
	ret = T_(trie_policy_put)(&trie, &es[0].data, &data, 0); /* Does add. */
	assert(ret && data == &es[0].data);
	memcpy(&dup, &es[0].data, sizeof dup);
	ret = T_(trie_policy_put)(&trie, &dup, &data, &PT_(false)); /* Not add. */
	assert(ret && data == &dup);
	ret = T_(trie_policy_put)(&trie, &dup, &data, &PT_(true)); /* Add. */
	assert(ret && data == &es[0].data), es[0].is_in = 0;
	T_(trie_prefix)(&trie, "", &it);
	m = T_(trie_size)(&it);
	printf("Trie size: %lu before, replacement %lu.\n",
		(unsigned long)n, (unsigned long)m);
	assert(n == m);
	/* Restore the original. */
	ret = T_(trie_put)(&trie, &es[0].data, 0); /* Add. */
	assert(ret && data == &es[0].data), es[0].is_in = 1;

#if 0 /* Remove is buggy. */
	printf("es_size %lu\n", es_size);
	for(n = 3; n < es_size; n++) {
		const char *key;
		if(!es[n].is_in) { printf("es %lu is not in\n", n); continue; }
		key = PT_(to_key)(&es[n].data);
		PT_(no) = (unsigned)n;
		printf("%u) Removing \"%s\" from trie.\n", PT_(no), key);
		data = T_(trie_remove)(&trie, key);
		PT_(graph)(&trie, "graph/" QUOTE(TRIE_NAME) "-remove.gv");
		assert(data == &es[n].data);
		es[n].is_in = 0;
		data = T_(trie_get)(&trie, key);
		assert(!data);
		break;
	}
#endif

	T_(trie_)(&trie), assert(!trie.root), PT_(valid)(&trie);
	assert(!errno);
}

/** Will be tested on stdout. Requires `TRIE_TEST`, and not `NDEBUG` while
 defining `assert`. @allow */
static void T_(trie_test)(void) {
	printf("<" QUOTE(TRIE_NAME) ">trie"
		" of type <" QUOTE(TRIE_TYPE) ">"
		" was created using: TREE_KEY<" QUOTE(TRIE_KEY) ">;"
		" TRIE_TEST <" QUOTE(TRIE_TEST) ">;"
		" testing:\n");
	PT_(test)();
	fprintf(stderr, "Done tests of <" QUOTE(TRIE_NAME) ">trie.\n\n");
}

#undef QUOTE
#undef QUOTE_
