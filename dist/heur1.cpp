#include "dist.hpp"
#include <list>
#include <climits>

extern int core_num;
extern std::vector<int> expr_dist;

void dist_clause(std::set<unsigned> &symbol_set, std::vector<std::set<unsigned> > &symbol_sub, std::vector<int> &clause_weight)
{
	unsigned cls_num = symbol_sub.size();

	// initialize nodes
	std::map<simple_node, std::vector<int> > stat;
	for(unsigned i = 0; i < cls_num; i++)
	{
		std::set<unsigned> & my_sub = symbol_sub.at(i);
		simple_node sn;
		sn.set_symbol(symbol_set, my_sub);
		std::map<simple_node, std::vector<int> >::iterator findit = stat.find(sn);
		if(findit == stat.end())
		{
			std::vector<int> newvec;
			newvec.push_back(i);
			stat.insert(std::pair<simple_node, std::vector<int> >(sn, newvec));
		}
		else
		{
			(findit->second).push_back(i);
		}
	}

	// construct a poset for nodes
	// bottom nodes increase only
	std::vector<node*> bot_node;
	// top nodes increase and reduce
	std::list<node*> top_node;

	for(std::map<simple_node, std::vector<int> >::iterator it = stat.begin(); it != stat.end(); ++it)
	{
		simple_node this_sn = it->first;
		int clause_idx = (it->second).at(0);
		int clause_wgt = clause_weight.at(clause_idx);
		int clause_num = (it->second).size();
		node * nd = new node(this_sn);
		nd->set_weight(clause_wgt);
		nd->set_clause_num(clause_num);

		// make connections
		std::list<node*>::iterator list_it = top_node.begin();
		while(list_it != top_node.end())
		{
			node * top_nd = *list_it;
			if(top_search(nd, top_nd))
			{
				// new added node replaces a top node to be a new top node
				list_it = top_node.erase(list_it);
				// list_it will point to the element following the erased one
			}
			else
			{
				// (1) new added node cannot connect to any sub-node of this top node
				// (2) new added node connect to a sub node, but original top node is not replaced
				list_it++;
			}
		}

		// if this node doesn't have any child, it is a bottom node
		// if this node doesn't have any parent, it is a top node
		if(!nd->have_child())
		{
			bot_node.push_back(nd);
		}
		if(!nd->have_parent())
		{
			top_node.push_back(nd);
		}
	}

	// search for a shortest path from bottom node
	unsigned bot_len = bot_node.size();
	std::vector<simple_node> cur_path;
	std::vector<simple_node> best_path;
	unsigned best_wgt = UINT_MAX;
	for(unsigned i = 0; i < bot_len; i++)
	{
		find_shortest(bot_node.at(i), cur_path, best_path, 0, best_wgt, 0);
	}

	if(best_wgt < UINT_MAX)
	{
		// if such a path is derived
		expr_dist = std::vector<int>(cls_num, 0); // initialize first
		std::vector<int> rem_vec;
		unsigned bp_len = best_path.size();
		for(unsigned i = 0; i < bp_len; i++)
		{
			simple_node this_sn = best_path.at(i);
			std::vector<int> &this_vecint = (stat.find(this_sn))->second;
			rem_vec.insert(rem_vec.begin(), this_vecint.begin(), this_vecint.end());
		}
		// clauses in rem_vec are divided into (core_num - 1) partitions
#if 0
		unsigned rem_len = rem_vec.size();
		for(unsigned i = 0; i < rem_len; i++)
		{
			expr_dist.at(rem_vec.at(i)) = i % (core_num - 1) + 1;
		}
#endif

#if 1
		for(unsigned i = 0; i < (unsigned)(core_num - 1); i++)
		{
			expr_dist.at(rem_vec.at(i)) = i + 1;
		}
#endif
		// ok, remaining clauses compose into sub-formula 0
		return;
	}
	else
	{
		// if no such a path is derived, just use sequential deivision method
		int q = cls_num / core_num;
		int r = cls_num % core_num;
		for(int i = 0; i < core_num; i++)
		{
			int pick_num = q;
			if(r > 0)
			{
				pick_num++;
				r--;
			}
			for(int j = 0; j < pick_num; j++)
				expr_dist.push_back(i);
		}
	}

}

bool top_search(node * new_nd, node * top_nd)
{
	simple_node insc;
	insc = new_nd->intersect(top_nd);
	if(insc.symbol_size() == 0)
		return false;
	if(top_nd->is_subset(&insc))
	{
		// this top node could be replaced
		new_nd->add_child(top_nd);
		top_nd->add_parent(new_nd);
		return true;
	}
	// otherwise, search for sub-nodes
	unsigned num_child = top_nd->child_num();
	for(unsigned i = 0; i < num_child; i++)
	{
		sub_search(&insc, new_nd, top_nd->get_child(i));
	}
	return false;
}

void sub_search(simple_node * insc, node * new_nd, node * sub_nd)
{
	if(sub_nd->is_subset(insc))
	{
		// if this sub-node contains symbols only from intersection
		new_nd->add_child(sub_nd);
		sub_nd->add_parent(new_nd);
		return;
	}
	// otherwise, search for sub-nodes
	unsigned num_child = sub_nd->child_num();
	for(unsigned i = 0; i < num_child; i++)
	{
		sub_search(insc, new_nd, sub_nd->get_child(i));
	}
}

void find_shortest(node * this_node, std::vector<simple_node> & cur_path, std::vector<simple_node> & best_path, unsigned cur_wgt, unsigned & best_wgt, unsigned cur_num)
{
	unsigned new_num = cur_num + this_node->clause_num();
	unsigned new_wgt = cur_wgt + this_node->get_weight();
	simple_node this_sn = *this_node;
	cur_path.push_back(this_sn);
	if(new_num >= (unsigned)(core_num - 1))
	{
		// this path terminates
		if(new_wgt < best_wgt)
		{
			best_wgt = new_wgt;
			best_path = cur_path;
		}
		cur_path.pop_back();
		return;
	}
	// this path should continue extending
	unsigned num_parent = this_node->parent_num();
	for(unsigned i = 0; i < num_parent; i++)
	{
		find_shortest(this_node->get_parent(i), cur_path, best_path, new_wgt, best_wgt, new_num);
	}
	cur_path.pop_back();
	return;
}