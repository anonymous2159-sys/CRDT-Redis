//
// Created by admin on 2020/1/6.
//

#ifndef BENCH_UTIL_H
#define BENCH_UTIL_H

#include <sys/stat.h>

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "exp_setting.h"

#if defined(__linux__)
#include <hiredis/hiredis.h>
#elif defined(_WIN32)

#include <direct.h>

#include "../../redis-6.0.5/deps/hiredis/hiredis.h"

#endif

#define MAX_TIME_COLISION exp_setting::delay
constexpr int SPLIT_NUM = 10;
#define SLP_TIME_MICRO (MAX_TIME_COLISION * 1000 / SPLIT_NUM)

using namespace std;

int intRand(int min, int max);

static inline int intRand(int max) { return intRand(0, max - 1); }

static inline bool boolRand() { return intRand(0, 1); }

string strRand(int max_len = 16);

double doubleRand(double min, double max);

static inline double decide() { return doubleRand(0.0, 1.0); }

using redisReply_ptr = unique_ptr<redisReply, decltype(freeReplyObject) *>;

class cmd;

class redis_client
{
private:
    const char *ip;
    const int port;
    redisContext *c = nullptr;

    mutex m;
    condition_variable cv;
    list<cmd *> cmds;
    volatile bool run = false;
    thread pipeline;

    void connect()
    {
        if (c != nullptr) redisFree(c);
        c = redisConnect(ip, port);
        if (c == nullptr || c->err)
        {
            if (c)
            {
                cout << "\nError for redisConnect: " << c->errstr << ", ip:" << ip
                     << ", port:" << port << endl;
                redisFree(c);
                c = nullptr;
            }
            else
                cout << "\nCan't allocate redis context" << endl;
            exit(-1);
        }
    }

    void reply_error(const string &cmd);

    redisReply_ptr exec();

public:
    redis_client(const char *ip, int port) : ip(ip), port(port) { connect(); }

    void add_pipeline_cmd(cmd *command);

    redisReply_ptr exec(const string &cmd)
    {
        if (run)
        {
            cout << "\nYou cannot use pipeline cmd and exec cmd at the same redis_client." << endl;
            exit(-1);
        }
        bool retryed = false;
        auto r = static_cast<redisReply *>(redisCommand(c, cmd.c_str()));
        while (r == nullptr)
        {
            if (!retryed)
            {
                connect();
                retryed = true;
                r = static_cast<redisReply *>(redisCommand(c, cmd.c_str()));
                continue;
            }
            reply_error(cmd);
        }
        return redisReply_ptr(r, freeReplyObject);
    }

    redisReply_ptr exec(const cmd &cmd);

    ~redis_client()
    {
        if (run)
        {
            run = false;
            cv.notify_all();
            if (pipeline.joinable()) pipeline.join();
        }
        if (c != nullptr) redisFree(c);
    }
};

class cmd
{
protected:
    ostringstream stream;

    cmd() = default;

    template <typename T>
    inline void add_args(const T &arg)
    {
        stream << " " << arg;
    }

    template <typename T, typename... Types>
    inline void add_args(const T &arg, const Types &... args)
    {
        stream << " " << arg;
        add_args(args...);
    }

    friend class redis_client;

public:
    virtual void handle_redis_return(const redisReply_ptr &r) = 0;

    void exec(redis_client &c)
    {
        auto r = c.exec(stream.str());
        if (r->type != REDIS_REPLY_ERROR) handle_redis_return(r);
    }
};

class generator
{
private:
    class c_record
    {
    public:
        virtual void inc_rem() = 0;
    };

protected:
    template <class T>
    class record_for_collision : public c_record
    {
    private:
        vector<T> v[SPLIT_NUM];
        int cur = 0;
        unordered_set<T> h;
        mutex mtx;

    public:
        void add(T &name)
        {
            lock_guard<mutex> lk(mtx);
            if (h.find(name) == h.end())
            {
                h.emplace(name);
                v[cur].emplace_back(name);
            }
        }

        T get(T &&fail)
        {
            lock_guard<mutex> lk(mtx);
            if (h.empty()) return fail;
            int b = (cur + intRand(SPLIT_NUM)) % SPLIT_NUM;
            while (v[b].empty())
                b = (b + 1) % SPLIT_NUM;
            return v[b][intRand(static_cast<const int>(v[b].size()))];
        }

        void inc_rem() override
        {
            lock_guard<mutex> lk(mtx);
            cur = (cur + 1) % SPLIT_NUM;
            for (auto &n : v[cur])
                h.erase(n);
            v[cur].clear();
        }
    };

private:
    vector<c_record *> records;
    thread maintainer;
    volatile bool running = true;

protected:
    void add_record(c_record &r) { records.emplace_back(&r); }

    void start_maintaining_records()
    {
        maintainer = thread([this] {
            while (running)
            {
                this_thread::sleep_for(chrono::microseconds(SLP_TIME_MICRO));
                for (auto &r : records)
                    r->inc_rem();
            }
        });
    }

    ~generator()
    {
        running = false;
        if (maintainer.joinable()) maintainer.join();
    }

public:
    virtual void gen_and_exec(redis_client &c) = 0;
};

class rdt_log
{
private:
    string dir;

    static inline void bench_mkdir(const string &path)
    {
#if defined(_WIN32)
        _mkdir(path.c_str());
#else
        mkdir(path.c_str(), S_IRWXU | S_IRGRP | S_IROTH);
#endif
    }

