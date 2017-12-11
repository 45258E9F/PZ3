#include "core.hpp"
#include "contextManager.hpp"
#include "dist/dist.hpp"

#define MAX_STACK_SIZE_PER_THREAD 4000u * 1024u * 1024u

typedef boost::chrono::high_resolution_clock boost_clock;

std::string file_path;
unsigned core_num;
contextManager cm;

closure true_clo;
closure false_clo;

// clause distribution, variable list, clauses per core, formula per core respectively
std::vector<int> expr_dist;
std::vector<std::map<unsigned, int> > expr_var;
std::vector<std::map<unsigned, int> > expr_fun;

// FIXME: use expr_vector instead of std::vector<expr>
std::vector<std::vector<expr> > expr_table;
// expr_list: sub-formulas for every core
std::vector<expr> expr_list;

// structures on shared variables
// sv_set: set of shared variables
// sf_set: set of shared functions
std::set<unsigned> sv_set;
std::set<unsigned> sf_set;
// var_fs: set of variables for each sub-formula
// fun_fs: set of functions for each sub-formula
std::vector<std::set<unsigned> > var_fs;
std::vector<std::set<unsigned> > fun_fs;

// var_expr: list of shared variables in each sub-formula
// fun_expr: list of shared function declarations in each formula
std::vector<std::map<unsigned, expr> > var_expr;
std::vector<std::map<unsigned, func_decl> > fun_expr;
// svexpr: expressions corresponding to shared variables and classification number
// sfist: shared function instances and corresponding classification number
std::map<unsigned, closure> svexpr;
std::map<func_inst, closure> sfist;

// checklist indicates check result for every sub-formula
std::vector<check_result> checklist;
// interpo_list is a list for interpolants in every sub-formula
std::vector<expr> interpo_list;
// table_list: equivalence class conversion table of each sub-problem
std::vector<std::map<closure, closure> > table_list;
// model_list is a map of models for every sub-formula
std::map<int, model> model_list;

bool need_term = false;

#ifdef PZ3_PROFILING
boost::chrono::milliseconds subsolve_time = boost::chrono::milliseconds::zero();
boost::chrono::milliseconds conciliate_time = boost::chrono::milliseconds::zero();
#endif

#ifdef PZ3_FINE_GRAINED_PROF
boost::atomic<long long> decomp_time(0);
boost::atomic<long long> solve_time(0);
boost::atomic<long long> interp_time(0);
boost::atomic<long long> formulate_time(0);
boost::atomic<long long> ssr_time(0);
#endif

// for parallel control
pthread_mutex_t err_mutex;
pthread_barrier_t crea_barrier;
pthread_barrier_t stat_barrier;
pthread_barrier_t dist_barrier;

pthread_barrier_t barrier1;
pthread_barrier_t barrier2;

int main(int argc, char *argv[])
{
    PZ3_Result fresult = PZ3_unknown;

    if (argc != 3)
    {
        usage(argv[0]);
    }
    get_args(argv);

    if (core_num <= 0)
    {
        std::cerr << "Invalid core number!\n";
        exit(1);
    }
    cm.init_q_ctx(core_num);
    pthread_mutex_init(&err_mutex, NULL);
    pthread_barrier_init(&crea_barrier, NULL, core_num);
    pthread_barrier_init(&stat_barrier, NULL, core_num);
    pthread_barrier_init(&dist_barrier, NULL, core_num);
    expr_table = std::vector<std::vector<expr> >(core_num);
	
    fresult = solve_file();

    switch (fresult)
    {
    case PZ3_unsat:
        std::cout << "unsat\n";
        break;
    case PZ3_sat:
        std::cout << "sat\n";
        break;
    default:
        std::cout << "unknown\n";
    }
    return 0;
}

void usage(char const *prog_name)
{
    std::cerr << "Usage: " << prog_name << " ";
    std::cerr << "[Path of smtlib file] [Number of cores]\n";
    exit(1);
}

void get_args(char *const argv[])
{
    file_path = std::string(argv[1]);
    core_num = atoi(argv[2]);
}

PZ3_Result solve_file()
{

#ifdef PZ3_PROFILING
	boost_clock::time_point division_start = boost_clock::now();
#endif

    // Step 1: Preprocessing (Problem division)
    // If core_num is 1, it is just a sequential version of Z3
    if (core_num == 1)
    {
        config cfg;
        cfg.set("MODEL", true);
        cfg.set("PROOF", true);
        context c(cfg);

        Z3_ast m_fs = Z3_parse_smtlib2_file(c, file_path.c_str(), 0, 0, 0, 0, 0,
                                            0);
        expr fs = to_expr(c, m_fs);
        solver s(c);
        s.add(fs);
        switch (s.check())
        {
        case sat:
            return PZ3_sat;
        case unsat:
            return PZ3_unsat;
        default:
            return PZ3_unknown;
        }
    }

    // Otherwise, prepare for parallel processing
    pthread_t *thread_handles = (pthread_t *)malloc(
                                    core_num * sizeof(pthread_t));

    for (unsigned i = 0; i < core_num; i++)
    {
        pthread_create(&thread_handles[i], NULL, division, (void *) ((long) i));
    }

#ifdef PZ3_PRINT_TRACE
    std::cout << "Thread creation complete" << std::endl;
#endif

    for (unsigned i = 0; i < core_num; i++)
    {
        pthread_join(thread_handles[i], NULL);
    }
    free(thread_handles);

#ifdef PZ3_FINE_GRAINED_PROF
    boost_clock::time_point division_start = boost_clock::now();
#endif
    // Combining clauses in each core into formula
    for (unsigned i = 0; i < core_num; i++)
    {
        int lenq = expr_table.at(i).size();
        expr sprb(cm.get_q_ctx(i));
        if (lenq > 0)
        {
            sprb = expr_table.at(i).at(0);
            for (int j = 1; j < lenq; j++)
            {
                sprb = sprb && expr_table.at(i).at(j);
            }
        }
        else
        {
            sprb = cm.get_q_ctx(i).bool_val(true);
        }
        expr_list.push_back(sprb);
    }
#ifdef PZ3_FINE_GRAINED_PROF
    boost::chrono::milliseconds division_time = boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - division_start);
    decomp_time.fetch_add(division_time.count(), boost::memory_order_relaxed);
    std::cout << "DECOMP: " << decomp_time << std::endl;
#endif

#ifdef PZ3_DIST
    for(unsigned i = 0; i < core_num; i++)
    {
        std::cout << "Sub-problem " << i << ":" << std::endl;
        std::cout << expr_list.at(i) << std::endl;
    }
    std::cout << "Terminate early for test" << std::endl;
    exit(0);
#endif

#ifdef PZ3_PROFILING
	std::cout << "DECOMPOSITION: " << boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - division_start) << std::endl;
#endif

#ifdef PZ3_PROFILING
    boost_clock::time_point subsolve_start = boost_clock::now();
#endif

    // Solve sub-formuals in parallel
    thread_handles = (pthread_t *) malloc(core_num * sizeof(pthread_t));
    pthread_attr_t attr_subsolve;
#ifndef PZ3_ONECORE
    cpu_set_t cpus;
#endif
    pthread_attr_init(&attr_subsolve);
    for (unsigned i = 0; i < core_num; i++)
    {
#ifndef PZ3_ONECORE
        CPU_ZERO(&cpus);
        CPU_SET(i, &cpus);
        pthread_attr_setaffinity_np(&attr_subsolve, sizeof(cpu_set_t), &cpus);
#endif
        pthread_create(&thread_handles[i], &attr_subsolve, subsolve, (void *) ((long) i));
    }

#ifdef PZ3_PRINT_TRACE
    std::cout << "Thread creation complete" << std::endl;
#endif

    for (unsigned i = 0; i < core_num; i++)
    {
        pthread_join(thread_handles[i], NULL);
    }
    free(thread_handles);

#ifdef PZ3_PROFILING
	subsolve_time += boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - subsolve_start);
