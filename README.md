# ![IDA Swarm](ida_swarm.png) IDA Swarm - Democratizing Reverse Engineering Through AI

## Revolutionary Mission

**IDA Swarm** fundamentally transforms reverse engineering from an esoteric skill into an accessible capability through AI-powered multi-agent collaboration. This project empowers individuals to understand, analyze, and study software, advancing software transparency and consumer understanding of digital systems

## Important Context

IDA Swarm is a research and educational tool that extends IDA Pro's capabilities through AI automation. Like IDA Pro itself, hex editors, and debuggers, this tool has legitimate uses in security research, education, and software analysis. While IDA Pro has a cost barrier, IDA Swarm democratizes the expertise required to use these tools effectively.

## Demo

[![IDA Swarm Demo Video](https://img.youtube.com/vi/6F0HhH0ZPXI/maxresdefault.jpg)](https://www.youtube.com/watch?v=6F0HhH0ZPXI)

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

## Important Notice: Passion Project

> This is a **PASSION PROJECT**
>
> - **Expect bugs and instability**
> - **Designed around IDA Pro 9.0.240807 beta**
> - **Process architecture due to IDAlib limitations in this version**
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
- **AI Agents**: Specialized IDA instances performing focused analysis tasks independently
- **IRC Server**: Patch replication and conflict resolution (agents work independently, IRC only for conflicts)
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
git clone https://github.com/shells-above/ida-swarm.git
cd ida-swarm
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
3. Run the plugin: `Edit → Plugins → IDA Swarm`
4. Enter your analysis request in the dialog

## Key Features

### Multi-Agent Parallel Analysis
- Orchestrator spawns agents for different analysis aspects
- **Agents work independently** in parallel on isolated database copies
- No direct inter-agent communication during analysis
- Automatic conflict resolution through IRC debate when patch conflicts occur

### Binary Patching System
- **Assembly and byte-level patching** with multi-architecture support via Keystone
- **Dual patching**: Synchronizes changes to both IDA database and actual binary file
- **Real-time patch replication**: All patches instantly broadcast to all agents
- **Conflict resolution**: When agents patch the same location differently, they must debate and reach consensus

### Code Injection System
- **Temporary workspace allocation**: Agents develop code in temporary IDA segments
- **No-go zones**: Prevents agents from allocating overlapping memory regions
- **Code cave detection**: Automatically finds and claims unused space in binaries
- **Relocation**: Code developed in temp segments is relocated to permanent locations

### Analysis Tools
- Function analysis and cross-referencing
- Data structure identification
- String and constant analysis
- Control flow understanding
- Decompilation integration

### The Design Tension: Independence vs Collaboration

We faced a fundamental design choice that affects the entire system:

#### Option 1: Complete Independence
- Each agent has its own isolated database
- Agents reach conclusions independently without bias
- Conflicts arise naturally when conclusions differ
- Never merge changes to main database
- **Pro**: No agent can bias another's analysis
- **Con**: No benefit from collaborative discovery

#### Option 2: Full Collaboration
- All write operations instantly replicated to all agents
- Agents work on the same database together
- **Pro**: Maximum information sharing and efficiency
- **Con**: Early decisions by one agent bias all others

#### Our Hybrid Approach
We implemented a complex (this may not be ideal, but it is what was made) middle ground:

1. **For Analysis**: Agents work independently, then merge results after completion
   - Preserves independent reasoning for reverse engineering tasks
   - Function names, comments, types can conflict and be resolved
   - Final merged result benefits all future agents

2. **For Patching**: Real-time replication with conflict resolution
   - Patches are instantly shared to keep binaries in sync
   - Prevents binary divergence that would be impossible to merge
   - But creates the "multiple truths" problem:
      - Agent A patches address X to disable a feature
      - Agent B patches address X to modify the feature
      - Unlike metadata conflicts, neither is "more correct"
   - Resolution requires negotiation in conflict channels

3. **For Code Injection**: Coordinated allocation with no-go zones
   - Temporary segments and code caves are immediately claimed
   - Prevents allocation conflicts while maintaining independence
   - Deterministic addressing ensures consistency

### The Unsolvable Problem

Unlike metadata conflicts where agents seek the "most accurate" answer, patching conflicts have no objective truth:
- Two agents can validly patch the same code for different purposes
- Both modifications might be "correct" for different goals
- The conflict channel becomes a negotiation rather than a search for truth

This is why patches are replicated immediately - to surface these conflicts early rather than discovering incompatible binary states during merge.

### Conflict Resolution
When agents disagree on modifications:
1. Dedicated IRC channel created (`#conflict_address_tool`)
2. Agents debate and provide reasoning
3. For metadata: Agents seek the most accurate representation
4. For patches: Agents must negotiate compatible modifications
5. Consensus must be reached by all parties
6. Orchestrator enforces agreed solution

## Project Philosophy

### Consumer Rights Philosophy

We believe users should be able to:
- **Understand** software behavior through analysis
- **Analyze** how software features and mechanisms work
- **Fix** bugs and security vulnerabilities
- **Research** software for interoperability and compatibility
- **Learn** from the software they use

All while respecting intellectual property rights and complying with applicable laws

## Known Issues & Limitations

### Current Beta Issues

1**IDA Integration**
- IDAlib is broken in 9.0.240807, which is why I spawn processes which is not ideal

2**MCP launchd**
- I was encountering weird issues with trying to launch ida64 from the terminal on my mac (used to work, but something broke), and using launchd was the only workaround that worked for me

### Workarounds

- **Session hangs**: Restart MCP server and close IDA instances
- **Agent failures**: Orchestrator will detect and report, continue with remaining agents
- **IRC issues**: Check `/tmp/ida_swarm_workspace/` for debug logs

## Technical Deep Dive

### Why Spawned Processes?

IDA Pro 9.0.240807 beta has issues with IDAlib (Hex-Rays' official automation library). Since IDAlib would be perfect for this tool but isn't working in this beta, I implemented process spawning as a workaround. This approach works reliably and has the side benefit of visual debugging through separate IDA windows.

### Communication Architecture

1. **MCP → Orchestrator**: Named pipes (JSON-RPC protocol)
2. **Orchestrator → Agents**: Process spawning with configuration
3. **Agent ↔ Agent**: IRC protocol for real-time collaboration (WIP)
4. **Agent → IDA**: Direct API calls within process

## Legitimate Use Cases

IDA Swarm is a research tool designed for:
- Security vulnerability research (DMCA § 1201(j))
- Software interoperability analysis (DMCA § 1201(f))
- Educational and academic research
- Understanding software behavior and functionality

Like IDA Pro, Ghidra, and other professional reverse engineering tools, IDA Swarm serves legitimate roles in cybersecurity research and software engineering.

## License

Open source for research and educational purposes. Users are responsible for compliance with applicable laws.
