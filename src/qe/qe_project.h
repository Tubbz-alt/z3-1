
#ifndef __QE_ARITH_H_
#define __QE_ARITH_H_

#include "model.h"
#include "expr_map.h"

namespace qe {
    /**
       Loos-Weispfenning model-based projection for a basic conjunction.
       Lits is a vector of literals.
       return vector of variables that could not be projected.
     */
    expr_ref arith_project(model& model, app_ref_vector& vars, expr_ref_vector const& lits);

    void arith_project(model& model, app_ref_vector& vars, expr_ref& fml);

    void arith_project(model& model, app_ref_vector& vars, expr_ref& fml, expr_map& map);

    //void array_project (model& model, app_ref_vector& vars, expr_ref& fml);

    void array_project_selects (model& model, app_ref_vector& arr_vars, expr_ref& fml, app_ref_vector& aux_vars, bool project_all_stores = false);

    void array_project_eqs (model& model, app_ref_vector& arr_vars, expr_ref& fml, app_ref_vector& aux_vars);
};

#endif
