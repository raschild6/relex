/*************************************************************************/
/* Copyright (c) 2004                                                    */
/* Daniel Sleator, David Temperley, and John Lafferty                    */
/* All rights reserved                                                   */
/*                                                                       */
/* Use of the link grammar parsing system is subject to the terms of the */
/* license set forth in the LICENSE file included with this software.    */
/* This license allows free redistribution and use in source and binary  */
/* forms, with or without modification, subject to certain conditions.   */
/*                                                                       */
/*************************************************************************/

#include "api-structures.h"
#include "prepare/build-disjuncts.h"
#include "connectors.h"
#include "dict-common/dict-common.h"    // Dictionary_s
#include "disjunct-utils.h"
#include "externs.h"
#include "preparation.h"
#include "print/print.h"
#include "prune.h"
#include "resources.h"
#include "string-set.h"
#include "utilities.h"
#include "tokenize/word-structures.h"   // Word_struct
#include "tokenize/tok-structures.h"    // gword_set

#define D_PREP 5 // Debug level for this module.

/**
 * Set c->nearest_word to the nearest word that this connector could
 * possibly connect to.
 * The connector *might*, in the end, connect to something more distant,
 * but this is the nearest one that could be connected.
 * Also recalculate length_limit to be the farthest word number that could
 * be connected.
 */
static int set_dist_fields(Connector * c, size_t w, int delta, int w_clamp)
{
	int i;
	if (c == NULL) return (int) w;
	i = set_dist_fields(c->next, w, delta, w_clamp) + delta;
	c->nearest_word = i;
	int farthest_word = w + (delta * c->length_limit);
	/* Clamp it to the range [0, sent_length). */
	if (delta * farthest_word > w_clamp) farthest_word = w_clamp;
	c->farthest_word = farthest_word;
	return i;
}

/**
 * Initialize the word fields of the connectors,
 * eliminate those disjuncts that are so long, that they
 * would need to connect past the end of the sentence,
 * and mark the shallow connectors.
 */
static void setup_connectors(Sentence sent)
{
	for (WordIdx w = 0; w < sent->length; w++)
	{
		Disjunct *head = NULL;
		Disjunct *xd;

		for (Disjunct *d = sent->word[w].d; d != NULL; d = xd)
		{
			xd = d->next;
			if ((set_dist_fields(d->left, w, -1, 0) < 0) ||
			    (set_dist_fields(d->right, w, 1, (int)(sent->length-1)) >=
			     (int) sent->length))
			{
				; /* Skip this disjunct. */
			}
			else
			{
				d->next = head;
				head = d;
				if (NULL != d->left) d->left->shallow = true;
				if (NULL != d->right) d->right->shallow = true;

			}
		}
		sent->word[w].d = head;
	}
}

/**
 * Record the wordgraph word in each of its connectors.
 * It is used for checking alternatives consistency.
 */
void gword_record_in_connector(Sentence sent)
{
	for (Disjunct *d = sent->dc_memblock;
	     d < &((Disjunct *)sent->dc_memblock)[sent->num_disjuncts]; d++)
	{
		for (Connector *c = d->right; NULL != c; c = c->next)
			c->originating_gword = d->originating_gword;
		for (Connector *c = d->left; NULL != c; c = c->next)
			c->originating_gword = d->originating_gword;
	}
}

/**
 * Turn sentence expressions into disjuncts.
 * Sentence expressions must have been built, before calling this routine.
 */
static void build_sentence_disjuncts(Sentence sent, double cost_cutoff,
                                     Parse_Options opts)
{
	Disjunct * d;
	X_node * x;
	size_t w;

	sent->Disjunct_pool = pool_new(__func__, "Disjunct",
	                   /*num_elements*/2048, sizeof(Disjunct),
	                   /*zero_out*/false, /*align*/false, /*exact*/false);
	sent->Connector_pool = pool_new(__func__, "Connector",
	                   /*num_elements*/8192, sizeof(Connector),
	                   /*zero_out*/true, /*align*/false, /*exact*/false);

	for (w = 0; w < sent->length; w++)
	{
		d = NULL;
		for (x = sent->word[w].x; x != NULL; x = x->next)
		{
			Disjunct *dx = build_disjuncts_for_exp(sent, x->exp, x->string,
				&x->word->gword_set_head, cost_cutoff, opts);
			d = catenate_disjuncts(dx, d);
		}
		sent->word[w].d = d;
	}
}

/**
 * Assumes that the sentence expression lists have been generated.
 */
void prepare_to_parse(Sentence sent, Parse_Options opts)
{
	size_t i;

	build_sentence_disjuncts(sent, opts->disjunct_cost, opts);
	if (verbosity_level(D_PREP))
	{
		prt_error("Debug: After expanding expressions into disjuncts:\n\\");
		print_disjunct_counts(sent);
	}
	print_time(opts, "Built disjuncts");

	for (i=0; i<sent->length; i++)
	{
		sent->word[i].d = eliminate_duplicate_disjuncts(sent->word[i].d);

#if 0
		/* eliminate_duplicate_disjuncts() is now very efficient and doesn't
		 * take a significant time even for millions of disjuncts. If a very
		 * large number of disjuncts per word or very large number of words
		 * per sentence will ever be a problem, then a "checktimer" TLS
		 * counter can be used there. Old comment and code are retained
		 * below for documentation. */

		/* Some long Russian sentences can really blow up, here. */
		if (resources_exhausted(opts->resources))
			return;
#endif
	}
	print_time(opts, "Eliminated duplicate disjuncts");

	if (verbosity_level(D_PREP))
	{
		prt_error("Debug: After duplicate elimination:\n");
		print_disjunct_counts(sent);
	}

	setup_connectors(sent);

	if (verbosity_level(D_SPEC+2))
	{
		printf("prepare_to_parse:\n");
		print_all_disjuncts(sent);
	}
}
