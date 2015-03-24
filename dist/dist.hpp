#ifndef _DIST_H_
#define _DIST_H_

#include "../core.hpp"

class simple_node;
class node;

void dist_clause(std::set<unsigned> &symbol_set, std::vector<std::set<unsigned> > &symbol_sub, std::vector<int> &clause_weight);

class simple_node
{
protected:
	boost::shared_ptr<std::vector<bool> > symbol_status;
	unsigned symbol_num;

public:
	simple_node()
	{
		symbol_status = boost::shared_ptr<std::vector<bool> >(new std::vector<bool>);
		symbol_num = 0;
	}

	void set_symbol(std::set<unsigned> & total_set, std::set<unsigned> & sub_set)
	{
		std::set<unsigned>::iterator it1 = total_set.begin();
		std::set<unsigned>::iterator it1_end = total_set.end();
		std::set<unsigned>::iterator it2 = sub_set.begin();
		std::set<unsigned>::iterator it2_end = sub_set.end();

		while(it2 != it2_end)
		{
			if(*it1 == *it2)
			{
				symbol_status->push_back(true);
				symbol_num++;
				it1++;
				it2++;
			}
			else if(*it1 < *it2)
			{
				symbol_status->push_back(false);
				it1++;
			}
			else
			{
				std::cerr << "illegal case" << std::endl;
				std::cerr << "it1: " << *it1 << ";" << "it2: " << *it2 << std::endl;
				exit(1);
			}
		}
		// fill 'false' in remaining positions
		while(it1 != it1_end)
		{
			symbol_status->push_back(false);
			it1++;
		}

		assert(symbol_status->size() == total_set.size());
	}

	unsigned symbol_size()
	{
		return symbol_status->size();
	}

	unsigned truebit_num()
	{
		return symbol_num;
	}

	bool get_status(unsigned idx)
	{
		if(idx < symbol_status->size())
			return symbol_status->at(idx);
		else
		{
			std::cerr << "illegal parameter" << std::endl;
			return false;
		}
	}

	bool operator[](unsigned idx)
	{
		if(idx < symbol_status->size())
			return symbol_status->at(idx);
		else
		{
			std::cerr << "illegal parameter" << std::endl;
			return false;
		}
	}

	void push(bool value)
	{
		symbol_status->push_back(value);
		if(value)
			symbol_num++;
	}

	friend bool operator<(const simple_node &lhs, const simple_node &rhs)
	{
		if (lhs.symbol_num < rhs.symbol_num) return true;
		if (lhs.symbol_num > rhs.symbol_num) return false;
		unsigned len = lhs.symbol_status->size();
		for(unsigned i = 0; i < len; i++)
		{
			if(!lhs.symbol_status->at(i) && rhs.symbol_status->at(i)) return true;
			if(lhs.symbol_status->at(i) && !rhs.symbol_status->at(i)) return false;
		}
		return false; // If they are all the same
	}

	friend bool operator==(const simple_node &lhs, const simple_node& rhs)
	{
		if (lhs.symbol_num == rhs.symbol_num)
		{
			unsigned len = lhs.symbol_status->size();
			for(unsigned i = 0; i < len; i++)
			{
				if(lhs.symbol_status->at(i) != rhs.symbol_status->at(i))
					return false;
			}
			return true;
		}
		return false;
	}

	friend bool operator>(const simple_node &lhs, const simple_node &rhs)
	{
		if(lhs < rhs || lhs == rhs)
			return false;
		return true;
	}

	simple_node intersect(simple_node * hs)
	{
		unsigned len = symbol_status->size();
		assert(len == hs->symbol_size());
		simple_node result;
		for(unsigned i = 0; i < len; i++)
		{
			if(symbol_status->at(i) && hs->get_status(i))
				result.push(true);
			else
				result.push(false);
		}
		return result;
	}

	bool is_subset(simple_node * hs)
	{
		unsigned len = symbol_status->size();
		assert(len == hs->symbol_size());
		if(symbol_num >= hs->truebit_num())
			return false;
		for(unsigned i = 0; i < len; i++)
		{
			if(symbol_status->at(i) && !hs->get_status(i))
				return false;
		}
		return true;
	}

};

class node : public simple_node
{
protected:
	boost::shared_ptr<std::vector<node*> > before;
	boost::shared_ptr<std::vector<node*> > after;
	unsigned weight;
	unsigned cls_num;

public:
	node():simple_node()
	{
		before = boost::shared_ptr<std::vector<node*> >(new std::vector<node*>);
		after = boost::shared_ptr<std::vector<node*> >(new std::vector<node*>);
		weight = 0;
		cls_num = 0;
	}

	node(simple_node & sn):simple_node(sn)
	{
		before = boost::shared_ptr<std::vector<node*> >(new std::vector<node*>);
		after = boost::shared_ptr<std::vector<node*> >(new std::vector<node*>);
		weight = 0;
		cls_num = 0;
	}

	void set_weight(unsigned wgt)
	{
		weight = wgt;
	}

	void set_clause_num(unsigned num)
	{
		cls_num = num;
	}

	unsigned get_weight()
	{
		return weight;
	}

	unsigned clause_num()
	{
		return cls_num;
	}

	bool have_child()
	{
		if(before->size() == 0)
			return false;
		return true;
	}

	bool have_parent()
	{
		if(after->size() == 0)
			return false;
		return true;
	}

	void add_child(node * nd)
	{
		before->push_back(nd);
	}

	void add_parent(node * nd)
	{
		after->push_back(nd);
	}

	unsigned child_num()
	{
		return before->size();
	}

	unsigned parent_num()
	{
		return after->size();
	}

	node * get_child(unsigned idx)
	{
		return before->at(idx);
	}

	node * get_parent(unsigned idx)
	{
		return after->at(idx);
	}

};


// prepare searching from a top node
bool top_search(node * new_nd, node * top_nd);

// search from a node
void sub_search(simple_node * insc, node * new_nd, node * sub_nd);

// find shortest path to construct a division
void find_shortest(node * this_node, std::vector<simple_node> & cur_path, std::vector<simple_node> & best_path, unsigned cur_wgt, unsigned & best_wgt, unsigned cur_num);

#endif
