#!/usr/bin/env python3
"""
Test script for IDA Swarm MCP Server
Tests the MCP protocol communication with the server
"""

import json
import subprocess
import time
import sys
import os
from datetime import datetime

# Test configuration
TEST_BINARY = "/Users/user/Documents/Escort-re/ESCORT Detector Tools.i64"
TEST_TASK = "__ENTER_ORCHESTRATOR_DEBUG_MODE__ does the function 'start' exist? this is literally all i need, just a boolean that is does or does not exist. i do NOT WANT ANY FURTHER ANALYSIS"

def log(message, level="INFO"):
    """Log a message with timestamp"""
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
    print(f"[{timestamp}] [{level}] {message}")
    sys.stdout.flush()

def send_mcp_request(proc, request):
    """Send a JSON-RPC request to the MCP server"""
    request_str = json.dumps(request) + "\n"
    log(f"Sending request: {json.dumps(request, indent=2)}", "DEBUG")

    try:
        proc.stdin.write(request_str.encode())
        proc.stdin.flush()
        log("Request sent successfully", "DEBUG")
        return True
    except Exception as e:
        log(f"Failed to send request: {e}", "ERROR")
        return False

def read_mcp_response(proc, timeout=300):
    """Read a JSON-RPC response from the MCP server"""
    log(f"Waiting for response (timeout: {timeout}s)...", "DEBUG")

    start_time = time.time()
    response_lines = []

    while True:
        if time.time() - start_time > timeout:
            log(f"Timeout waiting for response after {timeout} seconds", "ERROR")
            return None

        try:
            # Read line from stdout
            line = proc.stdout.readline()
            if not line:
                log("Server closed connection", "ERROR")
                return None

            line_str = line.decode().strip()
            if not line_str:
                continue

            # Try to parse as JSON
            try:
                response = json.loads(line_str)
                log(f"Received response: {json.dumps(response, indent=2)}", "DEBUG")
                return response
            except json.JSONDecodeError:
                # Not a complete JSON object yet, keep reading
                response_lines.append(line_str)
                continue

        except Exception as e:
            log(f"Error reading response: {e}", "ERROR")
            return None

def test_initialize(proc):
    """Test the initialize handshake"""
    log("Testing initialize handshake", "INFO")

    # Send initialize request
    init_request = {
        "jsonrpc": "2.0",
        "method": "initialize",
        "params": {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {
                "name": "test_client",
                "version": "1.0.0"
            }
        },
        "id": 1
    }

    if not send_mcp_request(proc, init_request):
        return False

    response = read_mcp_response(proc, timeout=10)
    if not response:
        log("Failed to get initialize response", "ERROR")
        return False

    if "error" in response:
        log(f"Initialize failed: {response['error']}", "ERROR")
        return False

    log("Initialize successful", "SUCCESS")

    # Send initialized notification
    initialized_notif = {
        "jsonrpc": "2.0",
        "method": "notifications/initialized"
    }
    send_mcp_request(proc, initialized_notif)

    return True

def test_list_tools(proc):
    """Test listing available tools"""
    log("Testing tools/list", "INFO")

    list_request = {
        "jsonrpc": "2.0",
        "method": "tools/list",
        "id": 2
    }

    if not send_mcp_request(proc, list_request):
        return False

    response = read_mcp_response(proc, timeout=10)
    if not response:
        log("Failed to get tools list", "ERROR")
        return False

    if "error" in response:
        log(f"List tools failed: {response['error']}", "ERROR")
        return False

    if "result" in response and "tools" in response["result"]:
        tools = response["result"]["tools"]
        log(f"Available tools: {len(tools)}", "INFO")
        for tool in tools:
            log(f"  - {tool['name']}: {tool.get('description', 'No description')[:100]}...", "INFO")

    log("List tools successful", "SUCCESS")
    return True

def test_start_analysis_session(proc):
    """Test starting an analysis session"""
    log(f"Testing start_analysis_session with binary: {TEST_BINARY}", "INFO")

    # Check if binary exists
    if not os.path.exists(TEST_BINARY):
        log(f"Test binary not found: {TEST_BINARY}", "ERROR")
        return False, None

    start_request = {
        "jsonrpc": "2.0",
        "method": "tools/call",
        "params": {
            "name": "start_analysis_session",
            "arguments": {
                "binary_path": TEST_BINARY,
                "task": TEST_TASK
            }
        },
        "id": 3
    }

    if not send_mcp_request(proc, start_request):
        return False, None

    log("Waiting for IDA to start and initialize (this may take a while)...", "INFO")
    response = read_mcp_response(proc, timeout=300)  # 5 minutes timeout for IDA startup

    if not response:
        log("Failed to get start session response", "ERROR")
        return False, None

    if "error" in response:
        log(f"Start session failed: {response['error']}", "ERROR")
        return False, None

    # Extract session ID
    session_id = None
    if "result" in response:
        result = response["result"]
        if "content" in result and isinstance(result["content"], list):
            content_list = result["content"]
            if len(content_list) > 0:
                first_content = content_list[0]
                if isinstance(first_content, dict):
                    session_id = first_content.get("session_id")
                elif isinstance(first_content, str):
                    # Try to parse session ID from text
                    import re
                    match = re.search(r'session_\d+_\d+', first_content)
                    if match:
                        session_id = match.group(0)

    if session_id:
        log(f"Session started successfully: {session_id}", "SUCCESS")
    else:
        log("Session started but couldn't extract session ID", "WARNING")
        log(f"Full response: {json.dumps(response, indent=2)}", "DEBUG")

    return True, session_id

