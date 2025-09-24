#!/bin/bash

# sync_to_public.sh - Intelligent context-aware sync from private to public repo
# This version provides TRUE CONTEXT to Claude at every step for better decisions

set -e  # Exit on any error

echo "=== Context-Aware Sync System ==="
echo "Private: /Users/user/CLionProjects/ida_re_agent (with OAuth)"
echo "Public: /Users/user/CLionProjects/ida-swarm (API key only)"
echo ""

PRIVATE_REPO="/Users/user/CLionProjects/ida_re_agent"
PUBLIC_REPO="/Users/user/CLionProjects/ida-swarm"
TEMP_REPO="/tmp/ida-swarm"
WORK_DIR="/tmp/sync_work"
CACHE_DIR="$HOME/.ida_sync_cache"  # Persistent cache for tracking processed files
MAX_PARALLEL_JOBS=1  # Maximum number of parallel Claude instances

# Create persistent cache directory if it doesn't exist
mkdir -p "$CACHE_DIR"

# Cleanup and setup
echo "Setting up workspace..."
rm -rf "$TEMP_REPO" "$WORK_DIR"
cp -r "$PUBLIC_REPO" "$TEMP_REPO"
mkdir -p "$WORK_DIR"

# ============================================================================
# PHASE 1: DISCOVERY - Catalog ALL differences
# ============================================================================
echo ""
echo "=== PHASE 1: Discovery Phase ==="
echo "Cataloging all differences between repositories..."

cd "$PRIVATE_REPO"

# Find ALL files in private repo (not just source)
find . -type f \
    -not -path "./.git/*" \
    -not -path "./build/*" \
    -not -path "./cmake-build-*/*" \
    -not -path "./.idea/*" \
    -not -path "./.claude/*" \
    -not -name "sync_to_public*" \
    -not -name ".DS_Store" \
    -not -name "*.swp" \
    -not -name "*.o" \
    -not -name "*.a" \
    -not -name "*.so" \
    -not -name "*.dylib" | sort > "$WORK_DIR/private_files.txt"

# Find ALL files in public repo (not just source)
cd "$PUBLIC_REPO"
find . -type f \
    -not -path "./.git/*" \
    -not -path "./build/*" \
    -not -path "./cmake-build-*/*" \
    -not -path "./.idea/*" \
    -not -path "./.claude/*" \
    -not -name ".DS_Store" \
    -not -name "*.swp" \
    -not -name "*.o" \
    -not -name "*.a" \
    -not -name "*.so" \
    -not -name "*.dylib" | sort > "$WORK_DIR/public_files.txt"

# Categorize differences
> "$WORK_DIR/new_files.txt"
> "$WORK_DIR/deleted_files.txt"
> "$WORK_DIR/modified_files.txt"
> "$WORK_DIR/oauth_files.txt"
> "$WORK_DIR/needs_processing.txt"
> "$WORK_DIR/already_processed.txt"

# Function to get normalized file hash (ignoring whitespace differences)
get_file_hash() {
    local file="$1"
    # Normalize the file by:
    # - Removing trailing whitespace from each line
    # - Removing blank lines that only contain whitespace
    # - Converting tabs to spaces for consistency
    # - Compressing multiple spaces to single space
    # This ensures insignificant whitespace changes don't trigger reprocessing
    if [[ "$OSTYPE" == "darwin"* ]]; then
        sed 's/[[:space:]]*$//' "$file" | \
        grep -v '^[[:space:]]*$' | \
        sed 's/\t/ /g' | \
        sed 's/  */ /g' | \
        shasum -a 256 | cut -d' ' -f1
    else
        sed 's/[[:space:]]*$//' "$file" | \
        grep -v '^[[:space:]]*$' | \
        sed 's/\t/ /g' | \
        sed 's/  */ /g' | \
        sha256sum | cut -d' ' -f1
    fi
}

