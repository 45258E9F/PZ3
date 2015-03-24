#ifndef _CORE_H_
#define _CORE_H_

#include <z3++.h>
#include <fstream>
#include <vector>
#include <map>
#include <set>
#include <list>
#include <cstdlib>
#include <ctime>
#include <boost/shared_ptr.hpp>

//#define PZ3_PRINT_TRACE
//#define PZ3_DIST
//#define PZ3_WEIRD_BUG_1

#define PZ3_MASTER_THREAD 0
#define PZ3_VAR_WEIGHT 1
#define PZ3_FUNC_WEIGHT 20

using namespace z3;

typedef enum
{
    PZ3_unsat,
    PZ3_unknown,
    PZ3_sat
} PZ3_Result;

typedef enum
{
    PZ3_file_ok,
    PZ3_file_noexist,
    PZ3_file_nosmt,
    PZ3_file_corrupt
} PZ3_File_Result;

typedef enum
{
    PZ3_smt1,
    PZ3_smt2,
    PZ3_smt_unknown
} PZ3_File_Type;

class closure;
class func_inst;
class eqclass;
class mutate_func_inst;
class local_func_inst;

// closure brings information of sort
class closure
{
    unsigned sortid;
    unsigned value;

public:
    closure()
    {
        sortid = 0;
        value = 0;
    }
    closure(unsigned sort_id, unsigned value_id)
    {
        sortid = sort_id;
        value = value_id;
    }
    ~closure() {}
    unsigned get_sort()
    {
        return sortid;
    }
    unsigned get_value()
    {
        return value;
    }
    void set(expr fs)
    {
        sortid = fs.get_sort().hash();
        value = fs.hash();
    }
    void set(closure clo)
    {
    	sortid = clo.get_sort();
    	value = clo.get_value();
    }
    void set_zero()
    {
    	value = 0;
    }
    bool is_zero()
    {
    	if(sortid == 0 && value == 0)
    		return true;
    	return false;
    }

    friend bool operator<(const closure &lhs, const closure &rhs)
    {
        if (lhs.sortid < rhs.sortid) return true;
        if (lhs.sortid > rhs.sortid) return false;
        if (lhs.value < rhs.value) return true;
        return false; // case of >=
    }

    friend bool operator==(const closure &lhs, const closure &rhs)
    {
        if (lhs.sortid != rhs.sortid) return false;
        if (lhs.value != rhs.value) return false;
        return true;
    }

    friend bool operator!=(const closure &lhs, const closure &rhs)
    {
    	if(lhs == rhs) return false;
    	return true;
    }

    friend bool operator>(const closure &lhs, const closure &rhs)
    {
        if (lhs < rhs || lhs == rhs)
            return false;
        return true;
    }

    friend std::ostream &operator<<(std::ostream &out, const closure &rhs)
    {
        out << "(" << rhs.sortid << "," << rhs.value << ")";
        return out;
    }
};

// FIXME: reference counter is not thread-safe
// data race in reference counter causes problems in localization function
class func_inst
{
protected:
    unsigned func;
    unsigned domain_len;
    boost::shared_ptr<closure[]> domain;
    unsigned local_ptr;

public:
    func_inst(unsigned fun_id, unsigned dom_len)
    {
        func = fun_id;
        domain_len = dom_len;
        domain = boost::shared_ptr<closure[]>(new closure[domain_len]);
        local_ptr = 0;
    }
    bool push(closure clo)
    {
        if (local_ptr < domain_len)
        {
            domain[local_ptr].set(clo);
            local_ptr++;
            return true;
        }
        else
        {
            return false;
        }
    }

    unsigned get_func()
    {
        return func;
    }
    closure operator[](unsigned i)
    {
        if (i < domain_len)
        {
            return domain[i];
        }
        // unexpected case below
        closure zero_clo;
        return zero_clo;
    }
    unsigned get_domain_length()
    {
        return domain_len;
    }

    friend bool operator<(const func_inst &lhs, const func_inst &rhs)
    {
        if (lhs.func < rhs.func) return true;
        if (lhs.func > rhs.func) return false;
        for (unsigned i = 0; i < lhs.domain_len; ++i)
        {
            if (lhs.domain[i] < rhs.domain[i]) return true;
            if (lhs.domain[i] > rhs.domain[i]) return false;
        }
        return false; // If they are all the same
    }

    friend bool operator==(const func_inst &lhs, const func_inst &rhs)
    {
        if (lhs.func == rhs.func)
        {
            for (unsigned i = 0; i < lhs.domain_len; i++)
            {
                if (lhs.domain[i] != rhs.domain[i])
                    return false;
            }
            return true;
        }
        else
        {
            return false;
        }
    }

    friend bool operator>(const func_inst &lhs, const func_inst &rhs)
    {
        if (lhs < rhs || lhs == rhs)
            return false;
        return true;
    }

    friend std::ostream &operator<<(std::ostream &out, const func_inst &rhs)
    {
        out << rhs.func << "(";
        for (unsigned i = 0; i < rhs.domain_len - 1; i++)
        {
            out << rhs.domain[i] << ",";
        }
        if (rhs.domain_len > 0)
        {
            out << rhs.domain[rhs.domain_len - 1];
        }
        out << ")";
        return out;
    }
};

