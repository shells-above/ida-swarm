#!/bin/bash

# SECURITY WARNING: This file must NEVER be in the public repository
# It contains OAuth filtering logic and security patterns that must remain private
#
# sync_to_public.sh - Chunk-based intelligent sync from private to public repo
# Processes each change individually for maximum safety and precision

set -e  # Exit on any error

echo "=== Chunk-Based Intelligent Sync System ==="
echo "Private: /Users/user/CLionProjects/ida_re_agent"
echo "Public: /Users/user/CLionProjects/ida-swarm"
echo ""

PRIVATE_REPO="/Users/user/CLionProjects/ida_re_agent"
PUBLIC_REPO="/Users/user/CLionProjects/ida-swarm"
TEMP_REPO="/tmp/ida-swarm"

# Create temp copy of public repo
echo "Creating temporary workspace..."
rm -rf "$TEMP_REPO"
cp -r "$PUBLIC_REPO" "$TEMP_REPO"

# Create working directory for chunks
CHUNK_DIR="/tmp/sync_chunks"
rm -rf "$CHUNK_DIR"
mkdir -p "$CHUNK_DIR"

# Phase 1: Generate comprehensive diff
echo "Phase 1: Analyzing differences between repositories..."
cd "$PRIVATE_REPO"

# Get list of all files in private repo (excluding .git, build dirs, etc)
find . -type f \
    -not -path "./.git/*" \
    -not -path "./build/*" \
    -not -path "./cmake-build-*/*" \
    -not -path "./.idea/*" \
    -not -path "./.claude/*" \
    -not -name ".DS_Store" \
    -not -name "sync_to_public.sh" \
    -not -name "*.swp" \
    -not -name "*.o" \
    -not -name "*.a" \
    -not -name "*.so" \
    -not -name "*.dylib" | sort > /tmp/private_files.txt

# Process each file to find differences
echo "Processing differences file by file..."
chunk_counter=0
total_chunks=0

while IFS= read -r file; do
    # Skip if file doesn't exist in public repo (new file) or is identical
    public_file="$PUBLIC_REPO/$file"
    private_file="$PRIVATE_REPO/$file"

    if [ ! -f "$public_file" ]; then
        # New file in private repo
        echo "  New file: $file"
        {
            echo "NEW_FILE: $file"
            echo "---"
            cat "$private_file"
        } > "$CHUNK_DIR/chunk_${chunk_counter}.txt"
        ((chunk_counter++))
        ((total_chunks++))
    elif ! diff -q "$private_file" "$public_file" > /dev/null 2>&1; then
        # File exists but is different
        echo "  Modified file: $file"
        {
            echo "MODIFIED_FILE: $file"
            echo "---"
            diff -u "$public_file" "$private_file" || true
        } > "$CHUNK_DIR/chunk_${chunk_counter}.txt"
        ((chunk_counter++))
        ((total_chunks++))
    fi
done < /tmp/private_files.txt

if [ "$total_chunks" -eq 0 ]; then
    echo "No differences found between repositories."
    exit 0
fi

echo "Found $total_chunks file(s) with differences."
echo ""

# Phase 2: Process each chunk with Claude (parallel processing)
echo "Phase 2: Analyzing and applying safe changes..."

# Function to process a single chunk
process_chunk() {
    local chunk_file=$1
    local chunk_num=$2
    local total=$3

    # Extract filename from chunk
    local filename=$(head -1 "$chunk_file" | sed 's/.*: //')

    echo "  Starting chunk $chunk_num/$total: $filename"

    # Have Claude analyze and apply this specific change
    claude --dangerously-skip-permissions "
CRITICAL: You are processing ONE SPECIFIC FILE in a PARALLEL operation.

PARALLEL PROCESSING CONTEXT:
- Multiple Claude instances are running simultaneously
- Each instance handles ONE file independently
- Other instances are processing other files at the same time
- You do NOT need to worry about other files - just focus on yours

CONTEXT:
- Private repo contains OAuth authentication (MUST stay private)
- Public repo is on GitHub (MUST NOT have OAuth)
- You are reviewing ONE file's changes to decide if they're safe

FILE BEING PROCESSED: $filename

CHANGE TO ANALYZE:
$(cat "$chunk_file")

RULES:
1. If this change contains OAuth/authentication code: DO NOTHING
2. If this change is safe (no auth secrets): Apply it SURGICALLY to /tmp/ida-swarm
3. For mixed files (like CMakeLists.txt): Apply ONLY non-OAuth parts
4. For NEW_FILE: Create the new file (preserving full directory structure)
   - Use mkdir -p to ensure parent directories exist
   - Other instances might also be creating directories - that's OK

OAUTH INDICATORS TO EXCLUDE:
- The sync_to_public.sh script itself (contains security filtering logic)
- Files named oauth_* or *oauth*
- 'oauth', 'OAuth', 'client_secret', 'client_id'
- 'authorization_code', 'refresh_token', 'bearer_token'
- OAuth flow implementations
- Authentication endpoints

IMPORTANT FOR PARALLEL PROCESSING:
- Focus ONLY on the file assigned to you
- Don't worry about dependencies in other files
- Create parent directories if needed (mkdir -p is safe to run multiple times)
- Be SURGICAL and PRECISE with your specific file
- Use Edit or MultiEdit for modifications to existing files
- Use Write tool for creating new files

Analyze this change and apply it to /tmp/ida-swarm if safe.
If it contains ANY OAuth code, skip it entirely.
" > /dev/null 2>&1 || true

    echo "  Completed chunk $chunk_num/$total: $filename"
}

