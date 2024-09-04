#include "generate_traces.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <cstring>
#include <iostream>
//#include <city.h>
namespace anchor_tr_ {
        namespace {
                std::vector<std::string> split(std::string const & s, char delimiter) {
                        std::vector<std::string> tokens;
                        std::string token;
                        std::istringstream tokenStream(s);
                        while (std::getline(tokenStream, token, delimiter)) {
                                tokens.push_back(token);
                        }
                        return tokens;
                }
                std::vector<Trace_cmd> parse_trace(uint16_t, std::string path, int read_permille) {
                        std::ifstream file(path);
                        if (!file) {
                                return {};
                        }
                        std::vector<Trace_cmd> res;
                        res.reserve(10000000);
                        std::string line;
                        while (std::getline(file, line)) {
                                if (line.length() == 0) continue;
                                if (line[line.length() - 1] == '\n') {
                                        line[line.length() - 1] = 0;
                                }
                                auto elements = split(line, ' ');
                                if (elements.size() < 1) {
                                        continue;
                                }
                                res.emplace_back(elements[0], read_permille);
                        }
                        return res;
                }
                std::vector<Trace_cmd> parse_trace(uint16_t, std::string path) {
                        std::ifstream file(path);
                        if (!file) {
                                std::cout << "Workload file not found : " << path << "\n";
                                return {};
                        }
                        std::vector<Trace_cmd> res;
                        res.reserve(10000000);
                        std::string line;
                        while (std::getline(file, line)) {
                                if (line.length() == 0) continue;
                                if (line[line.length() - 1] == '\n') {
                                        line[line.length() - 1] = 0;
                                }
                                auto elements = split(line, ' ');
                                if (elements.size() < 2) {
                                        continue;
                                }
                                                                
                                res.emplace_back(elements[1], elements[0]);
                        }
                        return res;
                }
                std::vector<Trace_cmd> manufacture_trace(uint16_t, size_t Trace_size, size_t N_keys, int read_permille) {
                        std::vector<Trace_cmd> res;
                        res.reserve(Trace_size);
                        for (auto i = 0ULL; i < Trace_size; ++i) {
                              res.emplace_back(static_cast<uint32_t>(rand() % N_keys), read_permille);
                        }
                        return res;
                }
        } //namespace
        void Trace_cmd::init(uint32_t key_id, int read_permille) {
                op = (rand() % 1000) < read_permille ? Get : Put;
                key_hash = (uint64_t)key_id;
        }
        void Trace_cmd::init(uint32_t key_id, const char* op_trace) {
                if (strcmp(op_trace, "W") == 0) {
                        op = Put;
                }
                else if (strcmp(op_trace, "R") == 0) {
                        op = Get;
                }
                key_hash = (uint64_t)key_id;
        }
        Trace_cmd::Trace_cmd(uint32_t key_id, int read_permille) {
                init(key_id, read_permille);
        }
        Trace_cmd::Trace_cmd(std::string const & s, int read_permille) {
                init(static_cast<uint32_t>(strtoul(s.c_str(), nullptr, 10)), read_permille);
        }
        Trace_cmd::Trace_cmd(std::string const & s, std::string const & op) {
                init(static_cast<uint32_t>(strtoul(s.c_str(), nullptr, 10)), op.c_str());
        }
        std::vector<Trace_cmd> trace_init(uint16_t t_id, std::string path) {
                return parse_trace(t_id, path);
        }
        std::vector<Trace_cmd> trace_init(std::string file_path, int read_permile) {
                return parse_trace(0, file_path, read_permile);
        }
        std::vector<Trace_cmd> trace_init(std::string file_path) {
                return parse_trace(0, file_path);
        }
        std::vector<Trace_cmd> trace_init(uint16_t t_id, size_t Trace_size, size_t N_keys, int read_permille, int rand_start) {
                srand(rand_start);
                return manufacture_trace(t_id, Trace_size, N_keys, read_permille);
        }
} //namespace anchor_tr_