#endif
#ifdef PZ3_FINE_GRAINED_PROF
    std::cout << "SOLVE: " << solve_time << std::endl;
#endif

    // Step 2: Reconciliation
    // Collect variable information for formulas in each core
    vars_merge();
    funcs_merge();
    // Since then we can invoke get_conn_factor() to evaluate connectivity of clause distribution
#ifdef PZ3_PRINT_TRACE
    std::cout << "merge complete!" << std::endl;
#endif
    // Collect shared variables from different contexts(cores)
    shared_collect();

#ifdef PZ3_PRINT_TRACE
    std::cout << "shared_collect complete!" << std::endl;
#endif

    // Some preparations
    pthread_barrier_init(&barrier1, NULL, core_num + 1);
    pthread_barrier_init(&barrier2, NULL, core_num + 1);
    checklist = std::vector<check_result>(core_num);
    for (unsigned i = 0; i < core_num; i++)
    {
        expr empty_expr = expr(cm.get_q_ctx(i));
        interpo_list.push_back(empty_expr);
    }
    table_list = std::vector<std::map<closure, closure> >(core_num); 
    // We prepare model_list later for there is no way to create an empty model on the fly

#ifdef PZ3_PRINT_TRACE
    std::cout << "Before creating threads" << std::endl;
#endif

    thread_handles = (pthread_t *) malloc((core_num + 1) * sizeof(pthread_t));
	pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, MAX_STACK_SIZE_PER_THREAD);
    for (unsigned i = 0; i < core_num; i++)
    {
#ifndef PZ3_ONECORE
        CPU_ZERO(&cpus);
        CPU_SET(i, &cpus);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
#endif
        pthread_create(&thread_handles[i], &attr, slave_func,
                       (void *) ((long) i));
    }
#ifndef PZ3_ONECORE
    CPU_ZERO(&cpus);
    CPU_SET(PZ3_MASTER_THREAD, &cpus);
    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
#endif    
	pthread_create(&thread_handles[core_num], &attr, master_func, NULL);

    void *tret;
    pthread_join(thread_handles[core_num], &tret);
    for (unsigned i = 0; i < core_num; i++)
    {
        pthread_join(thread_handles[i], NULL);
    }

#ifdef PZ3_PROFILING
    std::cout << "SUBSOLVE: " << subsolve_time << std::endl;
    std::cout << "CONCILIATION: " << conciliate_time << std::endl;
#endif

#ifdef PZ3_FINE_GRAINED_PROF
    std::cout << "INTERP: " << interp_time << std::endl;
    std::cout << "FORM: " << formulate_time << std::endl;
    std::cout << "SSR: " << ssr_time << std::endl;
    std::cout << "GENSOLVE: " << solve_time << std::endl;
#endif

    switch ((long) tret)
    {
    case 0:
        return PZ3_sat;
    case 1:
        return PZ3_unsat;
    default:
        // case 2:
        return PZ3_unknown;
    }

}

void *division(void *rank)
{
#ifdef PZ3_FINE_GRAINED_PROF
    boost_clock::time_point div_start = boost_clock::now();
    boost::chrono::milliseconds div_time = boost::chrono::milliseconds::zero();
#endif
    long my_rank_l = (long) rank;
    int my_rank = (int) my_rank_l;
    config cfg;
    cfg.set("MODEL", true);
    cfg.set("PROOF", true);
    cm.mk_q_ctx(my_rank, cfg);
    context &ctx = cm.get_q_ctx(my_rank);
    expr fs(ctx);
    PZ3_File_Result pfr;
    expr_vector list(ctx);

    pfr = parse_file(ctx, fs);
    switch (pfr)
    {
    case PZ3_file_noexist:
        pthread_mutex_lock(&err_mutex);
        std::cerr << "From thread " << my_rank << "\n";
        std::cerr << "SMTLIB file doesn't exist.\n";
        exit(1);
    case PZ3_file_nosmt:
        pthread_mutex_lock(&err_mutex);
        std::cerr << "From thread " << my_rank << "\n";
        std::cerr << "Input file is not a valid SMTLIB file.\n";
        exit(1);
    case PZ3_file_corrupt:
        pthread_mutex_lock(&err_mutex);
        std::cerr << "From thread " << my_rank << "\n";
        std::cerr << "Input SMTLIB file is corrupted.\n";
        exit(1);
    default:
        break;
    }

    // Convert arbitrary formula into CNF
    // Attention: Z3 uses tseitin method to convert a formula into CNF form in order to avoid exponential increase of problem size
    // Therefore there are some auxiliary variables(All of them are boolean form). We don't need to care them.
    fs_to_cnf(my_rank, fs, list);
    int num_clause = list.size();

    // Distribute clauses in preparing for several cores
    if (my_rank == PZ3_MASTER_THREAD)
    {
        assert(expr_var.size() == 0);
        assert(expr_fun.size() == 0);
        expr_var = std::vector<std::map<unsigned, int> >(num_clause);
        expr_fun = std::vector<std::map<unsigned, int> >(num_clause);
    }
#ifdef PZ3_FINE_GRAINED_PROF
    div_time += boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - div_start);
#endif
    pthread_barrier_wait(&crea_barrier);
#ifdef PZ3_FINE_GRAINED_PROF
    div_start = boost_clock::now();
#endif
    for (int i = my_rank; i < num_clause; i += core_num)
    {
        assert(expr_var.at(i).size() == 0);
        assert(expr_fun.at(i).size() == 0);
        get_vars(list[i], expr_var.at(i), expr_fun.at(i));
    }
#ifdef PZ3_FINE_GRAINED_PROF
    div_time += boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - div_start);
#endif
    pthread_barrier_wait(&stat_barrier);

    if (my_rank == PZ3_MASTER_THREAD)
    {
#ifdef PZ3_FINE_GRAINED_PROF
        div_start = boost_clock::now();
#endif
        // merge all symbols in each clause and calculate weight of each clause
        std::set<unsigned> symbol_set;
        std::vector<int> clause_weight = std::vector<int>(num_clause, 0);
        std::vector<std::set<unsigned> > symbol_sub = std::vector<std::set<unsigned> >(num_clause);
        for(int i = 0; i < num_clause; i++)
        {
            std::map<unsigned, int>::iterator vit = expr_var.at(i).begin();
            std::map<unsigned, int>::iterator vit_end = expr_var.at(i).end();
            std::set<unsigned>& my_sub = symbol_sub.at(i);
            while(vit != vit_end)
            {
                symbol_set.insert(vit->first);
                my_sub.insert(vit->first);
                clause_weight.at(i) += (vit->second);
                ++vit;   
            }
            std::map<unsigned, int>::iterator fit = expr_fun.at(i).begin();
            std::map<unsigned, int>::iterator fit_end = expr_fun.at(i).end();
            while(fit != fit_end)
            {
                symbol_set.insert(fit->first);
                my_sub.insert(fit->first);
                clause_weight.at(i) += (fit->second);
                ++fit;
            }
        }

        dist_clause(symbol_set, symbol_sub, clause_weight);
#ifdef PZ3_FINE_GRAINED_PROF
        div_time += boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - div_start);
#endif
    }
    pthread_barrier_wait(&dist_barrier);

#ifdef PZ3_FINE_GRAINED_PROF
    div_start = boost_clock::now();
#endif
    // Generate expression for corresponding core
    assert(expr_dist.size() == list.size());
    for (int i = 0; i < num_clause; i++)
    {
        // The clause belongs to this core
        if (expr_dist.at(i) == my_rank)
        {
            expr_table.at(my_rank).push_back(list[i]);
        }
    }
#ifdef PZ3_FINE_GRAINED_PROF
    div_time += boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - div_start);
    decomp_time.fetch_add(div_time.count(), boost::memory_order_relaxed);
#endif

    return NULL;
}

