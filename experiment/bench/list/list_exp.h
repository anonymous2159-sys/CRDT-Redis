//
// Created by admin on 2020/7/17.
//

#ifndef BENCH_LIST_EXP_H
#define BENCH_LIST_EXP_H

#include "../exp_setting.h"
#include "../util.h"

class list_exp : public rdt_exp
{
private:
    static exp_setting::default_setting list_setting;

    void exp_impl(const string& type, const string& pattern) override;

public:
    list_exp() : rdt_exp(list_setting, "list")
    {
        // ! List types: "r", "rwf"
        add_type("r");
        add_type("rwf");
        add_pattern("upddominant");
    }
};

#endif  // BENCH_LIST_EXP_H