    template <size_t I = 0, typename... Tp>
    static inline typename enable_if<I == sizeof...(Tp) - 1, void>::type write_tuple(
        ofstream &ofs, tuple<Tp...> &t)
    {
        ofs << get<I>(t) << "\n";
    }

    template <size_t I = 0, typename... Tp>
    static inline typename enable_if<I < sizeof...(Tp) - 1, void>::type write_tuple(
        ofstream &ofs, tuple<Tp...> &t)
    {
        ofs << get<I>(t) << ",";
        write_tuple<I + 1, Tp...>(ofs, t);
    }

protected:
    rdt_log(const char *CRDT_name, const string &type)
    {
        ostringstream stream;
        stream << "../result/" << CRDT_name << (exp_setting::compare ? ",cmp" : "");
        bench_mkdir(stream.str());

        if (exp_setting::type == exp_setting::exp_type::pattern)
            stream << "/" << exp_setting::pattern_name;
        else
            stream << "/" << exp_setting::type_str[static_cast<int>(exp_setting::type)];
        bench_mkdir(stream.str());

        stream << "/" << exp_setting::round_num;
        bench_mkdir(stream.str());

        stream << "/" << type << "_" << TOTAL_SERVERS << "," << exp_setting::op_per_sec << ",("
               << exp_setting::delay << "," << exp_setting::delay_low << ")";
        dir = stream.str();
        bench_mkdir(dir);
    }

public:
    atomic<int> write_op_generated{0};
    volatile int write_op_executed = 0;

    virtual void write_logfiles() = 0;
    virtual void log_compare(redisReply_ptr &r1, redisReply_ptr &r2) = 0;

    template <typename... Tp>
    void write_one_logfile(const char *filename, list<tuple<Tp...>> &log)
    {
        ostringstream stream;
        stream << dir << "/" << filename;
        ofstream fout(stream.str(), ios::out | ios::trunc);
        for (auto &o : log)
            write_tuple(fout, o);
    }
};

class rdt_exp
{
private:
    const char *rdt_type;
    exp_setting::default_setting &rdt_exp_setting;

    class exp_setter
    {
    public:
        exp_setter(rdt_exp &r, const string &type)
        {
            exp_setting::set_exp_subject(type.c_str(), r.rdt_type);
            exp_setting::set_default(&r.rdt_exp_setting);
        }

        ~exp_setter()
        {
            exp_setting::set_default(nullptr);
            exp_setting::set_exp_subject(nullptr, nullptr);
        }
    };

protected:
    vector<string> rdt_types;
    vector<string> rdt_patterns;

    void add_type(const char *type) { rdt_types.emplace_back(type); }

    void add_pattern(const char *pattern) { rdt_patterns.emplace_back(pattern); }

    explicit rdt_exp(exp_setting::default_setting &rdt_st, const char *rdt_type)
        : rdt_exp_setting(rdt_st), rdt_type(rdt_type)
    {}

    virtual void exp_impl(const string &type, const string &pattern) = 0;

    inline void exp_impl(const string &type) { exp_impl(type, "default"); }

public:
    void test_patterns()
    {
        for (auto &p : rdt_patterns)
            for (auto &t : rdt_types)
                pattern_fix(p, t);
    }

    void test_default_settings()
    {
        for (auto &t : rdt_types)
            pattern_fix("default", t);
    }

    void test_delay(int round)
    {
        for (int delay = rdt_exp_setting.delay_e.start; delay <= rdt_exp_setting.delay_e.end;
             delay += rdt_exp_setting.delay_e.step)
            for (auto &type : rdt_types)
                delay_fix(delay, round, type);
    }

    void test_replica(int round)
    {
        for (int replica = rdt_exp_setting.replica_e.start;
             replica <= rdt_exp_setting.replica_e.end; replica += rdt_exp_setting.replica_e.step)
            for (auto &type : rdt_types)
                replica_fix(replica, round, type);
    }

    void test_speed(int round)
    {
        for (int speed = rdt_exp_setting.speed_e.start; speed <= rdt_exp_setting.speed_e.end;
             speed += rdt_exp_setting.speed_e.step)
            for (auto &type : rdt_types)
                speed_fix(speed, round, type);
    }
    void delay_fix(int delay, int round, const string &type)
    {
        exp_setter s(*this, type);
        exp_setting::set_delay(round, delay, delay / 5);
        exp_impl(type);
    }

    void replica_fix(int s_p_c, int round, const string &type)
    {
        exp_setter s(*this, type);
        exp_setting::set_replica(round, 3, s_p_c);
        exp_impl(type);
    }

    void speed_fix(int speed, int round, const string &type)
    {
        exp_setter s(*this, type);
        exp_setting::set_speed(round, speed);
        exp_impl(type);
    }

    void pattern_fix(const string &pattern, const string &type)
    {
        exp_setter s(*this, type);
        exp_setting::set_pattern(pattern);
        exp_impl(type, pattern);
    }

    void exp_start_all(int rounds)
    {
        auto start = chrono::steady_clock::now();

        test_patterns();

        for (int i = 0; i < rounds; i++)
        {
            test_delay(i);
            test_replica(i);
            test_speed(i);
        }

        auto end = chrono::steady_clock::now();
        auto time = chrono::duration_cast<chrono::duration<double>>(end - start).count();
        cout << "total time: " << time << " seconds" << endl;
    }
};

#endif  // BENCH_UTIL_H