PZ3_File_Result parse_file(context &ctx, expr &fs)
{
    std::ifstream file;
    PZ3_File_Type ftype = PZ3_smt_unknown;

    file.open(file_path.c_str(), std::ifstream::in);
    if (!file)
        return PZ3_file_noexist;
    file.close();
    ftype = check_filetype();
    if (ftype == PZ3_smt_unknown)
        return PZ3_file_nosmt;
    else if (ftype == PZ3_smt2)
    {
        Z3_ast m_fs = Z3_parse_smtlib2_file(ctx, file_path.c_str(), 0, 0, 0, 0,
                                            0, 0);
        expr im_fs = expr(ctx, m_fs);
        fs = im_fs;
        return PZ3_file_ok;
    }
    else
    {
        // Prepare for SMTLIB format benchmark
        return PZ3_file_ok;
    }
}

PZ3_File_Type check_filetype()
{
    std::string smt1str(".smt");
    std::string smt2str(".smt2");
    int len = file_path.length();
    if (len < 5)
        return PZ3_smt_unknown;
    if (file_path.compare(len - 5, 5, smt2str) == 0)
        return PZ3_smt2;
    if (file_path.compare(len - 4, 4, smt1str) == 0)
        return PZ3_smt1;
    return PZ3_smt_unknown;
}

void fs_to_cnf(int const my_rank, expr &fs, expr_vector &list)
{
    context &ctx = cm.get_q_ctx(my_rank);
    goal g(ctx);
    g.add(fs);

    params p_simp(ctx);
    p_simp.set(":elim_and", true);
    tactic t = with(tactic(ctx, "simplify"), p_simp)
               & tactic(ctx, "elim-term-ite") & tactic(ctx, "tseitin-cnf");
    apply_result aresult = t.apply(g);

    if (aresult.size() != 1)
    {
        std::cerr << "Unexpected subgoal number.\n" << std::endl;
        exit(1);
    }
    goal sg = aresult[0];
    unsigned expr_num = sg.size();
    for (unsigned i = 0; i < expr_num; i++)
    {
        list.push_back(sg[i]);
    }
}

void *subsolve(void *rank)
{
#ifdef PZ3_FINE_GRAINED_PROF
    boost_clock::time_point solve_start = boost_clock::now();
    boost::chrono::milliseconds subsolve_time;
#endif
#ifdef PZ3_PROFILING
    boost_clock::time_point subsolve_start = boost_clock::now();
#endif
    long my_rank_l = (long) rank;
    int my_rank = (int) my_rank_l;
    context &ctx = cm.get_q_ctx(my_rank);
    solver s(ctx);
    s.add(expr_list.at(my_rank));
    switch (s.check())
    {
    case unsat:
        pthread_mutex_lock(&err_mutex);
#ifdef PZ3_PRINT_TRACE
        std::cout << "From thread " << my_rank << ": unsat\n";
#endif
#ifdef PZ3_PROFILING
        // before exit, we should output the subsolve time
        subsolve_time += boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - subsolve_start);
        std::cout << "SUBSOLVE: " << subsolve_time << std::endl;
        std::cout << "CONCILIATION: " << conciliate_time << std::endl;
#endif
        std::cout << "unsat" << std::endl;
#ifdef PZ3_FINE_GRAINED_PROF
            // before exit, we should output time metrics
        subsolve_time = boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - solve_start);
        solve_time.fetch_add(subsolve_time.count(), boost::memory_order_relaxed);
        std::cout << "SOLVE: " << solve_time << std::endl;
        std::cout << "INTERP: " << 0 << std::endl;
        std::cout << "SSR: " << 0 << std::endl;
        std::cout << "FORM: " << 0 << std::endl;
        std::cout << "GENSOLVE: " << solve_time << std::endl;
#endif
        exit(0);
    case sat:
#ifdef PZ3_PRINT_TRACE
        pthread_mutex_lock(&err_mutex);
        std::cout << "From thread " << my_rank << ": sat\n";
        pthread_mutex_unlock(&err_mutex);
#endif
#ifdef PZ3_FINE_GRAINED_PROF
        subsolve_time = boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - solve_start);
        solve_time.fetch_add(subsolve_time.count(), boost::memory_order_relaxed);
#endif
        break;
    default:
#ifdef PZ3_PRINT_TRACE
        pthread_mutex_lock(&err_mutex);
        std::cout << "From thread " << my_rank << ": unknown\n";
        pthread_mutex_unlock(&err_mutex);
#endif
#ifdef PZ3_FINE_GRAINED_PROF
        subsolve_time = boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - solve_start);
        solve_time.fetch_add(subsolve_time.count(), boost::memory_order_relaxed);
#endif
        return NULL;
    }

    return NULL;
}

void get_vars(expr fs, std::map<unsigned, int> &vl, std::map<unsigned, int> &fl)
{
    if (!fs.is_app())
        return;
    if (fs.is_const())
    {
        if (fs.is_numeral())
            return;
        else
        {
            unsigned hashid = fs.hash();
            std::map<unsigned, int>::iterator it = vl.find(hashid);
            if (it == vl.end())
            {
                // FIXME: weight of variable
                vl.insert(std::pair<unsigned, int>(hashid, PZ3_VAR_WEIGHT));
            }
            // We don't care if this symbol appears for several times
            return;
        }
    }
    else if (fs.decl().decl_kind() == Z3_OP_UNINTERPRETED)
    {
        // deal with uninterpreted function
        unsigned hashid = fs.decl().hash();
        unsigned arity = fs.decl().arity();
        std::map<unsigned, int>::iterator it = fl.find(hashid);
        if (it == fl.end())
        {
            // FIXME: weight of function (arity considered)
            int fun_weight = arity * PZ3_FUNC_WEIGHT;
            fl.insert(std::pair<unsigned, int>(hashid, fun_weight));
        }
    }
    int narg = fs.num_args();
    for (int i = 0; i < narg; i++)
    {
        get_vars(fs.arg(i), vl, fl);
    }
}

void map_merge(std::set<unsigned> &result,
               std::map<unsigned, int> input)
{
    std::map<unsigned, int>::iterator it = input.begin();
    std::map<unsigned, int>::iterator it_end = input.end();
    while (it != it_end)
    {
        std::set<unsigned>::iterator ite = result.find(it->first);
        if (ite == result.end())
            result.insert(it->first);
        it++;
    }
}

/*
  Prerequisite: expr_dist, expr_var
*/
void vars_merge()
{
    var_fs = std::vector<std::set<unsigned> >(core_num);
    int dist_len = expr_dist.size();
    for (int i = 0; i < dist_len; i++)
    {
        int core_index = expr_dist.at(i);
        map_merge(var_fs.at(core_index), expr_var.at(i));
    }
}
/*
  Prerequisite: expr_dist, expr_fun
 */
void funcs_merge()
{
    fun_fs = std::vector<std::set<unsigned> >(core_num);
    int dist_len = expr_dist.size();
    for (int i = 0; i < dist_len; i++)
    {
        int core_index = expr_dist.at(i);
        map_merge(fun_fs.at(core_index), expr_fun.at(i));
    }
}