def test_send_message(proc, session_id):
    """Test sending a message to an active session"""
    if not session_id:
        log("No session ID provided, skipping send_message test", "WARNING")
        return False

    log(f"Testing send_message to session: {session_id}", "INFO")

    message_request = {
        "jsonrpc": "2.0",
        "method": "tools/call",
        "params": {
            "name": "send_message",
            "arguments": {
                "session_id": session_id,
                "message": "This is a test of the conversation continuing system, do you still have what we talked about earlier in context?"
            }
        },
        "id": 4
    }

    if not send_mcp_request(proc, message_request):
        return False

    response = read_mcp_response(proc, timeout=120)  # 2 minutes for analysis

    if not response:
        log("Failed to get message response", "ERROR")
        return False

    if "error" in response:
        log(f"Send message failed: {response['error']}", "ERROR")
        return False

    log("Message sent and response received successfully", "SUCCESS")
    return True

def test_close_session(proc, session_id):
    """Test closing a session"""
    if not session_id:
        log("No session ID provided, skipping close_session test", "WARNING")
        return False

    log(f"Testing close_session for: {session_id}", "INFO")

    close_request = {
        "jsonrpc": "2.0",
        "method": "tools/call",
        "params": {
            "name": "close_session",
            "arguments": {
                "session_id": session_id
            }
        },
        "id": 5
    }

    if not send_mcp_request(proc, close_request):
        return False

    response = read_mcp_response(proc, timeout=30)

    if not response:
        log("Failed to get close response", "ERROR")
        return False

    if "error" in response:
        log(f"Close session failed: {response['error']}", "ERROR")
        return False

    log("Session closed successfully", "SUCCESS")
    return True

import threading
import select

def stderr_reader(proc):
    """Read stderr in a separate thread and print it"""
    while True:
        line = proc.stderr.readline()
        if not line:
            break
        print(f"[SERVER STDERR] {line.decode().strip()}", file=sys.stderr)
        sys.stderr.flush()

def main():
    """Main test function"""
    log("=" * 60, "INFO")
    log("Starting MCP Server Tests", "INFO")
    log("=" * 60, "INFO")

    # Start the MCP server
    server_path = "/Users/user/CLionProjects/ida_re_agent/cmake-build-debug/ida_re_mcp_server"

    if not os.path.exists(server_path):
        log(f"MCP server not found at: {server_path}", "ERROR")
        log("Please build the server first", "ERROR")
        return 1

    log(f"Starting MCP server: {server_path}", "INFO")

    try:
        proc = subprocess.Popen(
            [server_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0
        )

        # Start thread to read stderr
        stderr_thread = threading.Thread(target=stderr_reader, args=(proc,))
        stderr_thread.daemon = True
        stderr_thread.start()

        # Give server time to start
        time.sleep(1)

        # Check if server is still running
        if proc.poll() is not None:
            log("Server exited immediately", "ERROR")
            stderr = proc.stderr.read().decode()
            log(f"Server stderr: {stderr}", "ERROR")
            return 1

        log("Server started successfully", "SUCCESS")

        # Run tests
        test_results = {}

        # Test 1: Initialize
        if test_initialize(proc):
            test_results["initialize"] = "PASS"
        else:
            test_results["initialize"] = "FAIL"
            log("Initialize failed, skipping remaining tests", "ERROR")
            proc.terminate()
            return 1

        # Test 2: List tools
        if test_list_tools(proc):
            test_results["list_tools"] = "PASS"
        else:
            test_results["list_tools"] = "FAIL"

        # Test 3: Start analysis session
        success, session_id = test_start_analysis_session(proc)
        if success:
            test_results["start_session"] = "PASS"

            # Test 4: Send message (only if session started)
            if test_send_message(proc, session_id):
                test_results["send_message"] = "PASS"
            else:
                test_results["send_message"] = "FAIL"

            # Test 5: Close session
            if test_close_session(proc, session_id):
                test_results["close_session"] = "PASS"
            else:
                test_results["close_session"] = "FAIL"
        else:
            test_results["start_session"] = "FAIL"
            test_results["send_message"] = "SKIP"
            test_results["close_session"] = "SKIP"

        # Print test summary
        log("=" * 60, "INFO")
        log("Test Summary:", "INFO")
        log("=" * 60, "INFO")

        for test_name, result in test_results.items():
            status = "✓" if result == "PASS" else ("✗" if result == "FAIL" else "○")
            log(f"{status} {test_name}: {result}", "INFO")

        # Terminate server
        log("Terminating server...", "INFO")
        proc.terminate()
        proc.wait(timeout=5)

        # Return success if all tests passed
        failed_tests = [t for t, r in test_results.items() if r == "FAIL"]
        if failed_tests:
            log(f"Tests failed: {', '.join(failed_tests)}", "ERROR")
            return 1
        else:
            log("All tests passed!", "SUCCESS")
            return 0

    except Exception as e:
        log(f"Unexpected error: {e}", "ERROR")
        if 'proc' in locals():
            proc.terminate()
        return 1

if __name__ == "__main__":
    sys.exit(main())