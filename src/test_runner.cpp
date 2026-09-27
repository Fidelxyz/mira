#include "test_runner.h"

#include <iostream>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "deployment.h"
#include "output_collector.h"

using namespace std;

static void lua_setresult(lua_State* lua, Result& result);
static bool lua_eval(lua_State* lua, const char* expression);

TestRunner::TestRunner(const YAML::Node& document)
    : lua(luaL_newstate())
{
    if (!lua) {
        cerr << "cannot create Lua state\n";
        return;
    }
    luaL_openlibs(lua);
    if (document["script"] &&
        luaL_dostring(lua, document["script"].as<string>().c_str()) != LUA_OK) {
        cerr << "Error evaluating Lua: " << lua_tostring(lua, -1) << endl;
        lua_pop(lua, 1);
        return;
    }
    initialized = true;
}

TestRunner::~TestRunner()
{
    if (lua) {
        lua_close(lua);
    }
}

bool
TestRunner::ready() const
{
    return initialized;
}

bool
TestRunner::run_deployment(Rime& rime,
                           const string& schema_id,
                           const string& deployment_name,
                           const YAML::Node& deployment)
{
    bool completed = true;
    for (const auto& test : deployment["tests"]) {
        auto keys = test["send"].as<string>();
        auto expression = "return ("s + test["assert"].as<string>() + ")";
        string test_id = schema_id + "::" + deployment_name + "::" + keys;
        cout << "- " << test_id << "... " << flush;
        string stdout_text, stderr_text;
        bool pass = false;
        {
            OutputCollector collector(stdout_text, stderr_text);
            auto session = prepare_session(rime, schema_id, deployment["options"]);
            if (session) {
                auto result = session->send_keys(keys);
                if (result) {
                    lua_setresult(lua, *result);
                    pass = lua_eval(lua, expression.c_str());
                } else {
                    completed = false;
                }
            } else {
                completed = false;
            }
        }

        cout << (pass ? "PASS" : "FAIL") << endl;
        if (!pass) {
            if (!stdout_text.empty()) {
                cout << "\n========= STDOUT =========\n" << stdout_text << "\n" << endl;
            }
            if (!stderr_text.empty()) {
                cout << "\n========= STDERR =========\n" << stderr_text << "\n" << endl;
            }
        }
        (pass ? passlist : faillist).push_back(test_id);
    }
    return completed;
}

int
TestRunner::finish() const
{
    const size_t failed = faillist.size();
    const size_t passed = passlist.size();
    if (failed) {
        cout << "\n\n" << failed << "/" << (failed + passed)
             << " tests failed:\n";
        for (const auto& id : faillist) {
            cout << id << "\n";
        }
        return failed < 126 ? static_cast<int>(failed) : 126;
    }
    cout << "Eveything OK!\n";
    return 0;
}

static void
lua_setresult(lua_State* lua, Result& result)
{
    if (result.commit) {
        lua_pushstring(lua, result.commit->c_str());
    } else {
        lua_pushnil(lua);
    }
    lua_setglobal(lua, "commit");

    const auto& candidates = result.candidates;
    lua_newtable(lua);
    for (size_t i = 0; i < candidates.size(); ++i) {
        lua_newtable(lua);
        lua_pushstring(lua, "text");
        lua_pushstring(lua, candidates[i].text.c_str());
        lua_settable(lua, -3);
        lua_pushstring(lua, "comment");
        lua_pushstring(lua, candidates[i].comment.value_or("").c_str());
        lua_settable(lua, -3);
        lua_rawseti(lua, -2, i + 1);
    }
    lua_setglobal(lua, "cand");

    if (result.preedit) {
        lua_pushstring(lua, result.preedit->c_str());
    } else {
        lua_pushnil(lua);
    }
    lua_setglobal(lua, "preedit");
}

static bool
lua_eval(lua_State* lua, const char* expression)
{
    if (luaL_dostring(lua, expression) != LUA_OK) {
        cerr << "Error evaluating Lua: " << lua_tostring(lua, -1) << endl;
        lua_pop(lua, 1);
        return false;
    }
    bool pass = lua_toboolean(lua, -1);
    lua_pop(lua, 1);
    return pass;
}
