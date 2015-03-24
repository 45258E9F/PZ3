#include "dist.hpp"

extern int core_num;
extern std::vector<int> expr_dist;

void dist_clause(std::set<unsigned> &symbol_set, std::vector<std::set<unsigned> > &symbol_sub, std::vector<int> &clause_weight)
{
    int length = symbol_sub.size();

    int q = length / core_num;
    int r = length % core_num;
    int index = 0;
    for (int i = 0; i < core_num; i++)
    {
        int pick_num = q;
        if (r > 0)
        {
            pick_num++;
            r--;
        }
        for (int j = 0; j < pick_num; j++, index++)
        {
            expr_dist.push_back(i);
        }
    }
}