# Function to check if file needs OAuth processing
needs_oauth_processing() {
    local file="$1"
    local private_file="$PRIVATE_REPO/$file"
    local public_file="$PUBLIC_REPO/$file"
    local cache_file="$CACHE_DIR/$(echo "$file" | sed 's|/|_|g').hash"

    # If cache file doesn't exist, needs processing
    if [ ! -f "$cache_file" ]; then
        return 0  # true - needs processing
    fi

    # Get stored hash and current private file hash
    local stored_hash=$(cat "$cache_file")
    local current_hash=$(get_file_hash "$private_file")

    # If private file changed since last processing, needs reprocessing
    if [ "$stored_hash" != "$current_hash" ]; then
        return 0  # true - needs processing
    fi

    # Check if public file matches what we expect (private minus OAuth)
    # If public doesn't exist or differs, needs processing
    if [ ! -f "$public_file" ]; then
        return 0  # true - needs processing
    fi

    return 1  # false - already processed and unchanged
}

# Check files in private repo
while IFS= read -r file; do
    # Skip OAuth-specific files entirely
    if echo "$file" | grep -q "oauth"; then
        echo "$file" >> "$WORK_DIR/oauth_files.txt"
        continue
    fi

    public_file="$PUBLIC_REPO/$file"
    private_file="$PRIVATE_REPO/$file"

    if [ ! -f "$public_file" ]; then
        echo "$file" >> "$WORK_DIR/new_files.txt"
        echo "$file" >> "$WORK_DIR/needs_processing.txt"
    elif ! diff -q "$private_file" "$public_file" > /dev/null 2>&1; then
        # Files differ - but check if it's just OAuth removal
        if needs_oauth_processing "$file"; then
            echo "$file" >> "$WORK_DIR/modified_files.txt"
            echo "$file" >> "$WORK_DIR/needs_processing.txt"
        else
            # Already processed for OAuth removal and private hasn't changed
            echo "$file" >> "$WORK_DIR/already_processed.txt"
        fi
    fi
done < "$WORK_DIR/private_files.txt"

# Check for deleted files (in public but not in private)
while IFS= read -r file; do
    private_file="$PRIVATE_REPO/$file"

    # If file exists in public but not in private (and it's not an oauth file we're excluding)
    if [ ! -f "$private_file" ] && ! echo "$file" | grep -q "oauth"; then
        echo "$file" >> "$WORK_DIR/deleted_files.txt"
    fi
done < "$WORK_DIR/public_files.txt"

# Display discovered differences
echo "Discovery complete:"
echo ""

echo "New files to add:"
if [ -s "$WORK_DIR/new_files.txt" ]; then
    while IFS= read -r file; do
        [ -n "$file" ] && echo "  + $file"
    done < "$WORK_DIR/new_files.txt"
else
    echo "  (none)"
fi
echo ""

echo "Modified files to update:"
if [ -s "$WORK_DIR/modified_files.txt" ]; then
    while IFS= read -r file; do
        [ -n "$file" ] && echo "  ≈ $file"
    done < "$WORK_DIR/modified_files.txt"
else
    echo "  (none)"
fi
echo ""

echo "Deleted files to remove:"
if [ -s "$WORK_DIR/deleted_files.txt" ]; then
    while IFS= read -r file; do
        [ -n "$file" ] && echo "  - $file"
    done < "$WORK_DIR/deleted_files.txt"
else
    echo "  (none)"
fi
echo ""

echo "OAuth files excluded:"
if [ -s "$WORK_DIR/oauth_files.txt" ]; then
    while IFS= read -r file; do
        [ -n "$file" ] && echo "  ✗ $file"
    done < "$WORK_DIR/oauth_files.txt"
else
    echo "  (none)"
fi
echo ""

echo "Files already processed (skipping - private unchanged):"
if [ -s "$WORK_DIR/already_processed.txt" ]; then
    count=$(wc -l < "$WORK_DIR/already_processed.txt")
    echo "  ⏭ $count files skipped (OAuth already removed, private repo unchanged)"
    if [ "$count" -le 10 ]; then
        while IFS= read -r file; do
            [ -n "$file" ] && echo "    - $file"
        done < "$WORK_DIR/already_processed.txt"
    fi
else
    echo "  (none)"
fi
echo ""

# ============================================================================
# PHASE 2: SURGICAL OAUTH REMOVAL - Process each file with TRUE CONTEXT
# ============================================================================
echo ""
echo "=== PHASE 2: Surgical OAuth Removal ==="