export -f process_chunk  # Export function for parallel to use

# Process chunks in parallel
processed=0
batch_size=50

for batch_start in $(seq 0 $batch_size $((total_chunks-1))); do
    batch_end=$((batch_start + batch_size - 1))
    if [ $batch_end -ge $total_chunks ]; then
        batch_end=$((total_chunks - 1))
    fi

    current_batch_size=$((batch_end - batch_start + 1))
    echo "Processing batch: chunks $((batch_start+1))-$((batch_end+1)) ($current_batch_size chunks in parallel)"

    # Launch parallel processes for this batch
    pids=()
    for i in $(seq $batch_start $batch_end); do
        if [ -f "$CHUNK_DIR/chunk_${i}.txt" ]; then
            ((processed++))
            process_chunk "$CHUNK_DIR/chunk_${i}.txt" $processed $total_chunks &
            pids+=($!)
        fi
    done

    # Wait for all parallel processes in this batch to complete
    for pid in ${pids[@]}; do
        wait $pid
    done

    echo "Batch complete. Processed $processed/$total_chunks chunks so far."
    echo ""
done

echo "All chunks processed."
echo ""

# Phase 3: Generate final diff for verification
echo "Phase 3: Generating final changeset..."
cd "$PUBLIC_REPO"
diff -ur . "$TEMP_REPO" > /tmp/final_sync.diff 2>/dev/null || true

# Remove diff headers for empty diff
if [ ! -s /tmp/final_sync.diff ]; then
    echo "No changes to sync."
    exit 0
fi

