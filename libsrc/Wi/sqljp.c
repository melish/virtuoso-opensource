/*
 *  sqljp.c
 *
 *  $Id$
 *
 *  Join plan - Reduce SQL opt search space
 *
 *  This file is part of the OpenLink Software Virtuoso Open-Source (VOS)
 *  project.
 *
 *  Copyright (C) 1998-2011 OpenLink Software
 *
 *  This project is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; only version 2 of the License, dated June 1991.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#include "sqlnode.h"
#include "eqlcomp.h"
#include "sqlfn.h"
#include "sqlpar.h"
#include "sqlpfn.h"
#include "sqlcmps.h"
#include "sqlintrp.h"
#include "sqlo.h"
#include "rdfinf.h"
#include "strlike.h"


df_elt_t *
dfe_left_col (df_elt_t * tb_dfe, df_elt_t * pred)
{
  if (DFE_BOP != pred->dfe_type && DFE_BOP_PRED != pred->dfe_type)
    return NULL;
  if (DFE_COLUMN == pred->_.bin.left->dfe_type && tb_dfe->_.table.ot == (op_table_t *) pred->_.bin.left->dfe_tables->data)
    return pred->_.bin.left;
  if (DFE_COLUMN == pred->_.bin.right->dfe_type && tb_dfe->_.table.ot == (op_table_t *) pred->_.bin.right->dfe_tables->data)
    return pred->_.bin.right;
  return NULL;
}

df_elt_t *
dfe_right (df_elt_t * tb_dfe, df_elt_t * pred)
{
  if (DFE_BOP != pred->dfe_type && DFE_BOP_PRED != pred->dfe_type)
    return NULL;
  if (!dk_set_member (pred->_.bin.left->dfe_tables, (void *) tb_dfe->_.table.ot))
    return pred->_.bin.left;
  if (!dk_set_member (pred->_.bin.right->dfe_tables, (void *) tb_dfe->_.table.ot))
    return pred->_.bin.right;
  return NULL;
}


int
dfe_is_quad (df_elt_t * tb_dfe)
{
  return (DFE_TABLE == tb_dfe->dfe_type && tb_is_rdf_quad (tb_dfe->_.table.ot->ot_table));
}


caddr_t dv_iri_short_name (caddr_t x);

char *
dfe_p_const_abbrev (df_elt_t * tb_dfe)
{
  static char tmp[20];
  caddr_t name;
  int len;
  DO_SET (df_elt_t *, pred, &tb_dfe->_.table.col_preds)
  {
      if (PRED_IS_EQ (pred) && pred->_.bin.left->dfe_type == DFE_COLUMN && 'P' == toupper (pred->_.bin.left->_.col.col->col_name[0]))
      {
	  df_elt_t * right = pred->_.bin.right;
	  if (DFE_CONST == right->dfe_type && DV_IRI_ID == DV_TYPE_OF (right->dfe_tree))
	  {
	      caddr_t box = dv_iri_short_name (right->dfe_tree);
	      strncpy (tmp, box ? box : "unnamed", sizeof (tmp));
	    tmp[sizeof (tmp) - 1] = 0;
	      dk_free_box (box);
	    }
	  else if ((name  = sqlo_iri_constant_name (pred->_.bin.right->dfe_tree)))
	    {
	      caddr_t pref, local;
	      if (iri_split (name, &pref, &local))
		{
		  dk_free_box (pref);
		  len = box_length (local) - 5;
		  strncpy (tmp, local + 4 + (len > sizeof (tmp) ? len - sizeof (tmp) : 0), sizeof (tmp));
		  tmp[sizeof (tmp) - 1] = 0;
		  dk_free_box (local);
		  return tmp;
		}
	  }
	  else
	    tmp[0] = 0;
	  return tmp;
      }
  }
  END_DO_SET ();
  return "";
}


float *
dfe_p_stat (df_elt_t * tb_dfe, iri_id_t pid, dk_set_t * parts_ret)
{
#if 0
  caddr_t ctx_name = sqlo_opt_value (tb_dfe->_.table.ot->ot_opts, OPT_RDF_INFERENCE);
  rdf_inf_ctx_t **place = ctx_name ? (rdf_inf_ctx_t **) id_hash_get (rdf_name_to_ric, (caddr_t) & ctx_name) : NULL;
#endif
  dbe_key_t *pk = tb_dfe->_.table.ot->ot_table->tb_primary_key;
  float *p_stat;
  if (!pk->key_p_stat)
    return NULL;
  p_stat = (float *) id_hash_get (pk->key_p_stat, (caddr_t) & pid);
  *parts_ret = pk->key_parts;
  return p_stat;
}

#define iri_id_check(x) (DV_IRI_ID == DV_TYPE_OF (x) ? unbox_iri_id (x) : 0)
dbe_key_t *
rdf_po_key (df_elt_t * tb_dfe)
{
  DO_SET (dbe_key_t *, key, &tb_dfe->_.table.ot->ot_table->tb_keys)
  {
    if (!key->key_no_pk_ref && key->key_parts->next && key->key_parts->next->next
	&& toupper (((dbe_column_t *) key->key_parts->data)->col_name[0]) == 'P'
	&& toupper (((dbe_column_t *) key->key_parts->next->data)->col_name[0]) == 'O')
      return key;
  }
  END_DO_SET ();
  return NULL;
}


float
sqlo_rdfs_type_card (df_elt_t * tb_dfe, df_elt_t * p_dfe, df_elt_t * o_dfe)
{
  df_elt_t *lower[2];
  df_elt_t *upper[2];
  index_choice_t ic;
  memzero (&ic, sizeof (ic));
  lower[0] = p_dfe;
  lower[1] = o_dfe;
  upper[0] = upper[1] = NULL;
  ic.ic_key = rdf_po_key (tb_dfe);
  if (!ic.ic_key)
    return dbe_key_count (tb_dfe->_.table.ot->ot_table->tb_primary_key);
  ic.ic_ric = rdf_name_to_ctx (sqlo_opt_value (tb_dfe->_.table.ot->ot_opts, OPT_RDF_INFERENCE));
  return sqlo_inx_sample (tb_dfe, ic.ic_key, lower, upper, 2, &ic);
}


int
dfe_is_iri_id_test (df_elt_t * pred)
{
  if (DFE_BOP != pred->dfe_type || BOP_NOT != pred->_.bin.op)
    return 0;
  pred = pred->_.bin.left;
  if (DFE_BOP_PRED != pred->dfe_type || BOP_EQ != pred->_.bin.op
      || 0 != unbox ((ccaddr_t) pred->_.bin.left->dfe_tree) || !st_is_call (pred->_.bin.right->dfe_tree, "isiri_id", 1))
    return 0;
  return 1;
}


extern caddr_t rdfs_type;

float
jp_fanout (join_plan_t * jp)
{
  /* for sql this is the table card over the col pred cards , for rdf this is based on p stat */
  int jinx;
  if (dfe_is_quad (jp->jp_tb_dfe))
    {
      dk_set_t parts = NULL;
      int nth_col = 0;
      float *p_stat;
      float s_card, o_card, g_card, misc_card = 1;
      iri_id_t p = 0, s = 0, g = 0;
      caddr_t o = NULL;
      df_elt_t *is_p = NULL, *is_s = NULL, *is_o = NULL, *is_g = NULL;
      for (jinx = 0; jinx < jp->jp_n_preds; jinx++)
	{
	  pred_score_t *ps = &jp->jp_preds[jinx];
	  if (!ps->ps_is_placeable)
	    continue;
	  if (!ps->ps_left_col)
	    {
	      if (dfe_is_iri_id_test (ps->ps_pred))
		continue;
	      misc_card *= 0.3;
	      continue;
	    }
	  if (!PRED_IS_EQ (ps->ps_pred))
	    {
	      misc_card *= 0.5;
	      continue;
	    }
	  switch (ps->ps_left_col->col_name[0])
	    {
	    case 'P':
	    case 'p':
	      is_p = ps->ps_pred;
	      p = iri_id_check (ps->ps_const);
	      break;
	    case 'S':
	    case 's':
	      is_s = ps->ps_pred;
	      s = iri_id_check (ps->ps_const);
	      break;
	    case 'O':
	    case 'o':
	      is_o = ps->ps_pred;
	      o = ps->ps_const;
	      break;
	    case 'G':
	    case 'g':
	      is_g = ps->ps_pred;
	      g = iri_id_check (ps->ps_const);
	      break;
	    }
	}
      if (!is_p || !p)
	goto general;
      if (unbox_iri_id (rdfs_type) == p)
	{
	  if (is_s && is_o)
	    return jp->jp_fanout = 0.9;
	  if (is_o && o)
	    return jp->jp_fanout = sqlo_rdfs_type_card (jp->jp_tb_dfe, is_p, is_o);
	}
      p_stat = dfe_p_stat (jp->jp_tb_dfe, p, &parts);
      if (!p_stat)
	goto general;

      DO_SET (dbe_column_t *, part, &parts)
      {
	switch (part->col_name[0])
	  {
	  case 'S':
	  case 's':
	    s_card = p_stat[nth_col];
	    break;
	  case 'O':
	  case 'o':
	    o_card = p_stat[nth_col];
	    break;
	  case 'G':
	  case 'g':
	    g_card = p_stat[nth_col];
	    break;
	  }
	nth_col++;
      }
      END_DO_SET ();
      if (is_s && !is_o)
	return jp->jp_fanout = arity_scale ((p_stat[0] / s_card) * misc_card);
      if (!is_s && is_o)
	return jp->jp_fanout = arity_scale ((p_stat[0] / o_card) * misc_card);
      if (is_s && is_o)
	return arity_scale ((p_stat[0] / s_card / o_card) * misc_card);
      return jp->jp_fanout = p_stat[0];
    }
  else if (DFE_TABLE == jp->jp_tb_dfe->dfe_type)
    {
      float card;
    general:
      card = dbe_key_count (jp->jp_tb_dfe->_.table.ot->ot_table->tb_primary_key);
      for (jinx = 0; jinx < jp->jp_n_preds; jinx++)
	{
	  pred_score_t *ps = &jp->jp_preds[jinx];
	  if (ps->ps_is_placeable)
	    card *= ps->ps_card;
	}
      jp->jp_fanout = card;
    }
  else
    jp->jp_fanout = 0.9;
  return jp->jp_fanout;
}