process_file() {
    local file=$1
    local action=$2  # "new" or "modified"
    local temp_file="$TEMP_REPO/$file"

    # Create log filename by replacing / with _
    local log_file="$WORK_DIR/logs/$(echo "$file" | sed 's|^\./||' | sed 's|/|_|g').log"

    echo "  Processing: $file ($action)"

    # Ensure directory exists
    mkdir -p "$(dirname "$temp_file")"

    # Ensure logs directory exists
    mkdir -p "$WORK_DIR/logs"

    # Copy file to temp location
    cp "$PRIVATE_REPO/$file" "$temp_file"

    # Process with Claude - provide TRUE CONTEXT, capture output to log
    claude --dangerously-skip-permissions --print "
================================================================================
CRITICAL CONTEXT - READ THIS CAREFULLY
================================================================================

You are part of a sync system that copies code from a PRIVATE repository to a
PUBLIC repository. This is for a college application project that MUST be perfect.

SITUATION:
- Private repo: Contains OAuth authentication + API key authentication
- Public repo: Must have API key authentication ONLY (no OAuth)
- Current file: $file
- Action type: $action

YOUR MISSION FOR THIS SPECIFIC FILE:
1. You are processing ONLY the file at: $temp_file
2. Remove ALL OAuth-related code from this file
3. Keep ALL other functionality intact
4. The code must remain FULLY FUNCTIONAL

CRITICAL FIRST STEP - YOU MUST DO THIS:
Before making ANY edits, you MUST read the ENTIRE file into your context.
Use the Read tool to load the complete file. If the file is large (over 1000 lines),
read it in chunks of 1000 lines at a time until you have seen EVERY line.
This is MANDATORY - you CANNOT skip this step.

WHY THIS IS CRITICAL:
- OAuth code can be scattered throughout the file
- OAuth references might be in unexpected places
- You might miss OAuth in parts you haven't read
- Missing even ONE OAuth reference will expose secrets in the public repo

READING STRATEGY:
1. First: Read the file with no offset/limit to try loading it all
2. If too large: Read in 1000-line chunks (offset 0 limit 1000, offset 1000 limit 1000, etc.)
3. Continue until you've read EVERY line of the file
4. Only AFTER reading the ENTIRE file should you begin making edits

YOU MUST LOAD THE ENTIRE FILE INTO CONTEXT BEFORE EDITING!

OAUTH INDICATORS TO REMOVE - MUST BE COMPLETELY PURGED:
- #include statements for oauth headers
- OAuth class instantiations
- OAuth method calls
- OAuth configuration fields
- OAuth-related variables
- OAuth flow implementations
- Client ID/secret references
- Bearer token handling
- Refresh token logic
- Authorization code handling
- Any OAuth-specific error handling
- ANY comments mentioning OAuth (even explanatory comments)
- ANY variable names containing "oauth" (case insensitive)
- ANY function names containing "oauth" (case insensitive)
- ANY enum values related to OAuth
- ANY conditional compilation blocks for OAuth
- ANY logging statements mentioning OAuth
- ANY string literals containing "oauth"
- ANY documentation strings mentioning OAuth

CRITICAL PURGE REQUIREMENT:
You MUST search for and remove ALL occurrences of these patterns:
- "oauth" (case insensitive)
- "OAuth"
- "OAUTH"
- Any variations or abbreviations

ABSOLUTELY NO TRACES OF OAUTH CAN REMAIN - NOT EVEN IN COMMENTS.
This includes removing entire comment blocks that mention OAuth.
If a comment says "// OAuth not supported" - DELETE THE ENTIRE COMMENT.
If a variable is named "enable_oauth" - DELETE THE ENTIRE VARIABLE.
If there's a comment explaining OAuth removal - DELETE IT.

ZERO TOLERANCE FOR ANY OAUTH REFERENCES ANYWHERE.

CRITICAL RULES:
- ONLY edit OAuth-related code
- Do NOT remove non-OAuth features
- Do NOT add stub functions - remove cleanly
- If a function becomes empty after OAuth removal, remove it entirely
- If OAuth is intertwined with other logic, carefully extract ONLY the OAuth parts
- Preserve ALL other functionality (like CodeInjectionManager, NoGoZones, etc.)

IMPORTANT:
- You do NOT need to worry about other files - focus ONLY on this one
- You do NOT need to fix compilation errors - those will be handled later
- Just make this file OAuth-free while keeping all other features

Now edit the file at $temp_file to remove all OAuth references.

Important note if you are editing the IDA Swarm CMakeLists.txt file:
- The private repos project name is project(ida_swarm_private)
- You *MUST* change it in the public repo -> project(ida_swarm)!

CRITICAL TOOL RESTRICTIONS - VIOLATION WILL CRASH THE SYSTEM:
You MUST use ONLY these tools for editing:
- Edit: For single replacements
- MultiEdit: For multiple replacements in the same file

DO NOT USE THESE TOOLS - THEY WILL CAUSE A CRASH:
- Write: Will CRASH the sync system if used
- Bash: Will CRASH if used for editing (sed, awk, echo >, etc.)
- Any other editing method will CRASH

ABSOLUTELY FORBIDDEN - NO FILE CREATION:
You CANNOT create ANY new files ANYWHERE, EVER, under ANY circumstance:
- NO creating files in /tmp/
- NO creating temporary files
- NO creating log files
- NO creating backup files
- NO creating ANY files for ANY reason

If you think you need to create a file, YOU DON'T. The ONLY thing you can do
is EDIT the existing file at $temp_file using Edit/MultiEdit.

The system is configured to ONLY accept Edit/MultiEdit. Using ANY other tool
for file modifications or creating ANY new files will cause an immediate
FATAL ERROR and corrupt the sync.

Be surgical and precise. Leave no OAuth traces.
Remember: You can NOT BE LAZY! It may take awhile, but it is worth it. This project is VERY IMPORTANT TO ME.
" > "$log_file" 2>&1

    if [ $? -eq 0 ]; then
        # Save hash of the private file to cache after successful processing
        local private_file="$PRIVATE_REPO/$file"
        local cache_file="$CACHE_DIR/$(echo "$file" | sed 's|/|_|g').hash"
        get_file_hash "$private_file" > "$cache_file"
        echo "    ✓ Processed and cached"
    else
        echo "    ✗ Failed (check log: $log_file)"
    fi
}

