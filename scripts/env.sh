# Source this to get ESP-IDF v5.5.3 (installed by eim) into the current shell:
#   . scripts/env.sh
# The eim activation script insists on being sourced interactively; its -e
# mode prints the variables instead, which is what is used here.
_ACT="$HOME/.espressif/tools/activate_idf_v5.5.3.sh"
if [ -f "$_ACT" ]; then
    _ENV="$(sh "$_ACT" -e 2>/dev/null)"
    _TOOLS_PATH="$(printf '%s\n' "$_ENV" | sed -n 's/^PATH=//p')"
    _SYS_PATH="$(printf '%s\n' "$_ENV" | sed -n 's/^SYSTEM_PATH=//p')"
    export IDF_PATH="$(printf '%s\n' "$_ENV" | sed -n 's/^IDF_PATH=//p')"
    export IDF_TOOLS_PATH="$(printf '%s\n' "$_ENV" | sed -n 's/^IDF_TOOLS_PATH=//p')"
    export IDF_PYTHON_ENV_PATH="$(printf '%s\n' "$_ENV" | sed -n 's/^IDF_PYTHON_ENV_PATH=//p')"
    export ESP_ROM_ELF_DIR="$(printf '%s\n' "$_ENV" | sed -n 's/^ESP_ROM_ELF_DIR=//p')"
    export OPENOCD_SCRIPTS="$(printf '%s\n' "$_ENV" | sed -n 's/^OPENOCD_SCRIPTS=//p')"
    export PATH="$_TOOLS_PATH:$IDF_PATH/tools:${_SYS_PATH:-$PATH}"
    unset _ENV _TOOLS_PATH _SYS_PATH
fi
unset _ACT