int
dfe_const_value (df_elt_t * dfe, caddr_t * data_ret)
{
  caddr_t name;
  if (DFE_CONST == dfe->dfe_type)
    {
      *data_ret = (caddr_t) dfe->dfe_tree;
      return 1;
    }
  if ((name = sqlo_iri_constant_name (dfe->dfe_tree)))
    {
      caddr_t id = key_name_to_iri_id (NULL, name, 0);
      *data_ret = id;
      if (!*data_ret)
	return 0;
      *data_ret = t_full_box_copy_tree (id);
      dk_free_tree (id);
      return 1;
    }
  if ((name = sqlo_rdf_obj_const_value (dfe->dfe_tree, NULL, NULL)))
    {
      *data_ret = name;
      return 1;
    }
  return 0;
}


#define JPF_TRY 4
#define JPF_HASH 8
#define JPF_NO_PLACED_JOINS 16


void
jp_add (join_plan_t * jp, df_elt_t * tb_dfe, df_elt_t * pred, int is_join)
{
  int n_preds = jp->jp_n_preds;
  caddr_t data;
  pred_score_t *ps;
  df_elt_t *right, *left_col;
  if (n_preds + 1 >= JP_MAX_PREDS)
    return;
  memzero (&jp->jp_preds[n_preds], sizeof (pred_score_t));
  jp->jp_n_preds = n_preds + 1;
  ps = &jp->jp_preds[n_preds];
  ps->ps_pred = pred;
  left_col = dfe_left_col (tb_dfe, pred);
  if (left_col)
    {
      ps->ps_left_col = left_col->_.col.col;
    }
  right = dfe_right (tb_dfe, pred);
  if (!is_join)
    ps->ps_is_placeable = 1;
  else
    {
      ps->ps_is_placeable = dfe_reqd_placed (pred);
      if ((JPF_NO_PLACED_JOINS & is_join) && ps->ps_is_placeable)
	{
	  jp->jp_n_preds--;
	  return;
	}
      if (jp->jp_hash_fill_dfes)
	{
	  DO_SET (op_table_t *, pred_dep, &pred->dfe_tables)
	  {
	    if (pred_dep->ot_dfe != tb_dfe && pred_dep->ot_dfe->dfe_is_placed
		&& !dk_set_member (jp->jp_hash_fill_dfes, (void *) pred_dep->ot_dfe))
	      jp->jp_not_for_hash_fill = 1;
	  }
	  END_DO_SET ();
	}
    }

  if (!right || !ps->ps_left_col || !PRED_IS_EQ (pred))
    {
      ps->ps_card = DFE_TEXT_PRED == pred->dfe_type ? 0.01 : 0.3;
      return;
    }
  ps->ps_right = right;
  if (DFE_COLUMN == right->dfe_type && PRED_IS_EQ (pred))
    {
      df_elt_t *right_tb = ((op_table_t *) right->dfe_tables->data)->ot_dfe;
      if (!right_tb->dfe_is_placed)
	{
	  int jinx;
	  for (jinx = 0; jinx < jp->jp_n_joined; jinx++)
	    if (right_tb == jp->jp_joined[jinx])
	      goto already_in;
	  jp->jp_joined[jp->jp_n_joined++] = right_tb;
	already_in:;
	}
      else
	{
	  if (-1 != ps->ps_left_col->col_n_distinct)
	    ps->ps_card = 1.0 / ps->ps_left_col->col_n_distinct;
	  else
	    ps->ps_card = 0.5;
	}
    }
  else if (dfe_const_value (right, &data))
    {
      ps->ps_const = data;
      ps->ps_is_const = 1;
      if (ps->ps_left_col && -1 != ps->ps_left_col->col_n_distinct)
	ps->ps_card = 1.0 / ps->ps_left_col->col_n_distinct;
    }
  if (!ps->ps_card)
    ps->ps_card = 0, 5;
}