# Export function and variables so they can be used in parallel subshells
export -f process_file
export -f get_file_hash
export PRIVATE_REPO
export TEMP_REPO
export WORK_DIR
export CACHE_DIR

# Only copy already-processed files without reprocessing
echo "Copying already-processed files..."
if [ -s "$WORK_DIR/already_processed.txt" ]; then
    count=$(wc -l < "$WORK_DIR/already_processed.txt")
    echo "  Copying $count files that were already OAuth-cleaned..."
    while IFS= read -r file; do
        [ -z "$file" ] && continue
        public_file="$PUBLIC_REPO/$file"
        temp_file="$TEMP_REPO/$file"
        if [ -f "$public_file" ]; then
            mkdir -p "$(dirname "$temp_file")"
            cp "$public_file" "$temp_file"
        fi
    done < "$WORK_DIR/already_processed.txt"
    echo "  Done copying already-processed files"
fi

# Process only files that need processing
echo "Processing files that need OAuth removal..."
if [ -s "$WORK_DIR/needs_processing.txt" ]; then
    count=$(wc -l < "$WORK_DIR/needs_processing.txt")
    echo "  Processing $count files (max $MAX_PARALLEL_JOBS parallel jobs)..."
    while IFS= read -r file; do
        [ -z "$file" ] && continue
        # Determine if it's new or modified
        if grep -q "^$file$" "$WORK_DIR/new_files.txt"; then
            action="new"
        else
            action="modified"
        fi
        echo "$file:$action"
    done < "$WORK_DIR/needs_processing.txt" | xargs -P "$MAX_PARALLEL_JOBS" -I {} bash -c '
        IFS=":" read -r file action <<< "$1"
        process_file "$file" "$action"
    ' _ {}
    echo "All files processed"
else
    echo "  No files need processing"
fi