class eqclass
{
protected:
    closure clo_value;
    expr *expr_value;
    std::vector<mutate_func_inst*> reverse_fist;
    bool is_set;
    // because there is a pointer, it is necessary to maintain a ref counter
    int * ref_count;
    void inc_ref()
    {
    	(*ref_count)++;
    }
    void dec_ref()
    {
    	if((*ref_count) > 0)
    		(*ref_count)--;
    }

public:
    eqclass()
    {
        clo_value.set_zero();
        expr_value = NULL;
        is_set = false;
        ref_count = new int(0);
    }

    eqclass(eqclass const &ec)
    {
    	clo_value = ec.clo_value;
    	expr_value = ec.expr_value;
    	reverse_fist = ec.reverse_fist;
    	is_set = ec.is_set;
    	ref_count = ec.ref_count;
    	inc_ref();
    }

    ~eqclass()
    {
    	dec_ref();
    	if(*ref_count == 0)
    	{
        	if (expr_value != NULL)
            	delete expr_value;
            delete ref_count;
    	}
    }

    void set_closure(closure clo)
    {
        clo_value.set(clo);
    }

    closure get_closure()
    {
    	return clo_value;
    }

    bool set_expr(expr fs)
    {
        if (expr_value == NULL)
        {
            expr_value = new expr(fs);
            is_set = true;
            inc_ref();
            return true;
        }
        // otherwise new expr will not replace old expr
        return false;
    }

    expr get_expr()
    {
    	if(expr_value == NULL)
    	{
    		std::cerr << "eqclass get_expr error" << std::endl;
    		exit(1);
    	}
    	return *expr_value;
    }

    void push_fist(mutate_func_inst * fist)
    {
    	reverse_fist.push_back(fist);
    }

    bool set_status()
    {
    	return is_set;
    }

    unsigned get_mfi_length()
    {
    	return reverse_fist.size();
    }

    mutate_func_inst* get_mfi(unsigned idx)
    {
    	if(idx < reverse_fist.size())
    		return reverse_fist.at(idx);
    	return NULL;
    }

    eqclass &operator=(eqclass &ec)
    {
    	ec.inc_ref();
    	dec_ref();
    	// copy ec to this object
    	clo_value = ec.clo_value;
    	expr_value = ec.expr_value;
    	reverse_fist = ec.reverse_fist;
    	is_set = ec.is_set;
    	ref_count = ec.ref_count;
    	return *this;
    }

    friend bool operator<(const eqclass & lhs, const eqclass & rhs)
    {
    	if(lhs.clo_value < rhs.clo_value) return true;
    	return false;
    }

    friend bool operator==(const eqclass & lhs, const eqclass & rhs)
    {
    	if(lhs.clo_value == rhs.clo_value) return true;
    	return false;
    }

    friend bool operator>(const eqclass & lhs, const eqclass & rhs)
    {
    	if(lhs.clo_value > rhs.clo_value) return true;
    	return false;
    }
};

class mutate_func_inst
{
protected:
    unsigned func_id;
    unsigned dom_len;
    eqclass **dom_cls;
    eqclass *range_cls;
    unsigned rem_valid_dom;
    mutate_func_inst *next;
    // because there is a pointer, it is necessary to maintain a ref counter
    int *ref_count;
    void inc_ref()
    {
    	(*ref_count)++;
    }
    void dec_ref()
    {
    	if((*ref_count) > 0)
    		(*ref_count)--;
    }

public:
    mutate_func_inst()
    {
        func_id = 0;
        dom_len = 0;
        dom_cls = NULL;
        range_cls = NULL;
        rem_valid_dom = 0;
        next = NULL;
        ref_count = new int(0);
    }

    mutate_func_inst(mutate_func_inst const &mfi)
    {
    	func_id = mfi.func_id;
    	dom_len = mfi.dom_len;
    	dom_cls = mfi.dom_cls;
    	range_cls = mfi.range_cls;
    	rem_valid_dom = mfi.rem_valid_dom;
    	next = mfi.next;
    	ref_count = mfi.ref_count;
    	inc_ref();
    }

    ~mutate_func_inst()
    {
    	dec_ref();
        // just created an array of pointers
        if((*ref_count) == 0)
        {
        	if (dom_cls != NULL)
            	delete [] dom_cls;
            delete ref_count;
        }
    }

    void set_func(unsigned id)
    {
        func_id = id;
    }

    unsigned get_func()
    {
    	return func_id;
    }

    void dom_init(unsigned length)
    {
        dom_len = length;
        rem_valid_dom = length;
        if (dom_cls == NULL)
        {
            dom_cls = new eqclass* [length];
            inc_ref();
        }
    }

    unsigned get_domain_length()
    {
    	return dom_len;
    }

    void set_dom(unsigned pos, eqclass *target)
    {
        if (dom_cls != NULL)
        {
            dom_cls[pos] = target;
        }
    }