int
dfe_in_hash_set (df_elt_t * tb_dfe, int hash_set)
{
  if (!hash_set)
    return 1;
  return unbox (sqlo_opt_value (tb_dfe->_.table.ot->ot_opts, OPT_HASH_SET)) == hash_set;
}

void
dfe_jp_fill (sqlo_t * so, op_table_t * ot, df_elt_t * tb_dfe, join_plan_t * jp, int mode, int hash_set)
{
  jp->jp_n_joined = jp->jp_n_preds = 0;
  jp->jp_tb_dfe = tb_dfe;
  DO_SET (df_elt_t *, pred, &ot->ot_preds)
  {
    if (pred->dfe_tables && !pred->dfe_tables->next
	&& tb_dfe->_.table.ot == (op_table_t *) pred->dfe_tables->data && dfe_in_hash_set (tb_dfe, hash_set))
      {
	jp_add (jp, tb_dfe, pred, 0);
      }
    else if (dk_set_member (pred->dfe_tables, (void *) tb_dfe->_.table.ot) && dfe_in_hash_set (tb_dfe, hash_set))
      {
	jp_add (jp, tb_dfe, pred, 1 | mode);
	if (jp->jp_preds[jp->jp_n_preds - 1].ps_is_placeable)
	  {
	    if (!jp->jp_prev)
	      tb_dfe->dfe_is_joined = 1;
	  }
      }
  }
  END_DO_SET ();
  jp->jp_fanout = jp_fanout (jp);
  if (jp->jp_fanout < 0)
    bing ();
}


