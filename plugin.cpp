#include <iostream>

#include <pro.h>
#include <prodir.h>
#include <ida.hpp>
#include <auto.hpp>
#include <expr.hpp>
#include <name.hpp>
#include <undo.hpp>
#include <name.hpp>
#include <diskio.hpp>
#include <loader.hpp>
#include <dirtree.hpp>
#include <kernwin.hpp>
#include <segment.hpp>
#include <parsejson.hpp>


// Define the class that inherits from plugmod_t
class MyPlugmod : public plugmod_t
{
public:
    // Constructor
    MyPlugmod()
    {
        msg("MyPlugmod: Constructor called.\n");
    }

    // Destructor
    virtual ~MyPlugmod()
    {
        msg("MyPlugmod: Destructor called.\n");
    }

    // Method that gets called when the plugin is activated
    virtual bool idaapi run(size_t arg) override
    {
        msg("MyPlugmod.run() called with arg: %zu\n", arg);

        // Add some actual functionality to test
        msg("Plugin is working! Current database: %s\n", get_path(PATH_TYPE_IDB));

        return true;
    }
};

static plugmod_t* idaapi init(void)
{
    msg("Plugin init() called\n");
    return new MyPlugmod();
}

static void idaapi term(void)
{
    msg("Plugin term() called\n");
}

static bool idaapi run(size_t arg)
{
    msg("Plugin run() called with arg: %zu\n", arg);
    return false; // This shouldn't be called with PLUGIN_MULTI
}

plugin_t PLUGIN = {
    IDP_INTERFACE_VERSION,
    PLUGIN_FIX,         // plugin flags
    init,                 // initialize
    term,                 // terminate. this pointer can be nullptr
    run,                  // invoke the plugin
    "List functions plugin", // long comment about the plugin
    "This plugin demonstrates basic functionality", // multiline help about the plugin
    "List functions",     // the preferred short name of the plugin
    "Ctrl-Shift-L"        // the preferred hotkey to run the plugin
};