# IDA Swarm - Democratizing Reverse Engineering Through AI

## Revolutionary Mission

**IDA Swarm** fundamentally transforms reverse engineering from an esoteric skill into an accessible capability through AI-powered multi-agent collaboration. This project empowers individuals to understand, analyze, and modify software they own, challenging traditional vendor control and advancing consumer digital rights.

## Why This Matters

### The Problem
- Traditional reverse engineering requires years of specialized training
- Complex tools are expensive and difficult to master
- Understanding software internals remains inaccessible to most users

### Our Solution
- AI agents that guide users through complex analysis
- Works with IDA Pro 9.0 beta for research purposes
- Teaches users how to analyze and understand software
- Multi-agent collaboration makes expert-level analysis accessible

## Important Notice: Experimental Passion Project

> This is a **PASSION PROJECT**
>
> - **Expect bugs and instability**
> - **MCP server has known bugs**
> - **Designed around IDA Pro 9.0.240807 beta**
> - **IDAlib is broken** in this version, hence the spawned process architecture
> - **Active experimentation** - Code changes frequently as new ideas are tested

## Architecture Overview

### Primary Mode: Direct IDA Integration
```
        ┌──────────────────────────┐
        │     IDA Pro Instance     │
        │  ┌────────────────────┐  │
        │  │    Orchestrator    │  │
        │  │   (Qt UI Dialog)   │  │
        │  └──────────┬─────────┘  │
        └─────────────┼────────────┘
                      │ Process Spawn
     ┌────────────────┼────────────────┐
     ▼                ▼                ▼
┌─────────┐    ┌─────────┐    ┌─────────┐
│ Agent 1 │    │ Agent 2 │    │ Agent N │
│  (IDA)  │◄──►│  (IDA)  │◄──►│  (IDA)  │
└─────────┘    └─────────┘    └─────────┘
     ▲                ▲                ▲
     └────────────────┼────────────────┘
              IRC Communication
                  (Swarm)
```

### Optional: External Tool Integration via MCP
```
┌─────────────────────────────────────────────────────────────┐
│                     External Tools (Claude)                 │
└────────────────────┬────────────────────────────────────────┘
                     │ MCP Protocol (JSON-RPC)
        ┌────────────▼────────────┐
        │   MCP Server (Optional) │
        │  (Session Management)   │
        └────────────┬────────────┘
                     │ Named Pipes
        ┌────────────▼────────────┐
        │     Orchestrator        │
        └────────────┬────────────┘
                     │ Process Spawn
     ┌───────────────┼───────────────┐
     ▼               ▼               ▼
┌─────────┐    ┌─────────┐    ┌─────────┐
│ Agent 1 │    │ Agent 2 │    │ Agent N │
│  (IDA)  │◄──►│  (IDA)  │◄──►│  (IDA)  │
└─────────┘    └─────────┘    └─────────┘
     ▲               ▲               ▲
     └───────────────┼───────────────┘
              IRC Communication
                  (Swarm)
```

### Key Components

- **Orchestrator**: Central coordinator with two interface modes:
  - **Native mode** (default): Qt dialog interface directly within IDA Pro
  - **MCP mode** (optional): External control via MCP server, still spawns visible IDA instances
- **AI Agents**: Specialized IDA instances performing focused analysis tasks
- **IRC Server**: Inter-agent communication (WIP) and conflict resolution
- **MCP Server** (optional): Enables external tool integration (Claude, etc.)
- **Tool System**: Comprehensive IDA Pro operation framework
- **Patching Engine**: Multi-architecture binary modification with Keystone

## Prerequisites

### Required Software

1. **Designed around IDA Pro 9.0 Beta** (Version 9.0.240807)
   - IDAlib is broken in this version, necessitating our process-based approach