int
jp_mark_restr_join (join_plan_t * jp, join_plan_t * root_jp)
{
  join_plan_t *prev;
  float path_fanout = 1;
  for (prev = jp; prev; prev = prev->jp_prev)
    {
      path_fanout *= prev->jp_fanout;
    }
  if (path_fanout > 0.8)
    return 0;
  if (-1 == root_jp->jp_best_cost || jp->jp_cost < root_jp->jp_best_cost)
    {
      root_jp->jp_best_jp = NULL;
      for (prev = jp; prev; prev = prev->jp_prev)
	t_set_push (&root_jp->jp_best_jp, (void *) prev->jp_tb_dfe);
    }
  return 1;
}


extern int enable_joins_only;

float
dfe_join_score_jp (sqlo_t * so, op_table_t * ot, df_elt_t * tb_dfe, dk_set_t * res, join_plan_t * prev_jp)
{
  join_plan_t jp;
  float score = 0;
  int level = 0, any_tried = 0;
  join_plan_t *root_jp = NULL;
  float path_fanout = 1;
  jp.jp_hash_fill_dfes = NULL;
  jp.jp_best_jp = NULL;
  jp.jp_reached = 0;
  jp.jp_prev = prev_jp;
  for (prev_jp = prev_jp; prev_jp; prev_jp = prev_jp->jp_prev)
    {
      path_fanout *= prev_jp->jp_fanout;
      root_jp = prev_jp;
      level++;
    }
  tb_dfe->dfe_is_placed = 1;	/* to fool dfe_reqd_placed */
  dfe_jp_fill (so, ot, tb_dfe, &jp, JPF_TRY, 0);
  if (jp.jp_prev)
    jp.jp_cost = jp.jp_prev->jp_cost + path_fanout * jp.jp_fanout;
  else
    {
      jp.jp_cost = jp.jp_fanout;
      if (so->so_any_placed && enable_joins_only && !tb_dfe->dfe_is_joined)
	{
	  t_set_push (res, (void *) t_cons ((void *) tb_dfe, NULL));
	  tb_dfe->dfe_is_placed = 0;
	  return (1.0 / jp.jp_cost) / (1 + jp.jp_n_joined);
	}
    }
  jp.jp_best_card = jp.jp_best_cost = -1;
  if (jp.jp_fanout * path_fanout < 0.7)
    goto restricting;
  if (jp.jp_n_joined && (level < 2 || (jp.jp_fanout < 1.1 && level < 4)))
    {
      int jinx;
      if (level > 0)
	root_jp->jp_reached += (float) jp.jp_n_joined / level;
      for (jinx = 0; jinx < jp.jp_n_joined; jinx++)
	{
	  dfe_join_score_jp (so, ot, jp.jp_joined[jinx], res, &jp);
	  any_tried = 1;
	}
    }
restricting:
  if (!any_tried && level > 0)
    {
      dk_set_t prev_best = jp.jp_best_jp;
      int is_restr = jp_mark_restr_join (&jp, root_jp);
      if (-1 == root_jp->jp_best_card || path_fanout * jp.jp_fanout < root_jp->jp_best_card)
	root_jp->jp_best_card = path_fanout * jp.jp_fanout;
      if (prev_best && is_restr)
	root_jp->jp_best_cost = 0.5 * MIN (jp.jp_cost, root_jp->jp_best_cost);
      else if (-1 == root_jp->jp_best_cost || jp.jp_cost < root_jp->jp_best_cost)
	root_jp->jp_best_cost = jp.jp_cost;
    }
  tb_dfe->dfe_is_placed = 0;
  if (!jp.jp_prev)
    {
      jp.jp_tb_dfe->dfe_arity = jp.jp_best_card;
      if (jp.jp_best_jp)
	t_set_push (res, (void *) jp.jp_best_jp);
      else
	t_set_push (res, (void *) t_cons (tb_dfe, NULL));
      score = 1.0 / (jp.jp_best_cost != -1 ? jp.jp_best_cost : jp.jp_fanout);
      return score;
    }
  return 0;
}