    eqclass* get_dom(unsigned pos)
    {
    	if(dom_cls != NULL)
    	{
    		return dom_cls[pos];
    	}
    	return NULL;
    }

    void set_range(eqclass *target)
    {
        range_cls = target;
    }

    eqclass* get_range()
    {
    	return range_cls;
    }

    void dec_valid_dom()
    {
    	rem_valid_dom--;
    }

    unsigned get_rem_valid_dom()
    {
    	return rem_valid_dom;
    }

    void set_next(mutate_func_inst * mfi)
    {
    	next = mfi;
    }

    mutate_func_inst* get_next()
    {
    	return next;
    }

    mutate_func_inst &operator=(mutate_func_inst &mfi)
    {
    	mfi.inc_ref();
    	dec_ref();
    	func_id = mfi.func_id;
    	dom_len = mfi.dom_len;
    	dom_cls = mfi.dom_cls;
    	range_cls = mfi.range_cls;
    	rem_valid_dom = mfi.rem_valid_dom;
    	next = mfi.next;
    	ref_count = mfi.ref_count;
    	return *this;
    }

    bool is_empty()
    {
        if(func_id == 0 && dom_len == 0 && dom_cls == NULL && range_cls == NULL &&
        	rem_valid_dom == 0 && next == NULL)
        	return true;
        return false;
    }
};

class local_func_inst
{
protected:
	expr * expr_value;
	closure value;
    // because there is a pointer, it is necessary to maintain a ref counter
	int * ref_count;
	void inc_ref()
	{
		(*ref_count)++;
	}
	void dec_ref()
	{
		if((*ref_count) > 0)
			(*ref_count)--;
	}

public:
	local_func_inst()
	{
		expr_value = NULL;
		ref_count = new int(0);
	}

	local_func_inst(local_func_inst const &lfi)
	{
		expr_value = lfi.expr_value;
		value = lfi.value;
		ref_count = lfi.ref_count;
		inc_ref();
	}

	~local_func_inst()
	{
		dec_ref();
		if((*ref_count) == 0)
		{
			if(expr_value != NULL)
				delete expr_value;
			delete ref_count;
		}
	}

	void set_expr(expr fs)
	{
		if(expr_value == NULL)
		{
			expr_value = new expr(fs);
			inc_ref();
		}
	}

	expr get_expr()
	{
		return *expr_value;
	}

	void set_closure(closure clo)
	{
		value.set(clo);
	}

	closure get_closure()
	{
		return value;
	}

	local_func_inst &operator=(local_func_inst &lfi)
	{
		lfi.inc_ref();
		dec_ref();
		expr_value = lfi.expr_value;
		value = lfi.value;
		ref_count = lfi.ref_count;
		return *this;
	}
};

/* Print proper usage of program */
void usage(char const *prog_name);

/* Get parameters from command prompt */
void get_args(char *const argv[]);

/* Check the satisfiability of a benchmark file */
PZ3_Result solve_file();

/* Problem division */
void *division(void *rank);

/* Parse smtlib file */
PZ3_File_Result parse_file(context &ctx, expr &fs);

/* Check file type for following process */
PZ3_File_Type check_filetype();

/* Convert parsed formula into CNF */
void fs_to_cnf(int const my_rank, expr &fs, expr_vector &list);

/* Solve sub-problems in parallel */
void *subsolve(void *rank);

/* Output the variable list of a formula */
void get_vars(expr fs, std::map<unsigned, int> &vl, std::map<unsigned, int> &fl);

/* Merge two variable maps together */
void map_merge(std::map<unsigned, int> &result, std::map<unsigned, int> input);

/* Merge variable maps of clauses in the same core */
void vars_merge();

/* Merge function maps of clauses in the same core */
void funcs_merge();

/* Collect shared variables from different contexts(cores) */
void shared_collect();

/* Extract expression objects of shared variables on parallel */
void *extract_vars(void *arg);

/* Extract variable expressions in specified list from a specified formula */
void assoc_vars(expr in_fs, std::set<unsigned> &vars, std::map<unsigned, expr> &var_map);

/* Extract function declarations in specified list from a specified formula */
void assoc_funs(expr in_fs, std::set<unsigned> &funs, std::map<unsigned, func_decl> &fun_map);

/* Function for master thread -- calculating the model for shared variables */
void *master_func(void *arg);

/* Function for slave thread -- calculating interpolation for sub-formulas */
void *slave_func(void *arg);

/* Function for interpolation between 2 constraints */
Z3_lbool PZ3_interpolate(context &c, expr fs1, expr fs2, expr &interp, Z3_model *md);

/* Translate an object of function declaration into other context */
func_decl PZ3_translate_func_decl(context &source_c, func_decl fd, context &target_c);

/* Localize terms for a sub-problem based on global shared terms */
void localization(context & c, std::map<unsigned, expr> & my_var, std::map<unsigned, func_decl> & my_fun, std::vector<local_func_inst> & result, std::set<closure> & valid_closure);

/* Choose a default closure for new function instance by voting method */
closure get_most_freq(std::vector<closure> & vec);

#endif