# Remove deleted files from temp repo
while IFS= read -r file <&3; do
    [ -z "$file" ] && continue
    temp_file="$TEMP_REPO/$file"
    if [ -f "$temp_file" ]; then
        echo "  Removing deleted file: $file"
        rm -f "$temp_file"
    fi
done 3< "$WORK_DIR/deleted_files.txt"

echo "OAuth removal complete"

# ============================================================================
# PHASE 3: INTELLIGENT BUILD REPAIR - Single Claude with full context
# ============================================================================
echo ""
echo "=== PHASE 3: Intelligent Build Repair ==="

cd "$TEMP_REPO"
rm -rf build
mkdir build
cd build

echo "Testing initial build..."
cmake .. > "$WORK_DIR/cmake_output.txt" 2>&1 || CMAKE_FAILED=true
if [ -z "$CMAKE_FAILED" ]; then
    make -j6 > "$WORK_DIR/make_output.txt" 2>&1 || MAKE_FAILED=true
fi

if [ -z "$CMAKE_FAILED" ] && [ -z "$MAKE_FAILED" ]; then
    echo "✅ Build successful on first try!"
else
    echo "Build failed, invoking intelligent repair..."

    # Single Claude instance with FULL CONTEXT
    claude --dangerously-skip-permissions --print "
================================================================================
CRITICAL BUILD REPAIR CONTEXT - THIS IS EXTREMELY IMPORTANT
================================================================================

You are part of a sync system that copies code from a PRIVATE repository to a
PUBLIC repository. This is for a college application project that MUST be perfect.

WHAT JUST HAPPENED:
1. We copied all files from private repo (with OAuth) to temp workspace
2. Claude instances removed OAuth code from each file individually
3. Now the build is broken

YOUR MISSION:
Fix ONLY the build errors caused by the OAuth removal. If there are issues that are not related to OAuth, that means one of the earlier Claude instances broke the project and I NEED TO KNOW NOW!
This is critical for a college application project that must be PERFECT.

BUILD ERRORS:
$(cat "$WORK_DIR/cmake_output.txt" "$WORK_DIR/make_output.txt" 2>/dev/null | tail -200)

CRITICAL RULES:
1. ONLY fix errors related to OAuth removal:
   - Missing oauth includes
   - Undefined OAuth symbols
   - OAuth parameter mismatches
   - OAuth method calls to nowhere

2. DO NOT touch non-OAuth features:
   - CodeInjectionManager must remain FULLY functional
   - NoGoZoneManager must remain FULLY functional
   - All analysis features must remain FULLY functional
   - All patching features must remain FULLY functional

3. When you see an undefined OAuth symbol:
   - Remove the code that uses it
   - Do NOT add stubs or dummy implementations
   - Do NOT disable features unrelated to OAuth

4. Common OAuth removal patterns:
   - Remove config->api.auth_method references
   - Remove OAuth manager instantiations
   - Remove OAuth credential handling
   - Simplify authentication to API key only

5. BE CAREFUL:
   - Some errors might LOOK OAuth-related but aren't
   - CodeInjectionManager is NOT OAuth - keep it functional
   - NoGoZoneManager is NOT OAuth - keep it functional

6. COMPLETE OAUTH PURGE REQUIREMENT:
   - If you find ANY remaining OAuth references while fixing build errors, REMOVE THEM
   - This includes comments, variable names, function names, string literals
   - Search for "oauth" (case insensitive) and purge ALL occurrences
   - NO OAUTH TRACES CAN REMAIN ANYWHERE IN THE CODEBASE

WORKSPACE: $TEMP_REPO

Start by exploring the errors deeply with grep and Read tools.
Understand each error before fixing.
Make surgical edits to fix ONLY OAuth-related build issues.
After fixing, try building again with:
  cd $TEMP_REPO/build && cmake .. && make -j6

Fix the build now, being extremely careful to preserve all non-OAuth features.
"
fi

# ============================================================================
# PHASE 4: FEATURE VERIFICATION - Ensure nothing was lost
# ============================================================================
echo ""
echo "=== PHASE 4: Feature Verification ==="