void
jp_print (join_plan_t * jp)
{
  int inx;
  if (jp->jp_prev)
    {
      jp_print (jp->jp_prev);
      printf ("---\n");
    }
  for (inx = 0; inx < jp->jp_n_preds; inx++)
    {
      pred_score_t *ps = &jp->jp_preds[inx];
      if (!ps->ps_is_placeable)
	continue;
      sqlo_box_print ((caddr_t) ps->ps_pred->dfe_tree);
    }
}




/* hash join with a join on the build side */


void
dfe_hash_fill_score (sqlo_t * so, op_table_t * ot, df_elt_t * tb_dfe, join_plan_t * prev_jp, int hash_set)
{
  /* if finds a restricting join path, adds the dfes on the path to the hash filler dfes */
  join_plan_t jp;
  int level = 0, pinx;
  join_plan_t *root_jp = NULL;
  float path_fanout = 1;
  if (DFE_TABLE != tb_dfe->dfe_type || tb_dfe->_.table.ot->ot_is_outer)
    return;
  jp.jp_prev = prev_jp;
  for (prev_jp = prev_jp; prev_jp; prev_jp = prev_jp->jp_prev)
    {
      path_fanout *= prev_jp->jp_fanout;
      root_jp = prev_jp;
      level++;
    }
  jp.jp_hash_fill_dfes = jp.jp_prev->jp_hash_fill_dfes;
  tb_dfe->dfe_is_placed = 1;	/* to fool dfe_reqd_placed */
  jp.jp_not_for_hash_fill = 0;
  dfe_jp_fill (so, ot, tb_dfe, &jp, JPF_HASH, hash_set);
  if (jp.jp_fanout > 1.3 || jp.jp_not_for_hash_fill)
    {
      if (level > 1 && path_fanout / root_jp->jp_fanout < 0.7)
	{
	  join_plan_t *prev;
	  root_jp->jp_best_card *= path_fanout;
	  for (prev = jp.jp_prev; prev; prev = prev->jp_prev)
	    {
	      t_set_pushnew (&root_jp->jp_hash_fill_dfes, (void *) prev->jp_tb_dfe);
	      for (pinx = 0; pinx < prev->jp_n_preds; pinx++)
		if (prev->jp_preds[pinx].ps_is_placeable)
		  t_set_pushnew (&root_jp->jp_hash_fill_preds, (void *) prev->jp_preds[pinx].ps_pred);
	    }
	}
      tb_dfe->dfe_is_placed = 0;
      return;
    }
  if (jp.jp_n_joined && level < 4)
    {
      int jinx;
      s_node_t tn;
      tn.data = (void *) tb_dfe;
      tn.next = jp.jp_prev->jp_hash_fill_dfes;
      jp.jp_hash_fill_dfes = &tn;
      for (jinx = 0; jinx < jp.jp_n_joined; jinx++)
	dfe_hash_fill_score (so, ot, jp.jp_joined[jinx], &jp, hash_set);
      jp.jp_hash_fill_dfes = tn.next;
    }
  else if (root_jp)
    {
      if (path_fanout * jp.jp_fanout / root_jp->jp_fanout < 0.8)
	{
	  join_plan_t *prev = &jp;
	  root_jp->jp_best_card *= path_fanout * jp.jp_fanout / root_jp->jp_fanout;
	  for (prev = &jp; prev; prev = prev->jp_prev)
	    {
	      t_set_pushnew (&root_jp->jp_hash_fill_dfes, (void *) prev->jp_tb_dfe);
	      for (pinx = 0; pinx < prev->jp_n_preds; pinx++)
		if (prev->jp_preds[pinx].ps_is_placeable)
		  t_set_pushnew (&root_jp->jp_hash_fill_preds, (void *) prev->jp_preds[pinx].ps_pred);
	    }
	}
    }
  tb_dfe->dfe_is_placed = 0;
}