# Phase 4: Security verification with Claude
echo "Phase 4: Security verification..."
VERIFICATION_RESULT=$(claude --dangerously-skip-permissions --print "
CRITICAL SECURITY AUDIT

You must verify these changes are safe for PUBLIC GitHub repository.

CHANGES TO REVIEW:
$(cat /tmp/final_sync.diff)

VERIFICATION CHECKS:
1. Search for ANY OAuth/authentication code
2. Check for client_secret, client_id, tokens
3. Verify no authentication flows exposed
4. Ensure no OAuth files included

RESPOND WITH:
'VERIFICATION: SAFE' - if absolutely clean
'VERIFICATION: UNSAFE' - if ANY auth code found

Be paranoid. This is going on public GitHub.
")

echo "$VERIFICATION_RESULT"
echo ""

if ! echo "$VERIFICATION_RESULT" | grep -q "VERIFICATION: SAFE"; then
    echo "❌ Security verification FAILED"
    echo "Sync aborted - no changes applied."
    exit 1
fi

# Phase 5: Build test with auto-repair loop
echo "Phase 5: Testing build with auto-repair..."
cd "$TEMP_REPO"
if [ -d "build" ]; then
    rm -rf build
fi
mkdir build
cd build

attempt=0
max_attempts=10  # Safety limit to prevent infinite loops

while true; do
    ((attempt++))
    echo "Build attempt $attempt..."

    # Try cmake first
    echo "Running cmake..."
    if ! cmake .. > /tmp/cmake_output.txt 2>&1; then
        echo "CMake failed, attempting repair..."
        CMAKE_ERROR=$(tail -50 /tmp/cmake_output.txt)
    else
        # Try make
        echo "Running make..."
        if make -j6 > /tmp/make_output.txt 2>&1; then
            echo "✅ Build successful!"
            break
        fi
        echo "Make failed, attempting repair..."
        CMAKE_ERROR=""
    fi

    if [ $attempt -gt $max_attempts ]; then
        echo "❌ Exceeded maximum repair attempts: $max_attempts"
        echo "Sync aborted - unable to fix build"
        exit 1
    fi

    # Capture build error
    if [ -z "$CMAKE_ERROR" ]; then
        BUILD_ERROR=$(make 2>&1 || true)  # Don't exit on make failure
    else
        BUILD_ERROR="CMAKE ERROR:\n$CMAKE_ERROR"
    fi

    echo "Invoking Claude to fix OAuth-related build issues (attempt $attempt)..."

    # Spawn Claude to fix - capture output to check for abort
    REPAIR_OUTPUT=$(claude --dangerously-skip-permissions --print "
CRITICAL BUILD REPAIR CONTEXT:

YOU ARE RUNNING INSIDE A SHELL SCRIPT. Your output will be parsed programmatically.

SITUATION:
- We synced code from private repo (has OAuth) to public repo (no OAuth)
- OAuth code was stripped out, but some references were missed
- The build is now failing, likely due to missing OAuth dependencies
- You must fix ONLY OAuth-related build failures

BUILD ERROR TO FIX:
$BUILD_ERROR

YOUR MISSION:
1. DEEP EXPLORATION FIRST (THIS IS CRITICAL):
   - Grep for ALL occurrences of the error symbols
   - Read the error file AND all files that include it
   - Trace the dependency chain completely
   - Understand WHY the error exists before fixing

2. DETERMINE IF THIS IS OAUTH-RELATED:
   - Missing auth_method, oauth fields, oauth includes = OAuth-related
   - Missing OAuth manager/flow/authorizer references = OAuth-related
   - Unrelated bugs, logic errors, other issues = NOT OAuth-related

3. FIX OR ABORT:
   - If OAuth-related: Fix surgically (remove references, not add stubs)
   - If NOT OAuth-related: Output 'ABORT: NON-OAUTH BUG DETECTED' and explain

CRITICAL SHELL SCRIPT REQUIREMENT:
If you detect a non-OAuth bug, you MUST output EXACTLY this string:
'ABORT: NON-OAUTH BUG DETECTED'

This string is parsed by the shell script. Without it, broken code will ship.
The script checks for this string to know when to stop.

Common OAuth-related fixes:
- Remove references to config->api.auth_method
- Remove includes of oauth_*.h files
- Remove OAuth manager instantiations
- Remove OAuth flow method calls

The public project is named ida_swarm, the private project is named ida_swarm_private
Make sure the public version appears as ida_swarm!
If you are working on CMakeLists.txt, make sure you update the project name!

Take your time. Explore deeply. Think carefully.
Fix the build now, or abort if it's not OAuth-related.
" 2>&1)

    echo "$REPAIR_OUTPUT"

    # Check if Claude detected non-OAuth bug
    if echo "$REPAIR_OUTPUT" | grep -q "ABORT: NON-OAUTH BUG DETECTED"; then
        echo ""
        echo "❌ CRITICAL: Non-OAuth bug detected in code"
        echo "This is not a sync issue - there are real bugs that need fixing"
        echo "Sync aborted to prevent shipping broken code"
        exit 1
    fi

    echo "Repair attempt complete, retrying build..."
    echo ""
done

echo "✅ Build test passed after $attempt attempts"
echo ""

# Phase 6: Apply changes to actual public repo
echo "Phase 6: Ready to apply changes"
rsync -av --delete \
    --exclude=.git \
    --exclude=build \
    --exclude=.idea \
    --exclude=.DS_Store \
    "$TEMP_REPO/" "$PUBLIC_REPO/"

# Phase 7: User review and commit
cd "$PUBLIC_REPO"

echo ""
echo "=== Changes Summary ==="
git status --short
echo ""
git diff --stat
echo ""

# Interactive review loop
while true; do
    echo "=== Review Options ==="
    echo "[r] Review detailed diff"
    echo "[c] Commit and push changes"
    echo "[a] Abort (discard changes)"
    echo ""
    read -p "Your choice: " -n 1 -r choice
    echo ""

    case "$choice" in
        r|R)
            echo "Opening diff viewer (press 'q' to exit)..."
            git diff --color | less -R
            ;;
        c|C)
            # Get commit message
            echo ""
            read -p "Enter commit message (or Enter for default): " commit_msg
            if [ -z "$commit_msg" ]; then
                commit_msg="Sync safe changes from private repo"
            fi

            # Commit and push
            echo "Committing..."
            git add -A
            git commit -m "$commit_msg"

            echo "Pushing to origin..."
            git push origin

            echo ""
            echo "✅ Successfully synced to public repository!"

            # Cleanup
            rm -rf "$CHUNK_DIR" "$TEMP_REPO" /tmp/final_sync.diff
            exit 0
            ;;
        a|A)
            echo "Aborting..."
            git reset --hard HEAD
            git clean -fd

            # Cleanup
            rm -rf "$CHUNK_DIR" "$TEMP_REPO" /tmp/final_sync.diff

            echo "❌ Sync aborted - changes discarded"
            exit 1
            ;;
        *)
            echo "Invalid choice. Please select r, c, or a."
            ;;
    esac
done