/*
  Prerequisite: var_fs, fun_fs,  expr_list
*/
void shared_collect()
{
    // Extract shared variables first
    std::map<unsigned, int> var_map;
    for (unsigned i = 0; i < core_num; i++)
    {
        std::set<unsigned> fsv = var_fs.at(i);
        std::set<unsigned>::iterator it_begin = fsv.begin();
        std::set<unsigned>::iterator it_end = fsv.end();
        for (std::set<unsigned>::iterator it = it_begin; it != it_end;
                ++it)
        {
            std::map<unsigned, int>::iterator vm_it = var_map.find(
                        *it);
            if (vm_it == var_map.end())
                var_map.insert(std::pair<unsigned, int>(*it, 1));
            else
                (vm_it->second)++;
        }
    }
    // Extract shared functions
    std::map<unsigned, int> fun_map;
    for (unsigned i = 0; i < core_num; i++)
    {
        std::set<unsigned> fsf = fun_fs.at(i);
        std::set<unsigned>::iterator it_begin = fsf.begin();
        std::set<unsigned>::iterator it_end = fsf.end();
        for (std::set<unsigned>::iterator itr = it_begin; itr != it_end; ++itr)
        {
            std::map<unsigned, int>::iterator fm_it = fun_map.find(*itr);
            if (fm_it == fun_map.end())
                fun_map.insert(std::pair<unsigned, int>(*itr, 1));
            else
                (fm_it->second)++;
        }
    }

    // Extract variables appeared for more than 1 time
    std::map<unsigned, int>::iterator vm_begin = var_map.begin();
    std::map<unsigned, int>::iterator vm_end = var_map.end();
    for (std::map<unsigned, int>::iterator it = vm_begin; it != vm_end;
            ++it)
    {
        if (it->second > 1)
        {
            // This is a shared variable
            sv_set.insert(it->first);
        }
    }

    // Extract functions appeared for more than 1 time
    std::map<unsigned, int>::iterator fm_begin = fun_map.begin();
    std::map<unsigned, int>::iterator fm_end = fun_map.end();
    for (std::map<unsigned, int>::iterator it = fm_begin; it != fm_end; ++it)
    {
        if (it->second > 1)
        {
            // This is a shared function
            sf_set.insert(it->first);
        }
    }

    // If sv_set is empty, then every sub-formula is separated
    // Then result of instance is SAT
    if (sv_set.size() == 0)
    {
#ifdef PZ3_PRINT_TRACE
        std::cout << "Separated problem" << std::endl;
#endif
        std::cout << "sat" << std::endl;
        exit(0);
    }
#ifdef PZ3_PRINT_TRACE
    std::cout << "Shared variables: " << sv_set.size() << std::endl;
    std::cout << "Shared functions: " << sf_set.size() << std::endl;
#endif

    // Extract shared variables and function declarations by cores on parallel
    var_expr = std::vector<std::map<unsigned, expr> >(core_num);
    fun_expr = std::vector<std::map<unsigned, func_decl> >(core_num);
    pthread_t *thread_handles = (pthread_t *) malloc(
                                    core_num * sizeof(pthread_t));
    for (unsigned i = 0; i < core_num; i++)
    {
        pthread_create(&thread_handles[i], NULL, extract_vars,
                       (void *) ((long) i));
    }
    for (unsigned i = 0; i < core_num; i++)
    {
        pthread_join(thread_handles[i], NULL);
    }

#ifdef PZ3_PRINT_TRACE
    std::cout << "SV and SF Extraction Complete!" << std::endl;
#endif

    // Create a context for shared variables
    config cfg;
    cfg.set("MODEL", true);
    cfg.set("PROOF", true);
    cm.mk_s_ctx(cfg);

    // Succeeded if reaching there.
}

void *extract_vars(void *arg)
{
    long my_rank_l = (long) arg;
    int my_rank = (int) my_rank_l;

    std::set<unsigned> my_sv;
    std::set<unsigned>::iterator it1 = var_fs.at(my_rank).begin();
    std::set<unsigned>::iterator it1_end = var_fs.at(my_rank).end();
    std::set<unsigned>::iterator it2 = sv_set.begin();
    std::set<unsigned>::iterator it2_end = sv_set.end();
    while (it1 != it1_end && it2 != it2_end)
    {
        if (*it1 == *it2)
        {
            my_sv.insert(*it1);
            it1++;
            it2++;
        }
        else if (*it1 > *it2)
            it2++;
        else
            it1++;
    }

    std::set<unsigned> my_sf;
    std::set<unsigned>::iterator it3 = fun_fs.at(my_rank).begin();
    std::set<unsigned>::iterator it3_end = fun_fs.at(my_rank).end();
    std::set<unsigned>::iterator it4 = sf_set.begin();
    std::set<unsigned>::iterator it4_end = sf_set.end();
    while (it3 != it3_end && it4 != it4_end)
    {
        if (*it3 == *it4)
        {
            my_sf.insert(*it3);
            it3++;
            it4++;
        }
        else if (*it3 > *it4)
            it4++;
        else
            it3++;
    }

    // my_sv and my_sf extracted
    // extract variables from sub-formula
    assoc_vars(expr_list.at(my_rank), my_sv, var_expr.at(my_rank));

    // extract function declarations from sub-formula
    assoc_funs(expr_list.at(my_rank), my_sf, fun_expr.at(my_rank));

    return NULL;
}

void assoc_vars(expr in_fs, std::set<unsigned> &vars,
                std::map<unsigned, expr> &var_map)
{
    // if vars is reduced to empty list, return from this function
    if (vars.size() == 0)
        return;
    if (!in_fs.is_app())
        return;
    if (in_fs.is_const())
    {
        if (in_fs.is_numeral())
            return;
        else
        {
            unsigned hashid = in_fs.hash();
            std::set<unsigned>::iterator it = vars.find(hashid);
            if (it == vars.end())
            {
                // this variable does not need to be added to var_map
                return;
            }
            else
            {
                // this variable needs to be added to var_map
                var_map.insert(std::pair<unsigned, expr>(hashid, in_fs));
                vars.erase(it);
                return;
            }
        }
    }
    int narg = in_fs.num_args();
    for (int i = 0; i < narg; i++)
    {
        assoc_vars(in_fs.arg(i), vars, var_map);
    }
}

void assoc_funs(expr in_fs, std::set<unsigned> &funs, std::map<unsigned, func_decl> &fun_map)
{
    if (funs.size() == 0)
        return;
    if (!in_fs.is_app())
        return;
    if (in_fs.is_const())
    {
        // const -- not need recursion
        return;
    }
    else if (in_fs.decl().decl_kind() == Z3_OP_UNINTERPRETED)
    {
        // deal with uninterpreted function
        unsigned hashid = in_fs.decl().hash();
        std::set<unsigned>::iterator it = funs.find(hashid);
        if (it == funs.end())
        {
            // (1) this function is private in this sub-formula
            // (2) this function has already been added into fun_map
            // DON'T RETURN for recursive funtion invocation.
        }
        else
        {
            // this function need to be added
            fun_map.insert(std::pair<unsigned, func_decl>(hashid, in_fs.decl()));
            funs.erase(it);
        }
    }
    int narg = in_fs.num_args();
    for (int i = 0; i < narg; i++)
    {
        assoc_funs(in_fs.arg(i), funs, fun_map);
    }
}

