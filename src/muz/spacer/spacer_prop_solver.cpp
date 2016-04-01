/*++
Copyright (c) 2011 Microsoft Corporation

Module Name:

    prop_solver.cpp

Abstract:

    SMT solver abstraction for SPACER.

Author:

    Krystof Hoder (t-khoder) 2011-8-17.

Revision History:

    Modified by Anvesh Komuravelli

--*/

#include <sstream>
#include "model.h"
#include "spacer_util.h"
#include "spacer_prop_solver.h"
#include "ast_smt2_pp.h"
#include "dl_util.h"
#include "model_pp.h"
#include "smt_params.h"
#include "datatype_decl_plugin.h"
#include "bv_decl_plugin.h"
#include "spacer_farkas_learner.h"
#include "ast_smt2_pp.h"
#include "expr_replacer.h"
#include "fixedpoint_params.hpp"

namespace spacer {

    prop_solver::prop_solver(manager& pm, fixedpoint_params const& p, symbol const& name) :
        m_fparams(pm.get_fparams()),
        m(pm.get_manager()),
        m_pm(pm),
        m_name(name),
        m_ctx(NULL),
        m_pos_level_atoms(m),
        m_neg_level_atoms(m),
        m_core(0),
        m_subset_based_core(false),
        m_uses_level(infty_level ()),
        m_delta_level(false),
        m_in_level(false)
    {
      
      m_solvers[0] = pm.mk_fresh ();
      m_solvers[1] = pm.mk_fresh2 ();
      
      m_contexts[0] = alloc(spacer::itp_solver, *(m_solvers[0]),
                            p.spacer_split_farkas_literals ());
      m_contexts[1] = alloc(spacer::itp_solver, *(m_solvers[1]),
                            p.spacer_split_farkas_literals ());
      
      for (unsigned i = 0; i < 2; ++i)
        m_contexts[i]->assert_expr (m_pm.get_background ());
    }

    void prop_solver::add_level() {
        unsigned idx = level_cnt();
        std::stringstream name;
        name << m_name << "#level_" << idx;
        func_decl * lev_pred = m.mk_fresh_func_decl(name.str().c_str(), 0, 0,m.mk_bool_sort());
        m_level_preds.push_back(lev_pred);

        app_ref pos_la(m.mk_const(lev_pred), m);
        app_ref neg_la(m.mk_not(pos_la.get()), m);

        m_pos_level_atoms.push_back(pos_la);
        m_neg_level_atoms.push_back(neg_la);

        m_level_atoms_set.insert(pos_la.get());
        m_level_atoms_set.insert(neg_la.get());
    }

    void prop_solver::ensure_level(unsigned lvl) {
        while (lvl>=level_cnt()) {
            add_level();
        }
    }

    unsigned prop_solver::level_cnt() const {
        return m_level_preds.size();
    }

    void prop_solver::assert_level_atoms(unsigned level) {
        unsigned lev_cnt = level_cnt();
        for (unsigned i=0; i<lev_cnt; i++) {
            bool active = m_delta_level ? i == level : i>=level;
            app * lev_atom =
              active ? m_neg_level_atoms.get (i) : m_pos_level_atoms.get (i);
            //m_ctx->assert_expr (lev_atom);
            m_ctx->push_bg (lev_atom);
        }
    }

    void prop_solver::assert_expr(expr * form) {
        SASSERT(!m_in_level);
        m_contexts[0]->assert_expr (form);
        m_contexts[1]->assert_expr (form);
        IF_VERBOSE(21, verbose_stream() << "$ asserted " << mk_pp(form, m) << "\n";);
        TRACE("spacer", tout << "add_formula: " << mk_pp(form, m) << "\n";);
    }

    void prop_solver::assert_expr(expr * form, unsigned level) {
        ensure_level(level);
        app * lev_atom = m_pos_level_atoms[level].get();
        app_ref lform(m.mk_or(form, lev_atom), m);
        assert_expr (lform);
    }


