#ifndef _CONTEXT_MANAGER_H_
#define _CONTEXT_MANAGER_H_

#include "core.hpp"

class contextManager
{
protected:
    context * s_ctx;
    std::vector<context*> q_ctx;
public:
    contextManager();
    ~contextManager();
    void init_q_ctx(int length);
    void mk_q_ctx(int index, config & c);
    void mk_s_ctx(config & c);
    context & get_q_ctx(int index);
    context & get_s_ctx();
};

#endif
