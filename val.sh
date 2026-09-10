#!/usr/bin/env bash
set -euo pipefail

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLATFORM="$(uname -s)"

if [[ "$PLATFORM" == "Darwin" ]]; then
    PLUGINVAL="${PLUGINVAL:-$HOME/pluginval/Builds/Release/pluginval_artefacts/Release/pluginval.app/Contents/MacOS/pluginval}"
else
    PLUGINVAL="${PLUGINVAL:-pluginval}"
fi
strlvl="${strlvl:-10}"
conf="${conf:-Debug}"

VST3_PATH="${VST3_PATH:-$PROJECT_ROOT/build/ulichperc_artefacts/$conf/VST3/ulichpercs.vst3}"
AU_PATH="${AU_PATH:-$PROJECT_ROOT/build/ulichperc_artefacts/$conf/AU/ulichpercs.component}"

# -----------------------------------------------------------------------------
# Checks
# -----------------------------------------------------------------------------

if ! command -v "$PLUGINVAL" >/dev/null 2>&1; then
    echo "ERROR: pluginval not found: $PLUGINVAL" >&2
    echo >&2
    echo "Set PLUGINVAL to its executable path, or use PLUGINVAL=pluginval if it is on PATH:" >&2
    echo "  PLUGINVAL=/full/path/to/pluginval \"$0\"" >&2
    exit 1
fi

case "$strlvl" in
    [1-9]|10) ;;
    *) echo "ERROR: strlvl must be an integer from 1 to 10." >&2; exit 1 ;;
esac

if [[ ! -e "$VST3_PATH" ]]; then
    echo "ERROR: VST3 not found:" >&2
    echo "  $VST3_PATH" >&2
    echo "Build ulichperc_VST3 for $conf first, or set conf / VST3_PATH." >&2
    exit 1
fi

if [[ "$PLATFORM" == "Darwin" && ! -e "$AU_PATH" ]]; then
    echo "ERROR: AU not found:" >&2
    echo "  $AU_PATH" >&2
    echo "Build ulichperc_AU for $conf first, or set conf / AU_PATH." >&2
    exit 1
fi

LOG_ROOT="$PROJECT_ROOT/validation-logs"
RUN_TIMESTAMP="$(date '+%Y%m%d-%H%M%S')"
mkdir -p "$LOG_ROOT"
# Keep concurrent runs, or runs started within the same second, separate.
RUN_DIR="$(mktemp -d "$LOG_ROOT/$RUN_TIMESTAMP-XXXXXX")"

# Convenient pointer to the most recent validation run.
ln -sfn "$(basename "$RUN_DIR")" "$LOG_ROOT/latest"

# -----------------------------------------------------------------------------
# Validation helper
# -----------------------------------------------------------------------------

FAILURES=0

validate_plugin()
{
    local name="$1"
    local path="$2"
    local terminal_log="$RUN_DIR/${name}-terminal.log"

    echo
    echo "================================================================"
    echo "Validating $name"
    echo "================================================================"
    echo "Plugin:     $path"
    echo "Strictness: $strlvl"
    echo "Logs:       $RUN_DIR"
    echo

    # Run both formats even if the first validation fails.
    set +e
    "$PLUGINVAL" \
        --validate "$path" \
        --strictness-level "$strlvl" \
        --verbose \
        --output-dir "$RUN_DIR" \
        2>&1 | tee "$terminal_log"
    local pipeline_status=("${PIPESTATUS[@]}")
    set -e

    if [[ ${pipeline_status[0]} -ne 0 ]]; then
        echo
        echo "FAIL: $name (pluginval exit code ${pipeline_status[0]})"
        FAILURES=$((FAILURES + 1))
    elif [[ ${pipeline_status[1]} -ne 0 ]]; then
        echo
        echo "FAIL: $name (could not save terminal log)"
        FAILURES=$((FAILURES + 1))
    else
        echo
        echo "PASS: $name"
    fi
}

# -----------------------------------------------------------------------------
# Run validation
# -----------------------------------------------------------------------------

validate_plugin "vst3" "$VST3_PATH"

if [[ "$PLATFORM" == "Darwin" ]]; then
    validate_plugin "au" "$AU_PATH"
else
    echo
    echo "Skipping AU validation: AU is macOS-only."
fi

# -----------------------------------------------------------------------------
# Result
# -----------------------------------------------------------------------------

echo
echo "================================================================"
echo "Validation summary"
echo "================================================================"
echo "Logs:"
echo "  $RUN_DIR"
echo
echo "Latest:"
echo "  $LOG_ROOT/latest"
echo

if [[ $FAILURES -eq 0 ]]; then
    echo "ALL VALIDATIONS PASSED"
    exit 0
else
    echo "$FAILURES VALIDATION(S) FAILED"
    exit 1
fi
