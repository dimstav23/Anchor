#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include <string>
namespace anchor_tr_ {
        struct Trace_cmd {
                enum operation{
                        Put,
                        Get
                } op;
                static constexpr size_t key_size = 8;
                std::function<void(Trace_cmd & cmd)> call_;
                //uint8_t key_hash[key_size];
                uint64_t key_hash;
                void call() { call_(*this); }
                explicit Trace_cmd(uint32_t key_id, int read_permille = 500);
                explicit Trace_cmd(std::string const & s, int read_permille);
                explicit Trace_cmd(std::string const & s, std::string const & op);
                ~Trace_cmd() { };
                private:
                void init(uint32_t key_id, int read_permille);
                void init(uint32_t key_id, const char* op_trace);
        };
        std::vector<Trace_cmd> trace_init(uint16_t t_id, size_t Trace_size, size_t N_keys, int read_permille = 500, int rand_start = 0);
        std::vector<Trace_cmd> trace_init(uint16_t t_id, std::string path);
        std::vector<Trace_cmd> trace_init(std::string path, int read_permille);
        std::vector<Trace_cmd> trace_init(std::string path);
} //anchor_tr_