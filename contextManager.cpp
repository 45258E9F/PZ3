#include "contextManager.hpp"

contextManager::contextManager()
{
    s_ctx = NULL;
    q_ctx.clear();
}

contextManager::~contextManager()
{
    delete s_ctx;
    int len = q_ctx.size();
    if(len != 0)
    {
	for(int index = 0; index < len; index++)
	    delete q_ctx.at(index);
    }
}

void contextManager::init_q_ctx(int length)
{
    int len = q_ctx.size();
    if(len != 0)
    {
	std::cerr << "Double initialization of queue.\n";
	exit(1);
    }
    q_ctx = std::vector<context*>(length, NULL);
}

void contextManager::mk_q_ctx(int index, config & c)
{
    int length = q_ctx.size();
    if((index >= length) || (index < 0))
    {
	std::cerr << "Inaccessible context element.\n";
	exit(1);
    }
    if(q_ctx.at(index) != NULL)
	delete q_ctx.at(index);
    q_ctx.at(index) = new context(c);
}

void contextManager::mk_s_ctx(config & c)
{
    if(s_ctx != NULL)
	delete s_ctx;
    s_ctx = new context(c);
}

context & contextManager::get_q_ctx(int index)
{
    int length = q_ctx.size();
    if((index >= length) || (index < 0))
    {
	std::cerr << "Inaccessible context element.\n";
	exit(1);
    }
    return *q_ctx.at(index);
}

context & contextManager::get_s_ctx()
{
    return *s_ctx;
}