2. **Qt 5.15.2** (IDA's custom build)
   - Download sources from https://hex-rays.com/blog/ida-8-4-qt-5-15-2-sources-build-scripts
   - macOS: Extract to `/Users/Shared/Qt/5.15.2-arm64` (directory name must include -arm64 for Qt to build correctly!)
   - Linux: Same idea

3. **Build Tools**
   - CMake 3.16+
   - C++20 compliant compiler
   - Keystone assembler (built automatically)

4. **Claude API Access**
   - Anthropic API key required

## Installation

### 1. Clone Repository

```bash
git clone https://github.com/yourusername/ida_re_agent.git
cd ida_re_agent
```

### 2. Configure Build

Edit `CMakeLists.txt` to set your paths:

```cmake
# IDA SDK path
set(IDASDK_PATH "/path/to/idasdk90")

# IDA Pro plugins directory
set(IDA_PLUGINS_PATH "/path/to/ida/plugins")

# Qt installation (IDA's custom build)
set(QTDIR "/path/to/qt-5.15.2")
```

### 3. Build Project

```bash
mkdir build && cd build
cmake ..
make
make install  # Installs plugin to IDA plugins directory
```

### 4. Configure API Access

Copy and edit the configuration file:

```bash
cp llm_re_config.json.example ~/.idapro/llm_re_config.json
```

Edit with your API key:

```json
{
  "api": {
    "auth_method": "api_key",
    "api_key": "sk-ant-..."
  }
}
```

## Usage

### Direct IDA Pro Usage

1. Open your target binary in IDA
2. Wait for initial analysis to complete
3. Run the plugin: `Edit → Plugins → LLM RE Agent`
4. Enter your analysis request in the dialog

## Key Features

### Multi-Agent Collaboration
- Orchestrator spawns agents for different analysis aspects
- Agents work in parallel on isolated database copies
- IRC-based communication enables real-time collaboration (WIP)
- Automatic conflict resolution through consensus mechanisms

### Binary Patching
- Assembly and byte-level patching
- Multi-architecture support (Keystone)
- Individual patch generation

### Comprehensive Analysis Tools
- Function analysis and cross-referencing
- Data structure identification
- String and constant analysis
- Control flow understanding
- Decompilation integration

### Conflict Resolution
When agents disagree on modifications:
1. Dedicated IRC channel created (`#conflict_address_tool`)
2. Agents debate and provide reasoning
3. Consensus must be reached by all parties
4. Orchestrator enforces agreed solution

## Project Philosophy

### Democratizing Reverse Engineering

This project makes reverse engineering accessible by:
- **Removing skill barriers**: AI performs complex analysis
- **Eliminating tool barriers**: Works with IDA beta 9.0
- 
### Consumer Rights Philosophy

We believe users should be able to:
- **Understand** software behavior through analysis
- **Modify** software for personal needs and preferences
- **Fix** bugs and security vulnerabilities
- **Remove** unwanted features or restrictions
- **Learn** from the software they use

## Known Issues & Limitations

### Current Beta Issues

1. **MCP Server is Buggy**

2. **IDA Integration**
   - IDAlib broken in 9.0.240807, which is why I spawn processes which is not ideal

3. **MCP launchd**
   - I was encountering weird issues with trying to launch ida64 from the terminal on my mac (used to work, but something broke), and using launchd was the only workaround that worked for me

### Workarounds

- **Session hangs**: Restart MCP server and close IDA instances
- **Agent failures**: Orchestrator will detect and report, continue with remaining agents
- **IRC issues**: Check `/tmp/ida_swarm_workspace/` for debug logs

## Technical Deep Dive

### Why Spawned Processes?

IDA Pro 9.0.240807 beta has a broken IDAlib implementation, and since I wanted this project to be able to work on the IDA beta I had to design around it. This is not ideal, and idalib would be perfect for this tool

### Communication Architecture

1. **MCP → Orchestrator**: Named pipes (JSON-RPC protocol)
2. **Orchestrator → Agents**: Process spawning with configuration
3. **Agent ↔ Agent**: IRC protocol for real-time collaboration
4. **Agent → IDA**: Direct API calls within process

## Legal & Ethical Considerations

### Research & Educational Purpose

This tool is intended for:
- Security research on software you own
- Understanding software behavior for compatibility
- Educational purposes in reverse engineering
- Personal modification of legally owned software

### NOT Intended For:
- Circumventing protections on software you don't own
- Commercial piracy or license violations
- Malicious modification or malware creation
- Distribution of circumvention tools

### Your Responsibility

Users are responsible for ensuring their use complies with:
- Local laws and regulations
- Software license agreements where applicable
- Ethical guidelines for security research

---

## Disclaimer

This software is provided as-is for research and educational purposes. The authors assume no liability for its use. Users must comply with all applicable laws and regulations. This project does not encourage or condone software piracy or license violations.

**Remember**: With great power comes great responsibility. Use this tool to expand knowledge, improve security, and advance software freedom—not to harm others or violate rights.
