/*++
Copyright (c) 2017 Microsoft Corporation

Module Name:

    sat_drat.h

Abstract:
   
    Produce DRAT proofs.

Author:

    Nikolaj Bjorner (nbjorner) 2017-2-3

Notes:

--*/
#ifndef SAT_DRAT_H_
#define SAT_DRAT_H_

namespace sat {
    class drat {
    public:
        struct s_ext {};
        struct s_unit {};
        struct premise {
            enum { t_clause, t_unit, t_ext } m_type;
            union {
                clause* m_clause;
                literal m_literal;                
            };
            premise(s_ext, literal l): m_type(t_ext), m_literal(l) {}
            premise(s_unit, literal l): m_type(t_unit), m_literal(l) {}
            premise(clause* c): m_type(t_clause), m_clause(c) {}
        };
    private:
        enum status { asserted, learned, deleted, external };
        typedef ptr_vector<clause> watch;
        solver& s;
        std::ostream*           m_out;
        ptr_vector<clause>      m_proof;
        svector<status>         m_status;
        literal_vector          m_units;
        vector<watch>           m_watches;
        svector<lbool>          m_assignment;
        bool                    m_inconsistent;

        void dump(unsigned n, literal const* c, status st);
        void append(literal l, status st);
        void append(literal l1, literal l2, status st);
        void append(clause& c, status st);
        friend std::ostream& operator<<(std::ostream & out, status st);
        status get_status(bool learned) const;
        bool is_cleaned(unsigned n, literal const* lits) const;

        void declare(literal l);
        void assign(literal l);
        void propagate(literal l);
        void assign_propagate(literal l);
        void del_watch(clause& c, literal l);
        void verify(unsigned n, literal const* c);
        bool is_drup(unsigned n, literal const* c);
        bool is_drat(unsigned n, literal const* c);
        lbool value(literal l) const; 
        void trace(std::ostream& out, unsigned n, literal const* c, status st);
        void display(std::ostream& out) const;

    public:
        drat(solver& s);
        ~drat();  
        void add();
        void add(literal l, bool learned);
        void add(literal l1, literal l2, bool learned);
        void add(clause& c, bool learned);
        void add(literal_vector const& c, svector<premise> const& premises);
        
        void del(literal l);
        void del(literal l1, literal l2);
        void del(clause& c);
    };

};

#endif