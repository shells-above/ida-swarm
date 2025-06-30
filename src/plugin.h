//
// Created by user on 6/29/25.
//

#ifndef PLUGIN_H
#define PLUGIN_H

#include <string>
#include <vector>
#include <ida.hpp>
#include <idp.hpp>
#include <kernwin.hpp>

// Forward declarations
namespace llm_re {
    class REAgent;
}

// UI Constants
extern const char* PLUGIN_NAME;
extern const char* PLUGIN_HOTKEY;
extern const char* LOG_VIEW_TITLE;
extern const char* LLM_MSG_VIEW_TITLE;

// Handler structures
struct log_handler_t : public action_handler_t {
    virtual int idaapi activate(action_activation_ctx_t*) override;
    virtual action_state_t idaapi update(action_update_ctx_t*) override;
};

struct llm_msg_handler_t : public action_handler_t {
    virtual int idaapi activate(action_activation_ctx_t*) override;
    virtual action_state_t idaapi update(action_update_ctx_t*) override;
};

struct resume_handler_t : public action_handler_t {
    virtual int idaapi activate(action_activation_ctx_t*) override;
    virtual action_state_t idaapi update(action_update_ctx_t*) override;
};

struct continue_handler_t : public action_handler_t {
    virtual int idaapi activate(action_activation_ctx_t*) override;
    virtual action_state_t idaapi update(action_update_ctx_t*) override;
};

// Utility functions
std::string get_timestamp();
void log_to_file_only(const std::string& text);
void append_to_log(const std::string& text);
void update_ui_state();

// Plugin functions
plugmod_t* idaapi init();
void idaapi term();
bool idaapi run(size_t arg);

// Plugin declaration
extern plugin_t PLUGIN;

#endif // PLUGIN_H