int enable_hash_fill_join = 1;
int enable_subq_cache = 1;


ST *
sqlo_pred_tree (df_elt_t * dfe)
{
  /* in existence pred dfes the dfe tree does not have the exists, so put it there, else the dfe tree pf a pred is good */
  ST *copy = (ST *) t_box_copy_tree ((caddr_t) dfe->dfe_tree);
  if (DFE_EXISTS == dfe->dfe_type)
    return (ST *) SUBQ_PRED (EXISTS_PRED, NULL, copy, NULL, NULL);
  return copy;
}


int
sqlo_hash_fill_join (sqlo_t * so, df_elt_t * hash_ref_tb, df_elt_t ** fill_ret, dk_set_t org_preds, dk_set_t hash_keys)
{
  /* find unplaced tables that join to hash_ef tb, restricting card and do not join to any other placed thing */
  int jinx, inx, ctr = 0;
  op_table_t *ot = so->so_this_dt;
  char sqk[SQK_MAX_CHARS];
  char *p_sqk = sqk;
  int hash_set = unbox (sqlo_opt_value (hash_ref_tb->_.table.ot->ot_opts, OPT_HASH_SET));
  int sqk_fill = 0;
  df_elt_t **sqc_place = NULL;
  df_elt_t *fill_copy;
  join_plan_t jp;
  df_elt_t *fill_dfe;
  if (!enable_hash_fill_join || -1 == hash_set)
    return 0;
  jp.jp_hash_fill_preds = org_preds;
  jp.jp_prev = NULL;
  jp.jp_hash_fill_dfes = NULL;
  dfe_jp_fill (so, ot, hash_ref_tb, &jp, JPF_TRY | JPF_NO_PLACED_JOINS, hash_set);
  jp.jp_best_card = 1;
  if (!jp.jp_n_joined)
    return 0;
  jp.jp_hash_fill_dfes = t_cons ((void *) hash_ref_tb, NULL);
  for (jinx = 0; jinx < jp.jp_n_joined; jinx++)
    dfe_hash_fill_score (so, ot, jp.jp_joined[jinx], &jp, hash_set);
  if (jp.jp_best_card > 0.9 && !hash_set)
    return 0;
  if (so->so_cache_subqs)
    {
      dfe_cc_list_key (hash_keys, sqk, &sqk_fill, sizeof (sqk) - 1);
      sprintf_more (sqk, sizeof (sqk), &sqk_fill, "| ");
      dfe_cc_list_key (jp.jp_hash_fill_dfes, sqk, &sqk_fill, sizeof (sqk) - 1);
      dfe_cc_list_key (jp.jp_hash_fill_preds, sqk, &sqk_fill, sizeof (sqk) - 1);
      so_ensure_subq_cache (so);
      if (sqk_fill < sizeof (sqk) - 10)
	sqc_place = (df_elt_t **) id_hash_get (so->so_subq_cache, (caddr_t) & p_sqk);
    }
  if (sqc_place)
    {
      fill_copy = sqlo_layout_copy (so, *sqc_place, hash_ref_tb);
    }
  else
    {
      ST **from = (ST **) t_list_to_array (jp.jp_hash_fill_dfes);
      ST *pred_tree = NULL;
      ST *texp = t_listst (9, TABLE_EXP, from,
	  NULL, NULL, NULL, NULL, NULL, NULL, NULL);
      ST *sel = t_listst (5, SELECT_STMT, NULL, t_list_to_array (hash_keys), NULL, texp);
      op_table_t *fill_ot;
      DO_SET (df_elt_t *, dfe, &jp.jp_hash_fill_preds) t_st_and (&pred_tree, sqlo_pred_tree (dfe));
      END_DO_SET ();
      texp->_.table_exp.where = pred_tree;
      DO_BOX (df_elt_t *, dfe, inx, sel->_.select_stmt.selection)
      {
	ST *as;
	char tmp[MAX_QUAL_NAME_LEN];
	if (DFE_COLUMN == dfe->dfe_type)
	  snprintf (tmp, sizeof (tmp), "%s.%s", dfe->dfe_tree->_.col_ref.prefix, dfe->dfe_tree->_.col_ref.name);
	else
	  snprintf (tmp, sizeof (tmp), "h%d", ctr++);
	as = t_listst (5, BOP_AS, t_box_copy_tree ((caddr_t) dfe->dfe_tree), NULL, t_box_string (tmp), NULL);
	sel->_.select_stmt.selection[inx] = (caddr_t) as;
      }
      END_DO_BOX;
      DO_BOX (df_elt_t *, tb_dfe, inx, texp->_.table_exp.from)
	  texp->_.table_exp.from[inx] =
	  t_listst (3, TABLE_REF, t_listst (6, TABLE_DOTTED, tb_dfe->_.table.ot->ot_table->tb_name,
	      tb_dfe->_.table.ot->ot_new_prefix, NULL, NULL, NULL), NULL);
      END_DO_BOX;
      sqlo_scope (so, &sel);
      fill_dfe = sqlo_df (so, sel);
      fill_dfe->dfe_super = hash_ref_tb;
      fill_ot = fill_dfe->_.sub.ot;
      fill_ot->ot_work_dfe = dfe_container (so, DFE_DT, hash_ref_tb);
      fill_ot->ot_work_dfe->_.sub.in_arity = 1;
      fill_copy = sqlo_layout (so, fill_ot, SQLO_LAY_VALUES, hash_ref_tb);
      fill_copy->_.sub.hash_filler_of = hash_ref_tb;
      fill_copy->_.sub.n_hash_fill_keys = dk_set_length (hash_keys);
      if (so->so_cache_subqs)
	{
	  caddr_t k = t_box_string (sqk);
	  df_elt_t *cp = sqlo_layout_copy (so, fill_copy, NULL);
	  t_id_hash_set (so->so_subq_cache, (caddr_t) & k, (caddr_t) & cp);
	}
    }
  hash_ref_tb->_.table.hash_filler = fill_copy;
  *fill_ret = fill_copy;
  DO_SET (df_elt_t *, tb_dfe, &jp.jp_hash_fill_dfes)
  {
    if (tb_dfe == hash_ref_tb)
      continue;
    t_set_push (&fill_copy->_.sub.dt_preds, (void *) tb_dfe);
    tb_dfe->dfe_is_placed = 1;
  }
  END_DO_SET ();
  DO_SET (df_elt_t *, dfe, &jp.jp_hash_fill_preds)
  {
    if (!dk_set_member (org_preds, (void *) dfe))
      t_set_pushnew (&fill_copy->_.sub.dt_preds, (void *) dfe);
    dfe->dfe_is_placed = 1;
  }
  END_DO_SET ();
  return 1;
}


