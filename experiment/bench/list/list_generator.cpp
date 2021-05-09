//
// Created by admin on 2020/7/16.
//

#include "list_generator.h"

constexpr int MAX_FONT_SIZE = 100;
constexpr int TOTAL_FONT_TYPE = 10;
constexpr int MAX_COLOR = 1u << 25u;

#define PA (pattern.PR_ADD)
#define PU (pattern.PR_ADD + pattern.PR_UPD)

list_generator::list_op_gen_pattern &list_generator::get_pattern(const string &name)
{
    static map<string, list_op_gen_pattern> patterns{
        {"default", {.PR_ADD = 0.41, .PR_UPD = 0.2, .PR_REM = 0.39}},
        {"upddominant", {.PR_ADD = 0.11, .PR_UPD = 0.8, .PR_REM = 0.09}}};
    if (patterns.find(name) == patterns.end()) return patterns["default"];
    return patterns[name];
}

int list_generator::id_gen::index(thread::id tid)
{
    static int next_index = 0;
    static mutex my_mutex;
    static map<thread::id, int> ids;
    lock_guard<mutex> lock(my_mutex);
    if (ids.find(tid) == ids.end()) ids[tid] = next_index++;
    return ids[tid];
}

void list_generator::gen_and_exec(redis_client &c)
{
    double rand = decide();
    // TODO conflicts?
    if (rand <= PA) { c.add_pipeline_cmd(gen_insert()); }
    else if (rand <= PU)
    {
        string id = list.random_get();
        if (id.empty()) return c.add_pipeline_cmd(gen_insert());
        string upd_type;
        int value;
        switch (intRand(6))
        {
            case 0:
                upd_type = "font";
                value = intRand(TOTAL_FONT_TYPE);
                break;
            case 1:
                upd_type = "size";
                value = intRand(MAX_FONT_SIZE);
                break;
            case 2:
                upd_type = "color";
                value = intRand(MAX_COLOR);
                break;
            case 3:
                upd_type = "bold";
                value = boolRand();
                break;
            case 4:
                upd_type = "italic";
                value = boolRand();
                break;
            default:
                upd_type = "underline";
                value = boolRand();
                break;
        }
        c.add_pipeline_cmd(new list_update_cmd(type, list, id, upd_type, value));
    }
    else
    {
        string id = list.random_get();
        if (id.empty()) return c.add_pipeline_cmd(gen_insert());
        c.add_pipeline_cmd(new list_remove_cmd(type, list, id));
    }
}
list_insert_cmd *list_generator::gen_insert()
{
    if (exp_setting::compare && decide() < 0.5)
    {
        string id_r = list.random_get_removed();
        if (!id_r.empty())
        {
            string pre_t = "readd", content_t = "NA";
            return new list_insert_cmd(type, list, pre_t, id_r, content_t, 0, 0, 0, false, false,
                                       false);
        }
    }
    string prev = list.random_get(), id = new_id.get(), content = strRand();
    int font = intRand(TOTAL_FONT_TYPE), size = intRand(MAX_FONT_SIZE), color = intRand(MAX_COLOR);
    bool bold = boolRand(), italic = boolRand(), underline = boolRand();
    if (prev.empty()) prev = "null";
    return new list_insert_cmd(type, list, prev, id, content, font, size, color, bold, italic,
                               underline);
}