# Generate comprehensive diff against PRIVATE repo to see ALL differences
diff -ur "$PRIVATE_REPO" "$TEMP_REPO" \
    --exclude=.git \
    --exclude=build \
    --exclude=cmake-build-* \
    --exclude=.idea \
    --exclude=.claude \
    --exclude=sync_to_public* \
    --exclude=.DS_Store > "$WORK_DIR/final_diff.txt" 2>/dev/null || true

# Verify with Claude
VERIFICATION=$(claude --dangerously-skip-permissions --print "
================================================================================
FINAL VERIFICATION - CRITICAL FOR COLLEGE APPLICATION
================================================================================

CONTEXT:
You are verifying the final sync from private (OAuth+API) to public (API only).
This project is for college applications and MUST be perfect.

DIFF INTERPRETATION:
The diff below shows PRIVATE_REPO vs TEMP_REPO (which will become public).
Lines with '-' exist in PRIVATE but not in TEMP (removed)
Lines with '+' exist in TEMP but not in PRIVATE (added/modified)

The ONLY acceptable differences should be OAuth-related removals.
Everything else should be IDENTICAL. You are our LAST LINE OF DEFENSE BEFORE THESE CHANGES ARE PUSHED PUBLICLY.
We CAN NOT EXPOSE THE EXISTENCE OF OAUTH! and we CAN NOT SHIP BROKEN / REMOVED FEATURES IF THEY EXIST IN PRIVATE!

CHANGES TO VERIFY:
$(cat "$WORK_DIR/final_diff.txt")

VERIFICATION CHECKLIST:
1. SCAN PUBLIC CODE FOR OAUTH TRACES (CRITICAL):
   - Use Grep tool to search workspace $TEMP_REPO for 'oauth' (case insensitive)
   - Use Grep tool to search workspace $TEMP_REPO for 'OAuth'
   - Use Grep tool to search workspace $TEMP_REPO for 'OAUTH'
   - Check comments, variable names, function names, string literals
   - If you find ANY OAuth reference anywhere, VERIFICATION MUST FAIL

2. ALL OAuth code removed (auth_method, oauth_manager, flows, tokens)
3. ALL other features preserved:
   - CodeInjectionManager fully functional
   - NoGoZoneManager fully functional
   - Consensus execution working
   - Patching systems intact
   - Analysis features complete
   - IRC communication working
   - Agent spawning functional

4. Check for accidentally removed features:
   - Were any non-OAuth functions deleted?
   - Were any non-OAuth classes removed?
   - Were any non-OAuth parameters stripped?

CRITICAL: Look for patterns like:
- 'removed - no-go zones not applied' (BAD - feature removed)
- 'removed - OAuth stripping' (OK - OAuth removed)

RESPONSE FORMAT:
Start with one of these EXACT strings:
'VERIFICATION: FAILED - [reason]' - if features were lost
'VERIFICATION: PASSED' - if only OAuth was removed

Then provide details about what you found.
")

echo "$VERIFICATION"

if echo "$VERIFICATION" | grep -q "VERIFICATION: PASSED"; then
    echo ""
    echo "✅ Verification passed!"

    # Apply changes
    echo "Applying changes to public repository..."
    rsync -av --delete \
        --exclude=.git \
        --exclude=build \
        --exclude=.idea \
        --exclude=.DS_Store \
        --exclude=sync_to_public* \
        "$TEMP_REPO/" "$PUBLIC_REPO/"

    echo ""
    echo "✅ Sync complete! Changes applied to public repository."
    echo ""
    echo "Launching Kaleidoscope to review differences..."

    # Launch Kaleidoscope to compare private and public repos
    if command -v ksdiff >/dev/null 2>&1; then
        ksdiff "$PRIVATE_REPO" "$PUBLIC_REPO" &
        echo "Kaleidoscope opened for review."
    else
        echo "Kaleidoscope not found. Please review with: cd $PUBLIC_REPO && git diff"
    fi
else
    echo ""
    echo "❌ Verification failed! Features were lost during sync."
    echo "Changes NOT applied. Please review the issues above."
    exit 1
fi

# Cleanup
rm -rf "$TEMP_REPO" "$WORK_DIR"