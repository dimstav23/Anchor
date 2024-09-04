#include "generate_traces.h"
//#include <cstdlib>
//#include <fstream>
//#include <sstream>
//#include <cstring>
#include <iostream>

using namespace anchor_tr_;
int
main(int argc, char *argv[])
{
    std::vector<Trace_cmd> trace = trace_init("/home/dimstav23/Tools/trace-generator/traces/simple_trace_w_100000_k_10000_a_0.70.txt", 500);
    
    for (std::vector<Trace_cmd>::const_iterator i = trace.begin(); i != trace.end(); ++i) { 
        std::cout << i->op << ' ' << i->key_hash << '\n';
    }
    return 0;
}