void *master_func(void *arg)
{
#ifdef PZ3_PROFILING
    boost_clock::time_point subsolve_start;
    boost_clock::time_point conciliate_start;

    conciliate_start = boost_clock::now();
#endif
#ifdef PZ3_FINE_GRAINED_PROF
    boost_clock::time_point master_start;
    boost::chrono::milliseconds master_time;
#endif
    long return_val = 2;
    context &m_ctx = cm.get_s_ctx();
    // fi_vec: used to store function instances in shared context
    expr_vector fi_vec(m_ctx);
    solver sv_solve(m_ctx);
    bool pure_literal = false;

#ifdef PZ3_FINE_GRAINED_PROF
    master_start = boost_clock::now();
#endif
    // get hash value of TRUE and FALSE
    unsigned bool_id = m_ctx.bool_sort().hash();
    unsigned true_id = m_ctx.bool_val(true).hash();
    unsigned false_id = m_ctx.bool_val(false).hash();
    true_clo = closure(bool_id, true_id);
    false_clo = closure(bool_id, false_id);

    // First we need to extract variables for shared context
    std::map<unsigned, expr> sv_map;
    for (unsigned i = 0; i < core_num; i++)
    {
        std::map<unsigned, expr>::iterator vesub = var_expr.at(i).begin();
        std::map<unsigned, expr>::iterator vesub_end = var_expr.at(i).end();
        while (vesub != vesub_end && sv_set.size() != 0)
        {
            unsigned hashid = vesub->first;
            std::set<unsigned>::iterator findit = sv_set.find(hashid);
            if (findit != sv_set.end())
            {
                // we found an element
                sv_set.erase(findit);
                expr correex = vesub->second;
                expr localex = to_expr(m_ctx, Z3_translate(cm.get_q_ctx(i), correex, m_ctx));
                sv_map.insert(std::pair<unsigned, expr>(hashid, localex));
            }
            ++vesub;
        }
    }

    if (sf_set.size() == 0)
    {
        pure_literal = true;
    }

    // Then we extract function declarations for shared context
    std::map<unsigned, func_decl> sf_map;
    for (unsigned i = 0; i < core_num; i++)
    {
        std::map<unsigned, func_decl>::iterator fdsub = fun_expr.at(i).begin();
        std::map<unsigned, func_decl>::iterator fdsub_end = fun_expr.at(i).end();
        while (fdsub != fdsub_end && sf_set.size() != 0)
        {
            unsigned hashid = fdsub->first;
            std::set<unsigned>::iterator findit = sf_set.find(hashid);
            if (findit != sf_set.end())
            {
                // we found an element
                sf_set.erase(findit);
                func_decl correfd = fdsub->second;
                func_decl localfd = PZ3_translate_func_decl(cm.get_q_ctx(i), correfd, m_ctx);
                sf_map.insert(std::pair<unsigned, func_decl>(hashid, localfd));
            }
            ++fdsub;
        }
    }

    // Initialize congruence closure for shared variables (ignore functions temporarily)
    assert(svexpr.size() == 0);
    // sv_solve is empty, but check() is necessary for getting an empty model
    sv_solve.check();
    model pre_model = sv_solve.get_model();
    for (std::map<unsigned, expr>::iterator svit = sv_map.begin(); svit != sv_map.end(); ++svit)
    {
        expr evalresult = pre_model.eval(svit->second, true);
        closure myclo;
        myclo.set(evalresult);
        svexpr.insert(std::pair<unsigned, closure>(svit->first, myclo));
    }
    // initially there is no shared function instance
#ifdef PZ3_FINE_GRAINED_PROF
    master_time = boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - master_start);
    ssr_time.fetch_add(master_time.count(), boost::memory_order_relaxed);
#endif

#ifdef PZ3_PROFILING
    conciliate_time += boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - conciliate_start);
#endif

#ifdef PZ3_PRINT_TRACE
    pthread_mutex_lock(&err_mutex);
    std::cout << "Master thread preparation completed" << std::endl;
    pthread_mutex_unlock(&err_mutex);
#endif

    while (true)
    {
#ifdef PZ3_PROFILING
        subsolve_start = boost_clock::now();
#endif
        pthread_barrier_wait(&barrier1);
        if (need_term)
            break;
        pthread_barrier_wait(&barrier2);
#ifdef PZ3_PROFILING
        subsolve_time += boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - subsolve_start);
#endif
        // check "check_result" of sub-formulas
#ifdef PZ3_PRINT_TRACE
        pthread_mutex_lock(&err_mutex);
        std::cout << "Master thread working" << std::endl;
        pthread_mutex_unlock(&err_mutex);
#endif

#ifdef PZ3_PROFILING
        conciliate_start = boost_clock::now();
#endif
#ifdef PZ3_FINE_GRAINED_PROF
        master_start = boost_clock::now();
#endif

        bool allsat = true;
        for (unsigned i = 0; i < core_num; i++)
        {
            if (checklist.at(i) == unsat)
            {
                allsat = false;
                Z3_ast z3_interpex = Z3_translate(cm.get_q_ctx(i), interpo_list.at(i), m_ctx);
                expr interpconstr = to_expr(m_ctx, z3_interpex);
                sv_solve.add(interpconstr);
            }
        }

        if (allsat)
        {
#ifdef PZ3_PRINT_TRACE
            std::cout << "ALLSAT" << std::endl;
#endif
            // fake-witness is impossible for pure-literal problems
            if (pure_literal)
            {
                need_term = true;
                return_val = 0;
            }

            // Step 1: read model to count function instances
            std::map<func_inst, std::vector<closure> > fist_count;
            for(std::map<int, model>::iterator it = model_list.begin(); it != model_list.end(); ++it)
            {
                int this_rank = it->first;
                model & this_model = it->second;
                std::map<closure, closure> & this_table = table_list.at(this_rank);
                std::map<unsigned, func_decl> & this_fun = fun_expr.at(this_rank);
                for(std::map<unsigned, func_decl>::iterator fun_it = this_fun.begin(); fun_it != this_fun.end(); fun_it++)
                {
                    unsigned fun_id = fun_it->first;
                    func_decl fun = fun_it->second;
                    func_interp fun_itp = this_model.get_func_interp(fun);
                    unsigned entry_num = fun_itp.num_entries();
                    for(unsigned i = 0; i < entry_num; i++)
                    {
                        func_entry this_entry = fun_itp.entry(i);
                        unsigned arg_num = this_entry.num_args();
                        // we only consider function instances whose arguments are all shared
                        // otherwise, it is impossible to appear multiple times in different sub-problems
                        bool all_shared = true;
                        func_inst fist(fun_id, arg_num);
                        for(unsigned j = 0; j < arg_num; j++)
                        {
                            closure dom_clo;
                            dom_clo.set(this_entry.arg(j));
                            std::map<closure, closure>::iterator findit = this_table.find(dom_clo);
                            if(findit == this_table.end())
                            {
                                // this closure is not shared
                                all_shared = false;
                                break;
                            }
                            fist.push(findit->second);
                        }
                        if(!all_shared)
                            continue;
                        // get value of this entry
                        closure range_clo;
                        range_clo.set(this_entry.value());
                        {
                            std::map<closure, closure>::iterator range_it = this_table.find(range_clo);
                            if(range_it == this_table.end())
                            {
                                // if its range is not shared, set it as a special zero closure
                                // FIXME: sort information should be ratained
                                // FIXME: if its range is of boolean type, set it as true or false instead of zero closure
                                if(range_clo.get_sort() == true_clo.get_sort())
                                    range_clo.set(true_clo);
                                else
                                    range_clo.set_zero();
                            }
                            else
                                range_clo.set(range_it->second);
                        }

                        std::map<func_inst, std::vector<closure> >::iterator fist_it = fist_count.find(fist);
                        if(fist_it == fist_count.end())
                        {
                            std::vector<closure> value_vec;
                            // insert value of this function instance
                            value_vec.push_back(range_clo);
                            fist_count.insert(std::pair<func_inst, std::vector<closure> >(fist, value_vec));
                        }
                        else
                        {
                            (fist_it->second).push_back(range_clo);
                        }
                    }
                }
            }

            // Step 2: extract shared function instances
            bool is_all_shared_inst = true;
            for(std::map<func_inst, std::vector<closure> >::iterator it = fist_count.begin(); it != fist_count.end(); ++it)
            {
                func_inst this_fist = it->first;
                std::vector<closure> & clo_vec = it->second;
                unsigned times = clo_vec.size();
                if(times > 1)
                {
                    // this instance is shared
                    std::map<func_inst, closure>::iterator findit;
                    // congruence closure stays unchanged, so we can directly search in sfist
                    findit = sfist.find(this_fist);
                    if(findit == sfist.end())
                    {
                        // this is a new function instance
                        is_all_shared_inst = false;
                        // using vote method to choose a closure as its default range closure    
                        closure most_freq = get_most_freq(clo_vec);
                        // if a zero closure returned, we need to do more
#if 0
                        if(most_freq.is_zero())
                        {
                            // FIXME: it is incorrect to randomly choose a closure in domain for new term because it is possible that
                            //        range sort is different from any of ones in domain
                            unsigned dom_len = this_fist.get_domain_length();
                            srand((unsigned)time(0));
                            unsigned rand_pos = rand() % dom_len;
                            most_freq = this_fist[rand_pos];
                        }
#endif
                        sfist.insert(std::pair<func_inst, closure>(this_fist, most_freq));                        
                    }
                }
            }
            if(is_all_shared_inst == true)
            {
                // no new instance added
                need_term = true;
                return_val = 0;
                continue;
            }
            // if reached here, our work is done for ALLSAT
        }
        else
        {
#ifdef PZ3_PRINT_TRACE
            std::cout << "SOME_UNSAT" << std::endl;
#endif
            switch (sv_solve.check())
            {
            case sat:
            {
                model sv_model = sv_solve.get_model();
                // update svexpr
                for(std::map<unsigned, expr>::iterator it = sv_map.begin(); it != sv_map.end(); ++it)
                {
                    expr eval_result = sv_model.eval(it->second, true);
                    closure res_clo;
                    res_clo.set(eval_result);
                    svexpr[it->first] = res_clo;
                }

                // update sfist
                // a naive approach: overwrite all the terms in sfist
#ifdef PZ3_PRINT_TRACE
                std::cout << "Updating sfist..." << std::endl;
#endif
                sfist.clear();
                unsigned num_func_decl = sv_model.num_funcs();
                for(unsigned i = 0; i < num_func_decl; i++)
                {
                    func_decl this_func = sv_model.get_func_decl(i);
                    unsigned this_id = this_func.hash();
                    func_interp this_itp = sv_model.get_func_interp(this_func);
                    unsigned entry_num = this_itp.num_entries();
                    for(unsigned j = 0; j < entry_num; j++)
                    {
                        func_entry this_entry = this_itp.entry(j);
                        unsigned arg_num = this_entry.num_args();
                        func_inst fist(this_id, arg_num);
                        for(unsigned k = 0; k < arg_num; k++)
                        {
                            closure this_clo;
                            this_clo.set(this_entry.arg(k));
                            fist.push(this_clo);
                        }
                        closure range_clo;
                        range_clo.set(this_entry.value());
                        sfist.insert(std::pair<func_inst, closure>(fist, range_clo));
                    }
                }
            }
            break;
            case unsat:
                return_val = 1;
                need_term = true;
                break;
            default:
                return_val = 2;
                need_term = true;
                break;
            }
        }