void
dfe_unplace_fill_join (df_elt_t * fill_dt, df_elt_t * tb_dfe, dk_set_t org_preds)
{
  if (DFE_DT != fill_dt->dfe_type)
    return;
  DO_SET (df_elt_t *, elt, &fill_dt->_.sub.dt_preds)
  {
    if (dk_set_member (org_preds, (void *) elt))
      continue;
    elt->dfe_is_placed = 0;
  }
  END_DO_SET ();
  if (0 && tb_dfe)
    {
      DO_SET (df_elt_t *, elt, &tb_dfe->_.table.all_preds) elt->dfe_is_placed = 1;
      END_DO_SET ();
      DO_SET (df_elt_t *, elt, &tb_dfe->_.table.all_preds) elt->dfe_is_placed = 1;
      END_DO_SET ();
    }
}


/* Cache compilations of subqs, dts and hash fill dts */

void
dfe_cc_key (df_elt_t * dfe, char *str, int *fill, int space)
{
  if (!dfe)
    return;
  switch (dfe->dfe_type)
    {
    case DFE_TABLE:
    case DFE_DT:
    case DFE_EXISTS:
    case DFE_VALUE_SUBQ:
      sprintf_more (str, space, fill, "%s", dfe->_.table.ot->ot_new_prefix);
      break;
    case DFE_BOP:
    case DFE_BOP_PRED:
      dfe_cc_key (dfe->_.bin.left, str, fill, space);
      sprintf_more (str, space, fill, " %d", dfe->_.bin.op);
      dfe_cc_key (dfe->_.bin.right, str, fill, space);
      break;
    case DFE_COLUMN:
      sprintf_more (str, space, fill, "%s.%s", dfe->dfe_tree->_.col_ref.prefix, dfe->dfe_tree->_.col_ref.name);
      break;
    }
}