  /// Poor man's maxsat. No guarantees of maximum solution
  /// Runs maxsat loop on m_ctx Returns l_false if hard is unsat,
  /// otherwise reduces soft such that hard & soft is sat.
  lbool prop_solver::maxsmt (expr_ref_vector &hard, expr_ref_vector &soft)
  {
    unsigned hard_sz = hard.size ();
    hard.append (soft);
    
    // replace expressions by assumption literals
    itp_solver::scoped_mk_proxy _p_(*m_ctx, hard);
    
    lbool res = m_ctx->check_sat (hard.size (), hard.c_ptr ());
    if (res != l_false || soft.empty ()) return res;
    
    soft.reset ();
    
    expr_ref saved (m);
    ptr_vector<expr> core;
    m_ctx->get_unsat_core (core);
    
    while (hard.size () > hard_sz)
    {
      bool found = false;
      for (unsigned i = hard_sz, sz = hard.size (); i < sz; ++i)
        if (core.contains (hard.get (i)))
          {
            found = true;
            saved = hard.get (i);
            hard[i] = hard.back ();
            hard.pop_back ();
            break;
          }
      if (!found)
      {
        hard.resize (hard_sz);
        return l_false;
      }
      
      res = m_ctx->check_sat (hard.size (), hard.c_ptr ());
      if (res == l_true) break;
      if (res == l_undef)
      {
        hard.push_back (saved);
        break;
      }
      SASSERT (res == l_false);
      core.reset ();
      m_ctx->get_unsat_core (core);
    }

    if (res != l_false)
    {
      // update soft
      for (unsigned i = hard_sz, sz = hard.size (); i < sz; ++i)
        soft.push_back (hard.get (i));
    }
    hard.resize (hard_sz);
    return res;
  }
  
    lbool prop_solver::internal_check_assumptions(
        const expr_ref_vector& hard_atoms,
        expr_ref_vector& soft_atoms)
    {
        flet<bool> _model(m_fparams.m_model, m_model != 0);
        expr_ref_vector expr_atoms(m);

        expr_atoms.append (hard_atoms);
        if (m_in_level) assert_level_atoms(m_current_level);
        lbool result = maxsmt (expr_atoms, soft_atoms);
        if (result == l_true && m_model) m_ctx->get_model (*m_model);

        SASSERT (result != l_false || soft_atoms.empty ());

        /// compute level used in the core
        // XXX this is a poor approximation because the core will get minimized further
        if (result == l_false) {
            ptr_vector<expr> core;
            m_ctx->get_full_unsat_core (core);
            unsigned core_size = core.size ();
            m_uses_level = infty_level ();
            
            for (unsigned i = 0; i < core_size; ++i) {
              if (m_level_atoms_set.contains (core[i]))
              {
                unsigned sz = std::min (m_uses_level, m_neg_level_atoms.size ());
                for (unsigned j = 0; j < sz; ++j)
                  if (m_neg_level_atoms [j].get () == core[i])
                  {
                    m_uses_level = j;
                    break;
                  }
                SASSERT (!is_infty_level (m_uses_level));
              }
            }
        }

        if (result == l_false && m_core && m.proofs_enabled() && !m_subset_based_core) {
            TRACE ("spacer", tout << "theory core\n";);
            m_core->reset ();
            m_ctx->get_itp_core (*m_core);
        }
        else if (result == l_false && m_core) {
          m_core->reset ();
          m_ctx->get_unsat_core (*m_core);
        }
        m_core = 0;
        m_model = 0;
        m_subset_based_core = false;
        return result;
    }



    lbool prop_solver::check_assumptions (const expr_ref_vector & hard_atoms,
                                          expr_ref_vector& soft_atoms,
                                          unsigned num_bg, expr * const * bg,
                                          unsigned solver_id) 
    {
        m_ctx = m_contexts [solver_id == 0 ? 0 : 0 /* 1 */].get ();
        solver::scoped_push _s_(*m_ctx);
        unsigned old_bg_size = m_ctx->get_num_bg ();
        
        // safe_assumptions safe(*this, hard_atoms, soft_atoms);
        for (unsigned i = 0; i < num_bg; ++i) m_ctx->assert_expr (bg [i]);
        
        lbool res = internal_check_assumptions (hard_atoms, soft_atoms);

        // clear all bg assumptions
        SASSERT (old_bg_size <= m_ctx->get_num_bg ());
        m_ctx->pop_bg (m_ctx->get_num_bg () - old_bg_size);

        return res;
    }

    void prop_solver::collect_statistics(statistics& st) const {
    }

    void prop_solver::reset_statistics() {
    }

    


}