#ifdef PZ3_PROFILING
        conciliate_time += boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - conciliate_start);
#endif
#ifdef PZ3_FINE_GRAINED_PROF
        master_time = boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - master_start);
        ssr_time.fetch_add(master_time.count(), boost::memory_order_relaxed);
#endif

    }

    return (void *) return_val;
}

void *slave_func(void *arg)
{
#ifdef PZ3_FINE_GRAINED_PROF
    boost_clock::time_point slave_start;
    boost::chrono::milliseconds slave_time;
#endif
    long my_rank_l = (long) arg;
    int my_rank = (int) my_rank_l;
    std::map<unsigned, expr> &my_var = var_expr.at(my_rank);
    std::map<unsigned, func_decl> &my_fun = fun_expr.at(my_rank);
    context &my_ctx = cm.get_q_ctx(my_rank);

    {
        // create an empty model for location
        // therefore, we don't need to reconstruct model list again and again, just by using =
        // it reduces many unnecessary locks and accelerate the execution
        solver empty_solve(my_ctx);
        empty_solve.check();
        model empty_model = empty_solve.get_model();
        pthread_mutex_lock(&err_mutex);
        model_list.insert(std::pair<int, model>(my_rank, empty_model));
        pthread_mutex_unlock(&err_mutex);
    }

#ifdef PZ3_PRINT_TRACE
    pthread_mutex_lock(&err_mutex);
    std::cout << "Slave thread " << my_rank << " preparation completed"
              << std::endl;
    pthread_mutex_unlock(&err_mutex);
#endif

    // 4 cases:
    // (1) No shared variables and functions : SAT
    // (2) No shared variables, some shared functions : SAT
    // (3) No shared functions, some shared variables : when #SV=1, SAT; otherwise it depends
    // (4) Some shared variables and functions : it depends
    // case 1-3 results in no constraints constructed, therefore constr_expr = TRUE.

    while (true)
    {
        pthread_barrier_wait(&barrier1);
        if (need_term)
            break;
#ifdef PZ3_PRINT_TRACE
        pthread_mutex_lock(&err_mutex);
        std::cout << "Slave thread " << my_rank << " working" << std::endl;
        pthread_mutex_unlock(&err_mutex);
#endif

#ifdef PZ3_FINE_GRAINED_PROF
        slave_start = boost_clock::now();
#endif
        // Step 1: localization
        std::vector<local_func_inst> result;
        // extract non-empty closure for following works
        std::set<closure> valid_closure;
        localization(my_ctx, my_var, my_fun, result, valid_closure);

#ifdef PZ3_PRINT_TRACE
        pthread_mutex_lock(&err_mutex);
        std::cout << "Slave thread " << my_rank << " localization complete!" << std::endl;
        pthread_mutex_unlock(&err_mutex);
#endif

        // Step 2: make statistics for terms
        std::map<closure, expr_vector> term_stat;
        // initialize this map
        for(std::set<closure>::iterator it = valid_closure.begin(); it != valid_closure.end(); ++it)
        {
            closure this_clo = *it;
            expr_vector new_evec(my_ctx);
            term_stat.insert(std::pair<closure, expr_vector>(this_clo, new_evec));
        }
        // first add variables
        for(std::map<unsigned, expr>::iterator it = my_var.begin(); it != my_var.end(); ++it)
        {
            unsigned var_id = it->first;
            expr var_expr = it->second;
            closure var_clo = svexpr[var_id];
            ((term_stat.find(var_clo))->second).push_back(var_expr);
        }
        // then add localized function instances
        unsigned result_num = result.size();
        for(unsigned i = 0; i < result_num; i++)
        {
            expr fist_expr = result.at(i).get_expr();
            closure fist_clo = result.at(i).get_closure();
            ((term_stat.find(fist_clo))->second).push_back(fist_expr);
        }

        // Step 3: construct constrain expression
        expr_vector cnsts_list(my_ctx);
        // Processing true/false expression
        std::map<closure, expr_vector>::iterator findit;
        findit = term_stat.find(true_clo);
        if(findit != term_stat.end())
        {
            expr_vector trueexs = findit->second;
            unsigned len = trueexs.size();
            for(unsigned i = 0; i < len; i++)
            {
                cnsts_list.push_back(trueexs[i]);
            }
            term_stat.erase(findit);
        }
        findit = term_stat.find(false_clo);
        if(findit != term_stat.end())
        {
            expr_vector falseexs = findit->second;
            unsigned len = falseexs.size();
            for(unsigned i = 0; i < len; i++)
            {
                cnsts_list.push_back(!falseexs[i]);
            }
            term_stat.erase(findit);
        }
        // Equality inside closure
        for(std::map<closure, expr_vector>::iterator it = term_stat.begin(); it != term_stat.end(); ++it)
        {
            expr_vector eq_list = it->second;
            unsigned len = eq_list.size();
            for(unsigned i = 1; i < len; i++)
            {
                expr lex = eq_list[i - 1];
                expr rex = eq_list[i];
                cnsts_list.push_back(lex == rex);
            }
        }
        // Inequality between closures of the same sort
        unsigned sortvalue = 0;
        std::vector<expr> ineq_list;
        for(std::map<closure, expr_vector>::iterator it = term_stat.begin(); it != term_stat.end(); ++it)
        {

            closure this_clo = it->first;
            unsigned this_sort = this_clo.get_sort();
            if(sortvalue != this_sort)
            {
                // from now on expressions are of new sort
                // make C(n,2) inequalities instead of a long distinct expression
                // for it could provide interpolants of higher quality
                sortvalue = this_sort;
                unsigned len = ineq_list.size();
                for(unsigned i = 0; i < len; i++)
                {
                    expr lex = ineq_list.at(i);
                    for(unsigned j = i + 1; j < len; j++)
                    {
                        expr rex = ineq_list.at(j);
                        cnsts_list.push_back(lex != rex);
                    }
                }
                ineq_list.clear();
                ineq_list.push_back((it->second)[0]);
            }
            else
            {
                ineq_list.push_back((it->second)[0]);
            }
        }
        // maybe there are terms remaining in list
        unsigned neqlen = ineq_list.size();
        for(unsigned i = 0; i < neqlen; i++)
        {
            expr lex = ineq_list.at(i);
            for(unsigned j = i + 1; j < neqlen; j++)
            {
                expr rex = ineq_list.at(j);
                cnsts_list.push_back(lex != rex);
            }
        }
        // conjunct expressions into one
        expr constr_expr(my_ctx);
        unsigned cnsts_len = cnsts_list.size();
        if(cnsts_len == 0)
        {
            // no constrain
            constr_expr = my_ctx.bool_val(true);
        }
        else
        {
            array<Z3_ast> _cnsts_list(cnsts_list);
            Z3_ast and_fs = Z3_mk_and(my_ctx, cnsts_len, _cnsts_list.ptr());
            constr_expr = to_expr(my_ctx, and_fs);
        }
#ifdef PZ3_FINE_GRAINED_PROF
        slave_time = boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - slave_start);
        formulate_time.fetch_add(slave_time.count(), boost::memory_order_relaxed);
#endif

        // Step 4: two contraint expressions are constructed
        // (1) expr_list.at(my_rank)
        // (2) constr_expr
        // We use interpolation function to obtain a SAT model (if they are SAT) or an interpolant (if they are UNSAT) or a model if it is unknown whether they are SAT
        // ** Z3 has repaired interpolation bug due to memeory leak (stack overflow). Therefore we revise our code with iZ3 interpolation system again.
        // *** Z3 revised its API for interpolation. We find a method to compute interpolation and gather them into a function namely PZ3_interpolate
        #if 0
        expr interp(my_ctx);
        Z3_model _sat_model;
        Z3_lbool status;
        #endif

        // If correction is ensured, we can delete profiling from original code to enable parallel interpolation computation
        #if 0
        pthread_mutex_lock(&err_mutex);
        status = PZ3_interpolate(my_ctx, expr_list.at(my_rank), constr_expr, interp, &_sat_model);
        pthread_mutex_unlock(&err_mutex);
        #endif

#ifdef PZ3_FINE_GRAINED_PROF
        slave_start = boost_clock::now();
#endif
        solver solve(my_ctx);
        solve.add(expr_list.at(my_rank));
        solve.add(constr_expr);
        switch(solve.check())
        {
            case unsat:
            {
#ifdef PZ3_FINE_GRAINED_PROF
                slave_time = boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - slave_start);
                solve_time.fetch_add(slave_time.count(), boost::memory_order_relaxed);
                slave_start = boost_clock::now();
#endif
                expr proof = solve.proof();
                array<Z3_ast> _sts(2);
                _sts[0] = expr_list.at(my_rank);
                _sts[1] = constr_expr;
                Z3_ast _interp;

                pthread_mutex_lock(&err_mutex);
                Z3_interpolate_proof(my_ctx, proof, 2, _sts.ptr(), 0, 0, &_interp, 0, 0);
                pthread_mutex_unlock(&err_mutex);
                
                expr interp = to_expr(my_ctx, _interp);
                checklist.at(my_rank) = unsat;
                interpo_list.at(my_rank) = interp;
#ifdef PZ3_FINE_GRAINED_PROF
                slave_time = boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - slave_start);
                interp_time.fetch_add(slave_time.count(), boost::memory_order_relaxed);
#endif
            }
            break;
            case sat:
            {
#ifdef PZ3_FINE_GRAINED_PROF
                slave_time = boost::chrono::duration_cast<boost::chrono::milliseconds> (boost_clock::now() - slave_start);
                solve_time.fetch_add(slave_time.count(), boost::memory_order_relaxed);
#endif
                model sat_model = solve.get_model();
                checklist.at(my_rank) = sat;
                // Push a model to model_list
                // It is safe to concurrently access data from different locations
                model_list.find(my_rank)->second = sat_model;

                // Construct conversion table for master thread to interprete this model
                // localized closure -> global shared closure
                table_list.at(my_rank).clear();
                std::map<closure, closure> & this_table = table_list.at(my_rank);
                for(std::map<closure, expr_vector>::iterator it = term_stat.begin(); it != term_stat.end(); ++it)
                {
                    closure global_clo = it->first;
                    expr test_expr = (it->second)[0];
                    expr test_res = sat_model.eval(test_expr);
                    closure local_clo;
                    local_clo.set(test_res);
                    // they are distinct in global, so are in local
                    this_table.insert(std::pair<closure, closure>(local_clo, global_clo));
                }
            }
            break;
            default:
            {
                // unknown: just mark it as illegal case
                std::cerr << "Unknown reason" << std::endl;
                exit(1);
            }
        }

        pthread_barrier_wait(&barrier2);
    }