void
dfe_cc_list_key (dk_set_t list, char *str, int *fill, int space)
{
  DO_SET (df_elt_t *, dfe, &list) dfe_cc_key (dfe, str, fill, space);
  END_DO_SET ();
}


void
so_ensure_subq_cache (sqlo_t * so)
{
  if (!so->so_subq_cache)
    so->so_subq_cache = t_id_hash_allocate (101, sizeof (caddr_t), sizeof (caddr_t), strhash, strhashcmp);
}


df_elt_t *
sqlo_dt_cache_lookup (sqlo_t * so, op_table_t * ot, dk_set_t imp_preds, caddr_t * cc_key_ret)
{
  char sqk[SQK_MAX_CHARS];
  caddr_t p_sqk = sqk;
  df_elt_t **place;
  int sqk_fill;
  if (!enable_subq_cache || !so->so_cache_subqs)
    {
      *cc_key_ret = NULL;
      return NULL;
    }
  so_ensure_subq_cache (so);
  snprintf (sqk, sizeof (sqk), "D %s", ot->ot_new_prefix);
  sqk_fill = strlen (sqk);
  dfe_cc_list_key (imp_preds, sqk, &sqk_fill, sizeof (sqk) - 1);
  if (sqk_fill > SQK_MAX_CHARS - 5)
    {
      *cc_key_ret = NULL;
      return NULL;
    }
  place = (df_elt_t **) id_hash_get (so->so_subq_cache, (caddr_t) & p_sqk);
  if (!place)
    {
      *cc_key_ret = t_box_string (sqk);
      return NULL;
    }
  *cc_key_ret = NULL;
  return sqlo_layout_copy (so, *place, NULL);
}


void
sqlo_dt_cache_add (sqlo_t * so, caddr_t cc_key, df_elt_t * copy)
{
  copy = sqlo_layout_copy (so, copy, NULL);
  t_id_hash_set (so->so_subq_cache, (caddr_t) & cc_key, (caddr_t) & copy);
}