#if 0
        if (status == Z3_L_FALSE)
        {
            // 2 constraints are inconsistent. Z3 produces an interpolant.
#ifdef PZ3_PRINT_TRACE
            pthread_mutex_lock(&err_mutex);
            std::cout << "Slave thread " << my_rank << " has interp: " << std::endl;
            std::cout << interp << std::endl;
            pthread_mutex_unlock(&err_mutex);
#endif
            checklist.at(my_rank) = unsat;
            interpo_list.at(my_rank) = interp;
        }
        else if (status == Z3_L_TRUE)
        {
#if 1
            // PZ3_interpolate can possibly produce a wrong model
            solver s(my_ctx);
            s.add(expr_list.at(my_rank));
            s.add(constr_expr);
            s.check();
            model sat_model = s.get_model();
#endif
            //model sat_model = model(my_ctx, _sat_model);
            checklist.at(my_rank) = sat;
#ifdef PZ3_PRINT_TRACE
            pthread_mutex_lock(&err_mutex);
            std::cout << "Slave thread " << my_rank << ": sat" << std::endl;
            //std::cout << sat_model << std::endl;
            pthread_mutex_unlock(&err_mutex);
#endif
            // Push a model to model_list
            // It is safe to concurrently access data from different locations
            model_list.find(my_rank)->second = sat_model;

            // Construct conversion table for master thread to interprete this model
            // localized closure -> global shared closure
            table_list.at(my_rank).clear();
            std::map<closure, closure> & this_table = table_list.at(my_rank);
            for(std::map<closure, expr_vector>::iterator it = term_stat.begin(); it != term_stat.end(); ++it)
            {
                closure global_clo = it->first;
                expr test_expr = (it->second)[0];
                expr test_res = sat_model.eval(test_expr);
                closure local_clo;
                local_clo.set(test_res);
                // they are distinct in global, so are in local
                this_table.insert(std::pair<closure, closure>(local_clo, global_clo));
            }
        }
        else
        {
            // unknown: just mark it as illegal case
            std::cerr << "Unknown reason" << std::endl;
            exit(1);
        }
        pthread_barrier_wait(&barrier2);
    }
#endif

    return NULL;
}

Z3_lbool PZ3_interpolate(context &c, expr fs1, expr fs2, expr &interp, Z3_model *md)
{
    expr pattern = expr(c, Z3_mk_interpolant(c, fs1));
    pattern = (pattern && fs2);
    array<Z3_ast_vector> _avec(1);
    Z3_lbool result = Z3_compute_interpolant(c, pattern, 0, _avec.ptr(), md);

    if (result == Z3_L_FALSE)
    {
        // Two constraints are unsatisfiable, there exists an interpolant between them
        interp = expr(c, Z3_ast_vector_get(c, _avec[0], 0));
    }

    return result;
}

func_decl PZ3_translate_func_decl(context &source_c, func_decl fd, context &target_c)
{
    Z3_func_decl _fd = Z3_to_func_decl(target_c, Z3_translate(source_c, fd, target_c));
    return func_decl(target_c, _fd);
}

void localization(context & c, std::map<unsigned, expr> & my_var, std::map<unsigned, func_decl> & my_fun, std::vector<local_func_inst> & result, std::set<closure> & valid_closure)
{
    eqclass * eq_list;
    mutate_func_inst * fist_list; // linked list
    
    // Step 1: count equivalence classes
    std::map<closure, unsigned> closure_set;
    unsigned idx = 0;
    for(std::map<unsigned, closure>::iterator it = svexpr.begin(); it != svexpr.end(); ++it)
    {
        closure var_clo = it->second;
        std::pair<std::map<closure, unsigned>::iterator, bool> ret;
        ret = closure_set.insert(std::pair<closure, unsigned>(var_clo, idx));
        if(ret.second == true) idx++;
    }
    for(std::map<func_inst, closure>::iterator it = sfist.begin(); it != sfist.end(); ++it)
    {
        func_inst this_fist = it->first;
        closure this_range = it->second;
        unsigned dom_len = this_fist.get_domain_length();
        for(unsigned i = 0; i < dom_len; i++)
        {
            closure dom_clo = this_fist[i];
            std::pair<std::map<closure, unsigned>::iterator, bool> ret;
            ret = closure_set.insert(std::pair<closure, unsigned>(dom_clo, idx));
            if(ret.second == true) idx++;
        }
        std::pair<std::map<closure, unsigned>::iterator, bool> ret;
        ret = closure_set.insert(std::pair<closure, unsigned>(this_range, idx));
        if(ret.second == true) idx++;
    }
    unsigned eq_list_len = closure_set.size();
    assert(eq_list_len == idx);
    eq_list = new eqclass[eq_list_len];
    for(std::map<closure, unsigned>::iterator it = closure_set.begin(); it != closure_set.end(); ++it)
    {
        closure this_clo = it->first;
        unsigned index = it->second;
        eq_list[index].set_closure(this_clo);
    }

    // Step 2: add function instances
    // FIXME: if the function declaration doesn't exist in this sub-problem, don't add it
    // FIXME: fist_list_len-> number of all shared function instances
    //        counter-> number of MY shared function instances
    //        fist_list_len >= counter
    //unsigned fist_list_len = sfist.size();
    fist_list = new mutate_func_inst;
    mutate_func_inst *ptr = fist_list;
    //unsigned counter = 0;
    mutate_func_inst *prv = NULL;
    for(std::map<func_inst, closure>::iterator it = sfist.begin(); it != sfist.end(); ++it)
    {
        func_inst this_fist = it->first;
        closure this_range = it->second;
        unsigned func_id = this_fist.get_func();
        if(my_fun.find(func_id) == my_fun.end())
        {
            // this function declaration doesn't exist
            // just skip this instance
            continue;
        }
        unsigned dom_len = this_fist.get_domain_length();
        ptr->set_func(func_id);
        ptr->dom_init(dom_len);
        for(unsigned i = 0; i < dom_len; i++)
        {
            closure dom_clo = this_fist[i];
            unsigned index = (closure_set.find(dom_clo))->second;
            // create a bidirectional connection
            ptr->set_dom(i, &eq_list[index]);
            eq_list[index].push_fist(ptr);
        }
        // create uni-directional connection for range
        unsigned range_index = (closure_set.find(this_range))->second;
        ptr->set_range(&eq_list[range_index]);
        //counter++;

        // create next node
        mutate_func_inst *ptr2 = new mutate_func_inst;
        ptr->set_next(ptr2);
        prv = ptr;
        ptr = ptr2;
    }

    // FIXME: if there is no function instance, fist_list has one empty element
    // check if this list is empty, if so just delete this element and set fist_list as NULL
    // FIXME: last element of fist_list is always an empty node
    if(fist_list->is_empty())
    {
        delete fist_list;
        fist_list = NULL;
    }
    else
    {
        // if there are at least 2 elements in linked list
        prv->set_next(NULL);
        delete ptr;
    }

    // since then the structure is complete

    // Step 3: add variables first (variables of this sub-problem)
    for(std::map<unsigned, expr>::iterator it = my_var.begin(); it != my_var.end(); ++it)
    {
        unsigned var_id = it->first;
        expr ex = it->second;
        closure var_clo = svexpr[var_id];
        std::map<closure, unsigned>::iterator findit = closure_set.find(var_clo);
        unsigned index = (closure_set.find(var_clo))->second;
        // FIXME: if there are 2 variables in one closure, then set_expr() is invoked twice
        // although the second set_expr() fails, it still decreases counter
        // therefore counter can be reduced to an incorrect value
        if(eq_list[index].set_expr(ex))
        {
            unsigned fist_len = eq_list[index].get_mfi_length();
            for(unsigned i = 0; i < fist_len; i++)
            {
                eq_list[index].get_mfi(i)->dec_valid_dom();
            }
            valid_closure.insert(var_clo);  
        }
    }

    // FIXME: not all the shared variables!
#if 0
    for(std::map<unsigned, closure>::iterator it = svexpr.begin(); it != svexpr.end(); ++it)
    {
        unsigned var_id = it->first;
        closure var_clo = it->second;
        unsigned index = (closure_set.find(var_clo))->second;
        expr ex = (my_var.find(var_id))->second;
        eq_list[index].set_expr(ex);
        unsigned fist_len = eq_list[index].get_mfi_length();
        for(unsigned i = 0; i < fist_len; i++)
        {
            eq_list[index].get_mfi(i)->dec_valid_dom();
        }
        valid_closure.insert(var_clo);
    }
#endif

    // Step 4: recursion to find all localized instances
    bool nochange = false;
    while(!nochange)
    {
        nochange = true;
        mutate_func_inst *ptr = fist_list;
        mutate_func_inst *prev_ptr = NULL;
        while(ptr != NULL)
        {
            if(ptr->get_rem_valid_dom() == 0)
            {
                // this function instance can be instantiated
                nochange = false;
                unsigned func_id = ptr->get_func();
                func_decl func = (my_fun.find(func_id))->second;
                expr_vector params(c);
                unsigned dom_len = ptr->get_domain_length();
                for(unsigned j = 0; j < dom_len; j++)
                {
                    params.push_back(ptr->get_dom(j)->get_expr());
                }
                expr newterm = func(params);
                eqclass * range = ptr->get_range();
                // if possible, update equivalence class
                if(range->set_status() == false)
                {
                    range->set_expr(newterm);                    
                    // propagate the effects
                    unsigned fist_len = range->get_mfi_length();
                    for(unsigned i = 0; i < fist_len; i++)
                    {
                        (range->get_mfi(i))->dec_valid_dom();
                    }
                }
                local_func_inst lfi;
                lfi.set_expr(newterm);
                lfi.set_closure(range->get_closure());
                result.push_back(lfi);
                valid_closure.insert(range->get_closure());
                // this instance can be deleted
                if(prev_ptr == NULL)
                {
                    // this is the first element to de deleted
                    fist_list = ptr->get_next();
                    delete ptr;
                    ptr = fist_list;
                }
                else
                {
                    prev_ptr->set_next(ptr->get_next());
                    delete ptr;
                    ptr = prev_ptr->get_next();
                }
            }
            else
            {
                prev_ptr = ptr;
                ptr = ptr->get_next();
            }
        }
        
    }

    // localization finished
    // some clean works
    delete [] eq_list;
    mutate_func_inst* clean_ptr = fist_list;
    while(clean_ptr != NULL)
    {
        mutate_func_inst* temp = clean_ptr;
        clean_ptr = clean_ptr->get_next();
        delete temp;
    }

}

closure get_most_freq(std::vector<closure> & vec)
{
    unsigned vec_len = vec.size();
    std::map<closure, unsigned> count_table;
    for(unsigned i = 0; i < vec_len; i++)
    {
        closure this_clo = vec.at(i);
        std::map<closure, unsigned>::iterator findit;
        findit = count_table.find(this_clo);
        if(findit == count_table.end())
        {
            // not found
            count_table.insert(std::pair<closure, unsigned>(this_clo, 1));
        }
        else
            (findit->second)++;
    }
    unsigned max = 0;
    closure best_clo; // zero closure by default
    for(std::map<closure, unsigned>::iterator it = count_table.begin(); it != count_table.end(); ++it)
    {
        closure this_clo = it->first;
        unsigned vote = it->second;
        if(vote > max)
        {   
            max = vote;
            best_clo = this_clo;
        }
    }
    // if a zero closure returned, most of instances are not in shared closures
    return best_clo;
}
